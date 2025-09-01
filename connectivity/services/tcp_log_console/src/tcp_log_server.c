// tcp_log_server.c  (ESP-IDF 5.x)
// Console TCP para logs, com ring buffer, bind no IP do AP e envio non-blocking.

#include "tcp_log_server.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#include "esp_log.h"
#include "esp_netif.h"       // <-- novo (pegar IP do AP)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <fcntl.h>

static const char *TAG = "TCP_LOG";

/* ===================== RING BUFFER ===================== */
#define LOG_RBUF_SZ   (8 * 1024)
static char   s_rbuf[LOG_RBUF_SZ];
static size_t s_rhead = 0;
static bool   s_rfull = false;
static SemaphoreHandle_t s_rmtx = NULL;

static void rbuf_init(void) {
    if (!s_rmtx) {
        s_rmtx = xSemaphoreCreateMutex();
    }
}

static void rbuf_write(const char *data, size_t len) {
    if (!s_rmtx) {
        rbuf_init();
    }
    xSemaphoreTake(s_rmtx, portMAX_DELAY);

    size_t off = 0;
    while (off < len) {
        size_t chunk = LOG_RBUF_SZ - s_rhead;
        if (chunk > (len - off)) chunk = len - off;
        memcpy(s_rbuf + s_rhead, data + off, chunk);
        s_rhead = (s_rhead + chunk) % LOG_RBUF_SZ;
        if (s_rhead == 0) s_rfull = true;
        off += chunk;
    }

    xSemaphoreGive(s_rmtx);
}

static size_t rbuf_snapshot(char *out, size_t out_sz) {
    if (!s_rmtx) {
        rbuf_init();
    }
    xSemaphoreTake(s_rmtx, portMAX_DELAY);

    size_t total = s_rfull ? LOG_RBUF_SZ : s_rhead;
    if (total > out_sz) total = out_sz;
    size_t start = s_rfull ? s_rhead : 0;
    for (size_t i = 0; i < total; ++i) {
        out[i] = s_rbuf[(start + i) % LOG_RBUF_SZ];
    }

    xSemaphoreGive(s_rmtx);
    return total;
}
/* ======================================================= */

/* ====================== TCP server ===================== */
static TaskHandle_t s_srv_task    = NULL;
static int          s_server_sock = -1;
static int          s_client_sock = -1;
static uint16_t     s_port        = 3333;

// NEW: fecha cliente se a pilha ficar bloqueada por muito tempo (EAGAIN/EWOULDBLOCK)
static uint16_t     s_again_strikes = 0;
#define AGAIN_STRIKES_LIMIT 200   // ~200 chamadas de log (~2 s, depende do volume de logs)

static void close_client(void) {
    if (s_client_sock >= 0) {
        shutdown(s_client_sock, SHUT_RDWR);
        close(s_client_sock);
        s_client_sock = -1;
    }
    s_again_strikes = 0;      // NEW
}

bool tcp_log_client_connected(void) {
    return s_client_sock >= 0;
}

// Fecha o cliente TCP atual (se existir). Útil ao derrubar o AP.
void tcp_log_force_close_client(void) {
       // fecha a sessão TCP atual (se houver) sem travar
    if (s_client_sock >= 0) {
        int fd = s_client_sock;
        s_client_sock = -1;  // invalida já, para que o logger não tente usar
        lwip_shutdown(fd, SHUT_RDWR);
        lwip_close(fd);
    }
    s_again_strikes = 0;
}

/* vprintf hook: guarda no buffer e tenta enviar ao cliente sem bloquear */
int tcp_log_vprintf(const char *fmt, va_list args) {
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf) - 3, fmt, args);
    if (n <= 0) return 0;
    if (n > (int)sizeof(buf) - 3) n = sizeof(buf) - 3;

    // normaliza CRLF
    if (buf[n - 1] == '\n') {
        buf[n - 1] = '\r';
        buf[n]     = '\n';
        buf[n + 1] = '\0';
        n += 1;
    } else {
        buf[n]   = '\r';
        buf[n+1] = '\n';
        buf[n+2] = '\0';
        n += 2;
    }

    rbuf_write(buf, n);

    if (s_client_sock >= 0) {
    int sent = send(s_client_sock, buf, n, MSG_DONTWAIT);
    if (sent < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            // pilha ocupada: acumula strikes e fecha para reconectar limpo
            if (++s_again_strikes > AGAIN_STRIKES_LIMIT) {
                ESP_LOGW(TAG, "Client stalled (%u strikes) → closing", s_again_strikes);
                close_client();
            }
            return n; // não bloqueia nem perde o fluxo do logger
        } else {
            close_client();   // erro real
            return n;
        }
    }
    s_again_strikes = 0;      // sucesso → zera strikes
    return sent;
}
    return n;
}

static void tcp_log_server_task(void *arg) {
    (void)arg;

    for (;;) {
        s_server_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (s_server_sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int opt = 1;
        setsockopt(s_server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // === bind preso ao IP do AP (192.168.4.1 tipicamente) ===
        esp_netif_ip_info_t ip = {0};
        esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (ap_netif) (void)esp_netif_get_ip_info(ap_netif, &ip);

        struct sockaddr_in addr = {0};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(s_port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(s_server_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            close(s_server_sock);
            s_server_sock = -1;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (listen(s_server_sock, 1) < 0) {
            close(s_server_sock);
            s_server_sock = -1;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ESP_LOGI(TAG, "Waiting TCP on port %u...", s_port);

        struct sockaddr_in raddr;
        socklen_t rlen = sizeof(raddr);
        s_client_sock = accept(s_server_sock, (struct sockaddr *)&raddr, &rlen);
        if (s_client_sock < 0) {
            close(s_server_sock);
            s_server_sock = -1;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        s_again_strikes = 0; 
        ESP_LOGI(TAG, "Client connected");
        close(s_server_sock);
        s_server_sock = -1;

        // non-blocking no socket do cliente
        int flags = lwip_fcntl(s_client_sock, F_GETFL, 0);
        lwip_fcntl(s_client_sock, F_SETFL, flags | O_NONBLOCK);

        // Keepalive (se suportado pela build)
#ifdef SO_KEEPALIVE
        int ka = 1; setsockopt(s_client_sock, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof(ka));
#endif
#ifdef TCP_KEEPIDLE
        int idle = 30; setsockopt(s_client_sock, IPPROTO_TCP, TCP_KEEPIDLE,  &idle, sizeof(idle));
#endif
#ifdef TCP_KEEPINTVL
        int intvl = 5; setsockopt(s_client_sock, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
#endif
#ifdef TCP_KEEPCNT
        int cnt = 3; setsockopt(s_client_sock, IPPROTO_TCP, TCP_KEEPCNT,   &cnt,  sizeof(cnt));
#endif

        // Banner + dump inicial
        static const char *banner =
            "\r\n=== TCP LOG CONSOLE READY ===\r\n"
            "(AP bound console, non-blocking)\r\n\r\n";
        send(s_client_sock, banner, strlen(banner), MSG_DONTWAIT);

        static char dump_buf[LOG_RBUF_SZ];
        size_t dn = rbuf_snapshot(dump_buf, sizeof(dump_buf));
        if (dn > 0) (void)send(s_client_sock, dump_buf, dn, MSG_DONTWAIT);

        // Loop: consome input (Enter etc.) e detecta desconexão
        while (s_client_sock >= 0) {
            char sink[64];
            int r = recv(s_client_sock, sink, sizeof(sink), MSG_DONTWAIT);
            if (r == 0) {            // peer fechou
                close_client();
                break;
            } else if (r < 0) {
                if (errno != EWOULDBLOCK && errno != EAGAIN) {
                    close_client();
                    break;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

esp_err_t start_tcp_log_server_task(uint16_t port) {
    s_port = port;
    if (s_srv_task) {
        return ESP_OK;
    }
    BaseType_t ok = xTaskCreate(tcp_log_server_task, "tcp_log_srv", 4096, NULL, 4, &s_srv_task);
    return (ok == pdPASS) ? ESP_OK : ESP_FAIL;
}

void stop_tcp_log_server_task(void) {
    if (s_srv_task) {
        vTaskDelete(s_srv_task);
        s_srv_task = NULL;
    }
    if (s_server_sock >= 0) {
        close(s_server_sock);
        s_server_sock = -1;
    }
    close_client();
}

/* =========================== TEE de log =========================== */
static vprintf_like_t s_prev_logger = NULL;

static int tee_vprintf(const char *fmt, va_list args) {
    (void)tcp_log_vprintf(fmt, args);
    va_list copy;
    va_copy(copy, args);
    int r = s_prev_logger ? s_prev_logger(fmt, copy) : vprintf(fmt, copy);
    va_end(copy);
    return r;
}

void tcp_log_install_tee(void) {
    if (!s_prev_logger) {
        s_prev_logger = esp_log_set_vprintf(tee_vprintf);
    }
}

void tcp_log_uninstall_tee(void) {
    if (s_prev_logger) {
        (void)esp_log_set_vprintf(s_prev_logger);
        s_prev_logger = NULL;
    }
}

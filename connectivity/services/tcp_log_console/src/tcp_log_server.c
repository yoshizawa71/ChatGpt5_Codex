// tcp_log_server.c  (ESP-IDF 5.x)
// Console TCP para logs, com ring buffer, bind em 0.0.0.0 e envio non-blocking.

#include "tcp_log_server.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <fcntl.h>
#include <netinet/in.h>

static const char *TAG = "TCP_LOG";

/* ===================== RING BUFFER ===================== */
#define LOG_RBUF_SZ   (8 * 1024)

static char   s_rbuf[LOG_RBUF_SZ];
static size_t s_rhead = 0;
static bool   s_rfull = false;
static portMUX_TYPE s_rspin = portMUX_INITIALIZER_UNLOCKED;

static int tcp_send_norm(int fd, const char *buf, size_t n)
{
    // Converte LF -> CRLF por blocos, sem alocar heap
    char tmp[512];
    size_t i = 0;
    while (i < n) {
        size_t o = 0;
        while (i < n && o < sizeof(tmp)-2) {
            char c = buf[i++];
            if (c == '\n') { tmp[o++] = '\r'; tmp[o++] = '\n'; }
            else           { tmp[o++] = c; }
        }
        int sent = lwip_send(fd, tmp, (int)o, MSG_DONTWAIT);
        if (sent < 0 && errno != EWOULDBLOCK && errno != EAGAIN) return -1;
    }
    return 0;
}

static void rbuf_write(const char *data, size_t len) {
    if (!data || len == 0) return;
    portENTER_CRITICAL(&s_rspin);
    size_t off = 0;
    while (off < len) {
        size_t chunk = LOG_RBUF_SZ - s_rhead;
        if (chunk > (len - off)) chunk = len - off;
        memcpy(s_rbuf + s_rhead, data + off, chunk);
        s_rhead = (s_rhead + chunk) % LOG_RBUF_SZ;
        if (s_rhead == 0) s_rfull = true;
        off += chunk;
    }
    portEXIT_CRITICAL(&s_rspin);
}

static size_t rbuf_snapshot(char *out, size_t out_sz) {
    if (!out || out_sz == 0) return 0;
    portENTER_CRITICAL(&s_rspin);
    size_t total = s_rfull ? LOG_RBUF_SZ : s_rhead;
    if (total > out_sz) total = out_sz;
    size_t start = s_rfull ? s_rhead : 0;
    for (size_t i = 0; i < total; ++i) {
        out[i] = s_rbuf[(start + i) % LOG_RBUF_SZ];
    }
    // “esvazia” a janela copiada (modelo snapshot + drain)
    s_rhead = 0;
    s_rfull = false;
    portEXIT_CRITICAL(&s_rspin);
    return total;
}
/* ======================================================= */

/* ====================== TCP server ===================== */
static TaskHandle_t s_srv_task   = NULL;
static int          s_listen_fd  = -1;   // socket em listen()
static int          s_client_fd  = -1;   // socket do cliente aceito
static uint16_t     s_port       = 3333;

// fecha cliente se houver muitos EAGAIN/EWOULDBLOCK consecutivos
static uint16_t     s_again_strikes = 0;
#define AGAIN_STRIKES_LIMIT 200     // ~200 ciclos de envio (depende do volume)

static void close_client(void) {
    if (s_client_fd >= 0) {
        lwip_shutdown(s_client_fd, SHUT_RDWR);
        lwip_close(s_client_fd);
        s_client_fd = -1;
    }
    s_again_strikes = 0;
}

void tcp_log_force_close_client(void) {
    if (s_client_fd >= 0) {
        int fd = s_client_fd;
        s_client_fd = -1; // invalida primeiro
        lwip_shutdown(fd, SHUT_RDWR);
        lwip_close(fd);
    }
    s_again_strikes = 0;
}

bool tcp_log_is_listening(void) { return s_listen_fd >= 0; }
bool tcp_log_has_client(void)   { return s_client_fd >= 0; }

bool tcp_log_port_in_use(int port)
{
    int fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (fd < 0) return false;

    int yes = 1;
    lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = lwip_htonl(INADDR_ANY);
    addr.sin_port        = lwip_htons((uint16_t)port);

    bool in_use = false;
    if (lwip_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno == EADDRINUSE) in_use = true;
    }
    lwip_close(fd);
    return in_use;
}

/* =============== Writer vprintf para log mux =============== */
int tcp_log_vprintf(const char *fmt, va_list ap)
{
    // formata uma única vez em buffer local
    char local[512];
    va_list ap2; va_copy(ap2, ap);
    int n = vsnprintf(local, sizeof(local), fmt, ap2);
    va_end(ap2);
    if (n <= 0) return n;
    if (n >= (int)sizeof(local)) n = (int)sizeof(local) - 1;
    local[n] = '\0';

    // grava no ring buffer para permitir dump no connect
    rbuf_write(local, (size_t)n);

    // tenta também enviar ao cliente (não bloqueante)
    if (s_client_fd >= 0) {
 //       int sent = lwip_send(s_client_fd, local, n, MSG_DONTWAIT);
          int sent = (s_client_fd >= 0) ? tcp_send_norm(s_client_fd, local, (size_t)n) : 0;
        if (sent < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                if (++s_again_strikes > AGAIN_STRIKES_LIMIT) {
                    ESP_LOGW(TAG, "Cliente lento — fechando conexão.");
                    close_client();
                }
            } else {
                close_client();
            }
        } else {
            s_again_strikes = 0;
        }
    }
    return n;
}

/* ====================== Task do servidor ====================== */
static void tcp_log_server_task(void *arg)
{
    (void)arg;

    for (;;) {
        // 1) cria socket e entra em listen
        s_listen_fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
        if (s_listen_fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int opt = 1;
        lwip_setsockopt(s_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr = {0};
        addr.sin_family      = AF_INET;
        addr.sin_port        = lwip_htons(s_port);
        addr.sin_addr.s_addr = lwip_htonl(INADDR_ANY);

        if (lwip_bind(s_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            lwip_close(s_listen_fd); s_listen_fd = -1;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (lwip_listen(s_listen_fd, 1) < 0) {
            lwip_close(s_listen_fd); s_listen_fd = -1;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ESP_LOGI(TAG, "Waiting TCP on port %u...", s_port);

        // 2) aceita um cliente (modelo single-client)
        struct sockaddr_in raddr; socklen_t rlen = sizeof(raddr);
        s_client_fd = lwip_accept(s_listen_fd, (struct sockaddr *)&raddr, &rlen);
        if (s_client_fd < 0) {
            lwip_close(s_listen_fd); s_listen_fd = -1;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        s_again_strikes = 0;
        ESP_LOGI(TAG, "Client connected");

        // single-client: fecha o listen enquanto existir cliente
        lwip_close(s_listen_fd); s_listen_fd = -1;

        // non-blocking
        int flags = lwip_fcntl(s_client_fd, F_GETFL, 0);
        lwip_fcntl(s_client_fd, F_SETFL, flags | O_NONBLOCK);

        // keepalive (se disponível)
    #ifdef SO_KEEPALIVE
        int ka = 1; lwip_setsockopt(s_client_fd, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof(ka));
    #endif
    #ifdef TCP_KEEPIDLE
        int idle = 30; lwip_setsockopt(s_client_fd, IPPROTO_TCP, TCP_KEEPIDLE,  &idle, sizeof(idle));
    #endif
    #ifdef TCP_KEEPINTVL
        int intvl = 5; lwip_setsockopt(s_client_fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    #endif
    #ifdef TCP_KEEPCNT
        int cnt = 3; lwip_setsockopt(s_client_fd, IPPROTO_TCP, TCP_KEEPCNT,   &cnt,  sizeof(cnt));
    #endif

// banner + dump inicial
static const char *banner =
    "\r\n=== TCP LOG CONSOLE READY ===\r\n"
    "(non-blocking)\r\n\r\n";
(void)tcp_send_norm(s_client_fd, banner, strlen(banner));

static char dump_buf[LOG_RBUF_SZ];
size_t dn = rbuf_snapshot(dump_buf, sizeof(dump_buf));
if (dn > 0) (void)tcp_send_norm(s_client_fd, dump_buf, dn);

// 3) loop do cliente (drain + detectar desconexão)
while (s_client_fd >= 0) {
    // envia qualquer backlog do ring
    static char out[1024];
    size_t n = rbuf_snapshot(out, sizeof(out));
    if (n > 0) {
        if (tcp_send_norm(s_client_fd, out, n) < 0) {  // <- usa normalização CRLF
            close_client();
            break;
        }
    }

    // detecta desconexão pelo RX
    char sink[64];
    int r = lwip_recv(s_client_fd, sink, sizeof(sink), MSG_DONTWAIT);
    if (r == 0) { // orderly shutdown
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

        // volta ao topo e recria o listen
    }
}

esp_err_t start_tcp_log_server_task(uint16_t port)
{
    s_port = port;
    if (s_srv_task) return ESP_OK;
    BaseType_t ok = xTaskCreate(tcp_log_server_task, "tcp_log_srv", 4096, NULL, 4, &s_srv_task);
    return (ok == pdPASS) ? ESP_OK : ESP_FAIL;
}

void stop_tcp_log_server_task(void)
{
    if (s_srv_task) {
        vTaskDelete(s_srv_task);
        s_srv_task = NULL;
    }
    if (s_listen_fd >= 0) {
        lwip_close(s_listen_fd);
        s_listen_fd = -1;
    }
    close_client();
}

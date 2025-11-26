/*
 * lte_ppp_test.c
 *
 * Teste de PPP LTE usando o serviço ppp_control.
 * - Inicializa o lte_ppp_link (lte_ppp_init)
 * - Inicializa o PPP Control
 * - Registra handler de eventos
 * - Sobe o PPP
 * - Espera conectar
 * - Imprime IP
 * - Faz um HTTP GET simples via sockets
 */

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "ppp_control.h"
#include "lte_ppp_link.h"
#include "system.h"

static const char *TAG = "lte_ppp_test";

//---------------------------------------------------------------------
// Handler de eventos do PPP Control
//---------------------------------------------------------------------
static void ppp_ctrl_event_handler(void *arg,
                                   esp_event_base_t event_base,
                                   int32_t event_id,
                                   void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_data;

    switch ((ppp_ctrl_event_t)event_id) {
    case PPP_CTRL_EVENT_STARTING:
        ESP_LOGI(TAG, "[EVT] PPP STARTING");
        break;

    case PPP_CTRL_EVENT_CONNECTED:
        ESP_LOGI(TAG, "[EVT] PPP CONNECTED");
        break;

    case PPP_CTRL_EVENT_STOPPING:
        ESP_LOGI(TAG, "[EVT] PPP STOPPING");
        break;

    case PPP_CTRL_EVENT_STOPPED:
        ESP_LOGI(TAG, "[EVT] PPP STOPPED");
        break;

    case PPP_CTRL_EVENT_FAILED:
        ESP_LOGE(TAG, "[EVT] PPP FAILED");
        break;

    default:
        ESP_LOGW(TAG, "[EVT] PPP UNKNOWN (%" PRId32 ")", event_id);
        break;
    }
}

//---------------------------------------------------------------------
// Pequena função de HTTP GET via sockets (para validar tráfego PPP)
//---------------------------------------------------------------------
static esp_err_t ppp_test_http_get(const char *host, const char *port)
{
    ESP_LOGI(TAG, "Iniciando teste HTTP GET: %s:%s", host, port);

    struct addrinfo hints = {
        .ai_family   = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;

    int ret = getaddrinfo(host, port, &hints, &res);
    if (ret != 0 || res == NULL) {
        ESP_LOGE(TAG, "getaddrinfo falhou: %d", ret);
        return ESP_FAIL;
    }

    int sock = socket(res->ai_family, res->ai_socktype, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() falhou");
        freeaddrinfo(res);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Conectando ao host...");
    ret = connect(sock, res->ai_addr, res->ai_addrlen);
    if (ret != 0) {
        ESP_LOGE(TAG, "connect() falhou, ret=%d", ret);
        freeaddrinfo(res);
        close(sock);
        return ESP_FAIL;
    }

    freeaddrinfo(res);

    const char *http_get =
        "GET / HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "User-Agent: esp32-ppp-test\r\n"
        "Connection: close\r\n"
        "\r\n";

    ssize_t sent = send(sock, http_get, strlen(http_get), 0);
    if (sent < 0) {
        ESP_LOGE(TAG, "send() falhou");
        close(sock);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "HTTP GET enviado, aguardando resposta...");

    char buf[256];
    ssize_t r = recv(sock, buf, sizeof(buf) - 1, 0);
    if (r < 0) {
        ESP_LOGE(TAG, "recv() falhou");
        close(sock);
        return ESP_FAIL;
    }

    buf[r] = '\0';
    ESP_LOGI(TAG, "Resposta parcial:\n%.*s", (int)r, buf);

    close(sock);
    ESP_LOGI(TAG, "HTTP GET teste concluído com sucesso.");
    return ESP_OK;
}

//---------------------------------------------------------------------
// Task de teste principal
//---------------------------------------------------------------------
static void lte_ppp_test_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "==== INICIO TESTE PPP LTE (via ppp_control) ====");

    // 0) Inicializa o módulo de link PPP (ubxlib + netif PPP)
    //    Aqui assumimos que o SARA já está ligado e registrado na rede.
    esp_err_t err = lte_ppp_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lte_ppp_init() falhou: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    
    if (system_net_core_init() != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao inicializar núcleo de rede");
    vTaskDelete(NULL);
    return;
}

    // 1) Inicializa o serviço PPP Control
    err = ppp_control_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ppp_control_init() falhou: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    // 2) Registra handler de eventos PPP_CTRL_EVENT
    esp_event_handler_instance_t instance;
    err = esp_event_handler_instance_register(
        PPP_CTRL_EVENT,
        ESP_EVENT_ANY_ID,
        ppp_ctrl_event_handler,
        NULL,
        &instance
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao registrar handler de evento PPP_CTRL_EVENT: %s",
                 esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    // Opcional: ajustar política de reconexão para o teste
    ppp_control_set_auto_reconnect(false);
    ppp_control_set_retry_params(2, 5000);

    // 3) Solicita start do PPP
    err = ppp_control_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ppp_control_start() falhou: %s", esp_err_to_name(err));
        esp_event_handler_instance_unregister(PPP_CTRL_EVENT,
                                              ESP_EVENT_ANY_ID,
                                              instance);
        vTaskDelete(NULL);
        return;
    }

    // 4) Aguarda conexão (timeout de ~90s, ajustável)
    int timeout_ms = 90000;
    const int step_ms = 500;

    while (timeout_ms > 0 && !ppp_control_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        timeout_ms -= step_ms;
    }

    if (!ppp_control_is_connected()) {
        ESP_LOGE(TAG, "PPP nao conectou dentro do timeout");
        ppp_control_stop();
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_event_handler_instance_unregister(PPP_CTRL_EVENT,
                                              ESP_EVENT_ANY_ID,
                                              instance);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "PPP reportou CONECTADO. Obtendo IP do netif...");

    // 5) Lê IP do esp_netif associado ao PPP
    esp_netif_t *netif = ppp_control_get_netif();
    if (!netif) {
        ESP_LOGE(TAG, "ppp_control_get_netif() retornou NULL");
    } else {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            ESP_LOGI(TAG, "PPP IP: %d.%d.%d.%d",
                     esp_ip4_addr1(&ip_info.ip),
                     esp_ip4_addr2(&ip_info.ip),
                     esp_ip4_addr3(&ip_info.ip),
                     esp_ip4_addr4(&ip_info.ip));
        } else {
            ESP_LOGE(TAG, "falha ao obter IP do netif PPP");
        }
    }

    // 6) Teste HTTP GET via sockets usando o PPP
    err = ppp_test_http_get("example.com", "80");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Teste HTTP GET falhou");
    } else {
        ESP_LOGI(TAG, "Teste HTTP GET OK (PPP funcionando)");
    }

    // 7) Derruba o PPP ao final do teste (se quiser deixar ligado, comente)
    ESP_LOGI(TAG, "Encerrando PPP apos teste...");
    ppp_control_stop();
    vTaskDelay(pdMS_TO_TICKS(5000));

    // 8) Remove handler e encerra task
    esp_event_handler_instance_unregister(PPP_CTRL_EVENT,
                                          ESP_EVENT_ANY_ID,
                                          instance);

    ESP_LOGI(TAG, "==== FIM TESTE PPP LTE ====");
    vTaskDelete(NULL);
}

//---------------------------------------------------------------------
// Função pública para disparar o teste (chame de fora)
//---------------------------------------------------------------------
void lte_ppp_test_start(void)
{
    xTaskCreate(
        lte_ppp_test_task,
        "lte_ppp_test_task",
        4096,
        NULL,
        4,
        NULL
    );
}

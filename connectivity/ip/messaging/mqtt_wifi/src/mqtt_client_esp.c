#include "mqtt_client_esp.h"

#include <string.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

// Mantido por compatibilidade (não usamos fallback automático de porta aqui)
#ifndef MQTT_WIFI_FALLBACK_PORT1883
#define MQTT_WIFI_FALLBACK_PORT1883 1
#endif

typedef struct {
    esp_mqtt_client_handle_t client;
    SemaphoreHandle_t        ev_mutex;
    SemaphoreHandle_t        ev_connected;
    SemaphoreHandle_t        ev_puback;
    volatile int             last_msg_id;
    volatile bool            connected;
} mqtt_esp_ctx_t;

static const char *TAG = "MQTT/ESP";

// -------------------- EVENTOS --------------------
static void _mqtt_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    mqtt_esp_ctx_t *ctx = (mqtt_esp_ctx_t *)arg;
    esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)event_data;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        ctx->connected = true;
        xSemaphoreGive(ctx->ev_connected);
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        break;

    case MQTT_EVENT_DISCONNECTED:
        ctx->connected = false;
        ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_PUBLISHED:
        if (e && e->msg_id == ctx->last_msg_id) {
            xSemaphoreGive(ctx->ev_puback);
        }
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED msg_id=%d", e ? e->msg_id : -1);
        break;

    case MQTT_EVENT_ERROR: {
        int rc = -1, tls_last = 0, tls_cert = 0;
        if (e && e->error_handle) {
            rc       = e->error_handle->connect_return_code;        // 5 => Not authorized
            tls_last = e->error_handle->esp_tls_last_esp_err;       // erro TLS baixo nível
            tls_cert = e->error_handle->esp_tls_cert_verify_flags;  // flags verificação X.509
        }
        ESP_LOGW(TAG, "MQTT_EVENT_ERROR rc=%d tls_last=0x%x cert_flags=0x%x",
                 rc, tls_last, tls_cert);
        break;
    }

    default:
        break;
    }
}

// -------------------- CONTEXTO --------------------
static mqtt_esp_ctx_t* _ctx_new(void) {
    mqtt_esp_ctx_t *ctx = (mqtt_esp_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->ev_mutex     = xSemaphoreCreateMutex();
    ctx->ev_connected = xSemaphoreCreateBinary();
    ctx->ev_puback    = xSemaphoreCreateBinary();

    if (!ctx->ev_mutex || !ctx->ev_connected || !ctx->ev_puback) {
        if (ctx->ev_mutex)     vSemaphoreDelete(ctx->ev_mutex);
        if (ctx->ev_connected) vSemaphoreDelete(ctx->ev_connected);
        if (ctx->ev_puback)    vSemaphoreDelete(ctx->ev_puback);
        free(ctx);
        return NULL;
    }
    return ctx;
}

static void _ctx_free(mqtt_esp_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->client) {
        esp_mqtt_client_stop(ctx->client);
        esp_mqtt_client_destroy(ctx->client);
        ctx->client = NULL;
    }
    if (ctx->ev_mutex)     vSemaphoreDelete(ctx->ev_mutex);
    if (ctx->ev_connected) vSemaphoreDelete(ctx->ev_connected);
    if (ctx->ev_puback)    vSemaphoreDelete(ctx->ev_puback);
    free(ctx);
}

// -------------------- API --------------------
// Cria o cliente e conecta (retorna handle do contexto em sucesso)
mqtt_esp_handle_t mqtt_client_esp_create_and_connect(const mqtt_conn_cfg_t *cfg, int timeout_ms)
{
    if (!cfg || !cfg->host || !cfg->host[0]) return NULL;

    mqtt_esp_ctx_t *ctx = _ctx_new();
    if (!ctx) return NULL;

    esp_mqtt_client_config_t mc = {0};

    // Endereço & credenciais
    mc.broker.address.hostname  = cfg->host;
    mc.broker.address.port      = cfg->port;
    mc.credentials.client_id    = (cfg->client_id && cfg->client_id[0]) ? cfg->client_id : NULL;
    mc.credentials.username     = cfg->username;
    mc.credentials.authentication.password = cfg->password;

    // Transporte: TCP (1883) ou SSL (8883)
    mc.broker.address.transport = cfg->use_tls ? MQTT_TRANSPORT_OVER_SSL
                                               : MQTT_TRANSPORT_OVER_TCP;

    // TLS (apenas quando SSL)
    if (cfg->use_tls) {
        if (cfg->ca_cert_pem && cfg->ca_cert_pem[0]) {
            // CA específica fornecida pelo front/config (opcional)
            mc.broker.verification.certificate = cfg->ca_cert_pem;
        }
        // Se certificate == NULL e o bundle estiver habilitado no projeto,
        // o esp-tls usará o bundle automaticamente.
    }

    // Session (API nova do IDF 5.x)
    mc.session.keepalive              = cfg->keepalive;
    mc.session.disable_clean_session  = !cfg->clean_session;  // invertido

    // Network/session extras
    mc.network.disable_auto_reconnect = false;

    // Inicializa cliente
    ctx->client = esp_mqtt_client_init(&mc);
    if (!ctx->client) {
        _ctx_free(ctx);
        return NULL;
    }

    // Registra handler com o CONTEXTO (não o client)
    esp_mqtt_client_register_event(ctx->client, ESP_EVENT_ANY_ID, _mqtt_event, ctx);

    // Start e aguarda CONNECTED (via semáforo)
    esp_err_t err = esp_mqtt_client_start(ctx->client);
    if (err != ESP_OK) {
        _ctx_free(ctx);
        return NULL;
    }

    if (timeout_ms <= 0) timeout_ms = 10000;
    if (xSemaphoreTake(ctx->ev_connected, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "Timeout esperando CONNECTED");
        _ctx_free(ctx);
        return NULL;
    }

    ESP_LOGI(TAG, "CONNECTED to broker");
    return (mqtt_esp_handle_t)ctx;
}

esp_err_t mqtt_client_esp_publish(mqtt_esp_handle_t handle,
                                  const char *topic,
                                  const char *payload,
                                  int qos,
                                  bool retain,
                                  int timeout_ms,
                                  int *out_msg_id)
{
    if (!handle || !topic || !payload) return ESP_ERR_INVALID_ARG;
    mqtt_esp_ctx_t *ctx = (mqtt_esp_ctx_t *)handle;

    if (qos != 0) qos = 1; // só 0 ou 1

    xSemaphoreTake(ctx->ev_mutex, portMAX_DELAY);
    ctx->last_msg_id = esp_mqtt_client_publish(ctx->client, topic, payload, 0 /*auto-len*/, qos, retain);
    int msg_id = ctx->last_msg_id;
    xSemaphoreGive(ctx->ev_mutex);

    if (out_msg_id) *out_msg_id = msg_id;

    if (msg_id < 0) {
        ESP_LOGE(TAG, "Falha ao publicar");
        return ESP_FAIL;
    }

    if (qos == 1) {
        if (timeout_ms <= 0) timeout_ms = 10000;
        // Aguarda PUBACK (MQTT_EVENT_PUBLISHED com msg_id correspondente)
        if (xSemaphoreTake(ctx->ev_puback, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
            ESP_LOGE(TAG, "Timeout aguardando PUBACK");
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

void mqtt_client_esp_stop_and_destroy(mqtt_esp_handle_t h) {
    _ctx_free((mqtt_esp_ctx_t *)h);
}

#include "mqtt_client_esp.h"

#include <string.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

// Se quiser que, ao cair de TLS->TCP, a porta 8883 seja trocada para 1883 automaticamente, mantenha 1.
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
        ESP_LOGI(TAG, "CONNECTED to broker");
        break;

    case MQTT_EVENT_DISCONNECTED:
        ctx->connected = false;
        ESP_LOGW(TAG, "DISCONNECTED from broker");
        break;

    case MQTT_EVENT_PUBLISHED:
        if (e && e->msg_id == ctx->last_msg_id) {
            xSemaphoreGive(ctx->ev_puback);
        }
        ESP_LOGI(TAG, "PUBLISHED msg_id=%d", e ? e->msg_id : -1);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT_EVENT_ERROR");
        break;

    default:
        break;
    }
}

// -------------------- CONTEXTO --------------------
static mqtt_esp_ctx_t* _ctx_new(void) {
    mqtt_esp_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->ev_mutex     = xSemaphoreCreateMutex();
    ctx->ev_connected = xSemaphoreCreateBinary();
    ctx->ev_puback    = xSemaphoreCreateBinary();

    if (!ctx->ev_mutex || !ctx->ev_connected || !ctx->ev_puback) {
        if (ctx->ev_mutex) vSemaphoreDelete(ctx->ev_mutex);
        if (ctx->ev_connected) vSemaphoreDelete(ctx->ev_connected);
        if (ctx->ev_puback) vSemaphoreDelete(ctx->ev_puback);
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
    if (ctx->ev_mutex) vSemaphoreDelete(ctx->ev_mutex);
    if (ctx->ev_connected) vSemaphoreDelete(ctx->ev_connected);
    if (ctx->ev_puback) vSemaphoreDelete(ctx->ev_puback);
    free(ctx);
}

// -------------------- API --------------------
mqtt_esp_handle_t mqtt_client_esp_create_and_connect(const mqtt_conn_cfg_t *cfg, int timeout_ms) {
    if (!cfg || !cfg->host || cfg->port <= 0) {
        ESP_LOGE(TAG, "Config inválida (host/port).");
        return NULL;
    }

    mqtt_esp_ctx_t *ctx = _ctx_new();
    if (!ctx) return NULL;

    esp_mqtt_client_config_t mc = {
        .broker.address.hostname = cfg->host,
        .broker.address.port     = cfg->port,
        .credentials.client_id   = (cfg->client_id && cfg->client_id[0]) ? cfg->client_id : "esp32",
        .credentials.username    = (cfg->username && cfg->username[0]) ? cfg->username : NULL,
        .credentials.authentication.password = (cfg->password) ? cfg->password : NULL,
        .session.keepalive       = (cfg->keepalive > 0) ? cfg->keepalive : 60,
        .session.disable_clean_session = cfg->clean_session ? false : true,
        .network.disable_auto_reconnect = false,
    };

    // ---- TLS: decide se ficará efetivamente ativo ----
    bool tls_effective = cfg->use_tls;

    if (tls_effective) {
        if (cfg->ca_cert_pem && cfg->ca_cert_pem[0]) {
            mc.broker.verification.certificate = cfg->ca_cert_pem;
        }
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        else {
            // Usa bundle de CAs públicas embutido no IDF
            mc.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
        }
#else
        else {
            ESP_LOGW(TAG, "TLS solicitado sem CA/BUNDLE. Forçando TCP (sem TLS).");
            tls_effective = false;
        }
#endif
    }

    // *** IMPORTANTE: Definir transporte explicitamente quando usamos hostname/port ***
    mc.broker.address.transport = tls_effective ? MQTT_TRANSPORT_OVER_SSL
                                                : MQTT_TRANSPORT_OVER_TCP;

#if MQTT_WIFI_FALLBACK_PORT1883
    if (!tls_effective && mc.broker.address.port == 8883) {
        ESP_LOGW(TAG, "Porta 8883 sem TLS: alternando para 1883.");
        mc.broker.address.port = 1883;
    }
#endif

    ctx->client = esp_mqtt_client_init(&mc);
    if (!ctx->client) {
        _ctx_free(ctx);
        return NULL;
    }

    esp_mqtt_client_register_event(ctx->client, MQTT_EVENT_ANY, _mqtt_event, ctx);

    if (esp_mqtt_client_start(ctx->client) != ESP_OK) {
        _ctx_free(ctx);
        return NULL;
    }

    if (timeout_ms <= 0) timeout_ms = 10000;
    // Espera pelo CONNECTED (sinalizado no handler)
    if (xSemaphoreTake(ctx->ev_connected, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "Timeout esperando CONNECTED");
        _ctx_free(ctx);
        return NULL;
    }

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

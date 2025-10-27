#include "adaptive_delay.h"
#include "mqtt_publisher.h"
#include "mqtt_client_esp.h"
#include "payload_builder.h"
#include "esp_wifi.h"
#include "esp_log.h"

// header dos índices/SD (ajuste nome se preciso)
#include "sdmmc_driver.h"

static const char *TAG = "MQTT/WIFI";

static adaptive_delay_t s_pub_jitter;  // estado do delay adaptativo (1 por tarefa)

// --- Diag helpers para payload ---
static uint32_t fnv1a32(const char *s) {
    uint32_t h = 2166136261u;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) { h ^= *p++; h *= 16777619u; }
    return h;
}

static void log_payload_preview(const char *topic, const char *json, size_t buf_cap) {
    if (!json) return;
    size_t L = strlen(json);
    ESP_LOGI("MQTT/DBG", "topic='%s' payload_len=%u buf_cap=%u hash32=0x%08x",
             topic ? topic : "", (unsigned)L, (unsigned)buf_cap, (unsigned)fnv1a32(json));
    int head = (L < 240) ? (int)L : 240;
    ESP_LOGD("MQTT/DBG", "payload.head: %.*s", head, json);
    if (L > 240) {
        int tail = (L < 120) ? (int)L : 120;
        const char *end = json + L - tail;
        ESP_LOGD("MQTT/DBG", "payload.tail: %.*s", tail, end);
    }
}

static inline void log_health_snapshot(const char *phase)
{
    size_t heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    UBaseType_t stack = uxTaskGetStackHighWaterMark(NULL);
    wifi_ap_record_t ap;
    int rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) rssi = ap.rssi; // STA conectado
    ESP_LOGI("HEALTH", "%s heap=%uB (~%uKB) stack=%u words rssi=%d dBm",
             phase, (unsigned)heap, (unsigned)(heap/1024), (unsigned)stack, rssi);
}

static void mqtt_publisher_init(void)
{
    // min=150ms, max=600ms, semente=200ms, sal=0..100ms
    // penalidades: heap <90KiB => +15%, <60KiB => +30%
    ad_init(&s_pub_jitter, 150, 600, 200, 100, 90, 60);
}

static void mqtt_publisher_init_once(void) {
    static bool inited = false;
    if (!inited) { mqtt_publisher_init(); inited = true; }
}

static bool wifi_sta_connected(void) {
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
}

// mqtt_tcp.c
esp_err_t mqtt_wifi_publish_now(void)
{
    // 0) STA precisa estar conectada
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        ESP_LOGW("MQTT/WIFI", "STA não conectada; abortando envio MQTT.");
        return ESP_ERR_INVALID_STATE;
    }
       mqtt_publisher_init_once();
       
    // 1) Carrega índices e buffers
    struct record_index_config rec_idx = {0};
    get_index_config(&rec_idx);

    char     topic[192]    = {0};
    char     payload[2048] = {0};
    uint32_t points        = 0;
    uint32_t new_cur       = rec_idx.cursor_position;

    const char *host     = get_mqtt_url();
    const char *topic_ui = get_mqtt_topic();

    // 2) Autodeteção do destino
    bool is_ubidots = false;
    if ((host && strstr(host, "ubidots.com") != NULL) ||
        (topic_ui && strncmp(topic_ui, "/v1.6/devices/", 14) == 0)) {
        is_ubidots = true;
    }

    bool is_weg = false;
    if ((topic_ui && (strncmp(topic_ui, "wnology/", 8) == 0 ||
                      strncmp(topic_ui, "losant/", 7)  == 0)) ||
        (host && (strstr(host, "wnology")   != NULL ||
                  strstr(host, "wegnology") != NULL ||
                  strstr(host, "losant")    != NULL))) {
        is_weg = true;
    }

    // 3) Monta topic/payload a partir do SD
    esp_err_t err = ESP_OK;
    if (is_ubidots) {
        err = mqtt_payload_build_from_sd_ubidots(topic, sizeof(topic),
                                                 payload, sizeof(payload),
                                                 &rec_idx, &points, &new_cur);
      } else if (is_weg) {
         // <<< NOVO: escolhe entre energia x água >>>
             uint8_t mode = get_weg_payload_mode(); // 0=energia (default), 1=água
              if (mode == 1) {
                              err = mqtt_payload_build_from_sd_weg_water(topic, sizeof(topic),
                                                   payload, sizeof(payload),
                                                   &rec_idx, &points, &new_cur);
       } else {
        err = mqtt_payload_build_from_sd_weg_energy(topic, sizeof(topic),
                                                    payload, sizeof(payload),
                                                    &rec_idx, &points, &new_cur);
            }
    } else {
        err = mqtt_payload_build_from_sd(topic, sizeof(topic),
                                         payload, sizeof(payload),
                                         &rec_idx, &points, &new_cur);
    }
    if (err != ESP_OK) {
        ESP_LOGE("MQTT/WIFI", "Falha ao montar payload: %s", esp_err_to_name(err));
        return err;
    }
    if (points == 0) {
        ESP_LOGI("MQTT/WIFI", "Nenhum ponto para enviar.");
        return ESP_OK;
    }
    
    ESP_LOGI("MQTT/WIFI", "topic='%s' points=%u payload_len=%u",
         topic, (unsigned)points, (unsigned)strlen(payload));

// [NOVO] Preview, e checagem de “quase lotado”
log_payload_preview(topic, payload, sizeof(payload));

if (strlen(payload) >= sizeof(payload) - 1) {
    ESP_LOGW("MQTT/DBG", "payload encostou no limite do buffer (%u). Considere aumentar.",
             (unsigned)sizeof(payload));
}

    // 4) Configura conexão MQTT
    int port = (int)get_mqtt_port();
    if (!host || !host[0]) {
        ESP_LOGE("MQTT/WIFI", "Host do broker vazio.");
        return ESP_ERR_INVALID_ARG;
    }
    if (port <= 0) port = 1883; // default

    // **Regra simples**: 8883 => TLS, 1883 => SEM TLS
    bool use_tls = (port == 8883);
    const char *pem = get_mqtt_ca_pem(); // ignorado em 1883

    // Credenciais
    const char *username = NULL;
    const char *password = NULL;
    if (has_network_token_enabled()) {
        username = get_network_token();
        password = "";
    } else {
        if (has_network_user_enabled()) username = get_network_user();
        if (has_network_pw_enabled())   password = get_network_pw();
    }
    // Ubidots aceita senha vazia se usar token como username
    if (host && strstr(host, "ubidots.com") != NULL) {
        if (!password) password = "";
    }

    mqtt_conn_cfg_t cfg = {
        .host          = host,
        .port          = port,
        .use_tls       = use_tls,
        .ca_cert_pem   = (use_tls && pem && pem[0]) ? pem : NULL,  // TLS com PEM se fornecido; sem PEM o wrapper pode usar bundle (ok no 8883)
        .client_id     = get_device_id(),   // ajustado abaixo para WEG
        .username      = username,
        .password      = password,
        .keepalive     = 60,
        .clean_session = true,
    };

    // 4.1) [WEG] Força ClientID = DeviceID extraído do tópico "wnology/<ID>/state"
    if (is_weg) {
        static char weg_client_id[64];
        const char *p  = strchr(topic, '/');           // após "wnology"
        const char *id = p ? p + 1 : NULL;             // início do <ID>
        const char *q  = id ? strchr(id, '/') : NULL;  // fim do <ID>
        if (id && q) {
            size_t n = (size_t)(q - id);
            if (n > 0 && n < sizeof(weg_client_id)) {
                memcpy(weg_client_id, id, n);
                weg_client_id[n] = '\0';
                cfg.client_id = weg_client_id;
            }
        }
    }

    // 4.2) Log de config (não vaza segredos)
    ESP_LOGI("MQTT/CFG",
             "host=%s port=%d tls=%d client_id=%s topic='%s' user_len=%d pw_len=%d",
             cfg.host, cfg.port, cfg.use_tls, cfg.client_id, topic,
             cfg.username ? (int)strlen(cfg.username) : 0,
             cfg.password ? (int)strlen(cfg.password) : 0);

    // 5) Conecta e publica QoS1
    uint64_t t0_us = esp_timer_get_time();
    mqtt_esp_handle_t h = mqtt_client_esp_create_and_connect(&cfg, 10000);
    if (!h) {
        ESP_LOGE("MQTT/WIFI", "Broker MQTT indisponível.");
        return ESP_FAIL;
    }

    err = mqtt_client_esp_publish(h, topic, payload,
                                  /*qos*/1, /*retain*/false,
                                  /*timeout_ms*/10000, NULL);

    mqtt_client_esp_stop_and_destroy(h);

    // 6) Avança índices só se sucesso
    if (err == ESP_OK) {
        if (rec_idx.total_idx > 0) {
            rec_idx.last_read_idx = (rec_idx.last_read_idx + points) % rec_idx.total_idx;
        }
        rec_idx.cursor_position = new_cur;
        save_index_config(&rec_idx);
        ESP_LOGI("MQTT/WIFI", "Envio OK: +%u ponto(s)", (unsigned)points);
    } else {
        ESP_LOGE("MQTT/WIFI", "Falha no publish; índices NÃO avançados.");
    }

    ESP_LOGI("MQTT/TIME", "publish_cost=%llums",
             (unsigned long long)((esp_timer_get_time() - t0_us)/1000ULL));
    return err;
}



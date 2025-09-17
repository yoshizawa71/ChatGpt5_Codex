#include "adaptive_delay.h"
#include "mqtt_publisher.h"
#include "mqtt_client_esp.h"
#include "mqtt_payload_builder.h"
#include "esp_wifi.h"
#include "esp_log.h"

// header dos índices/SD (ajuste nome se preciso)
#include "sdmmc_driver.h"

static const char *TAG = "MQTT/WIFI";

static adaptive_delay_t s_pub_jitter;  // estado do delay adaptativo (1 por tarefa)

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

void mqtt_publisher_init(void)
{
    // min=150ms, max=600ms, semente=200ms, sal=0..100ms
    // penalidades: heap <90KiB => +15%, <60KiB => +30%
    ad_init(&s_pub_jitter, 150, 600, 200, 100, 90, 60);
}

static bool wifi_sta_connected(void) {
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
}

esp_err_t mqtt_wifi_publish_now(void)
{
    if (!wifi_sta_connected()) {
        ESP_LOGW(TAG, "STA não conectada; abortando envio MQTT.");
        return ESP_ERR_INVALID_STATE;
    }

    // === 1) Preparar tópico + payload a partir da TABELA ===
    struct record_index_config rec_idx = {0};
    get_index_config(&rec_idx);

    char topic[192]   = {0};
    char payload[512] = {0};   // Ubidots: 1 ponto por publish; ajuste se precisar
    uint32_t points   = 0;
    uint32_t new_cur  = rec_idx.cursor_position;

    // Detecta Ubidots pelo host ou pelo tópico do front
    const char *host     = get_mqtt_url();
    const char *topic_ui = get_mqtt_topic();
    bool is_ubidots = false;
    if ((host && strstr(host, "ubidots.com") != NULL) ||
        (topic_ui && strncmp(topic_ui, "/v1.6/devices/", 14) == 0)) {
        is_ubidots = true;
    }

    esp_err_t err;
    if (is_ubidots) {
        err = mqtt_payload_build_from_sd_ubidots(topic, sizeof(topic),
                                                 payload, sizeof(payload),
                                                 &rec_idx, &points, &new_cur);
    } else {
        err = mqtt_payload_build_from_sd(topic, sizeof(topic),
                                         payload, sizeof(payload),
                                         &rec_idx, &points, &new_cur);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao montar payload: %s", esp_err_to_name(err));
        return err;
    }
    if (points == 0) {
        ESP_LOGI(TAG, "Nenhum ponto para enviar.");
        return ESP_OK;
    }

    // === 2) Conexão (com TLS opcional por porta/CA) ===
    int port = (int)get_mqtt_port();
    if (!host || !host[0]) {
        ESP_LOGE(TAG, "Host do broker vazio.");
        return ESP_ERR_INVALID_ARG;
    }
    if (port <= 0) port = 1883;

    bool use_tls = (port == 8883);
    const char *pem = get_mqtt_ca_pem();
    if (use_tls && (!pem || !pem[0])) { use_tls = false; }

    // auth: prioriza token
    const char *username = NULL;
    const char *password = NULL;
    if (has_network_token_enabled()) {
        username = get_network_token();
        password = "";
    } else {
        if (has_network_user_enabled()) username = get_network_user();
        if (has_network_pw_enabled())   password = get_network_pw();
    }

    int ulen = username ? (int)strlen(username) : 0;
    ESP_LOGI("MQTT/AUTH", "username len=%d, first4=%.4s last4=%s",
             ulen, username ? username : "", (ulen >= 4 ? username + (ulen - 4) : ""));

    // Se detectar Ubidots, força password vazio (ou "x") por garantia
    is_ubidots = (host && strstr(host, "ubidots.com") != NULL);
    if (is_ubidots && (!password || !password[0])) {
        password = ""; // ou "x"
    }

    // --- logs úteis (sem vazar o token inteiro) ---
    topic_ui = get_mqtt_topic();
    ESP_LOGI(TAG, "MQTT cfg: host=%s port=%d tls=%s",
             host, port, use_tls ? "yes" : "no");
    if (username && username[0]) {
        size_t ul = strlen(username);
        ESP_LOGI(TAG, "MQTT auth: username=%.*s*** (len=%u), password=%s",
                 (int)((ul > 4) ? 4 : ul), username, (unsigned)ul,
                 (password && password[0]) ? "<set>" : "<empty>");
    } else {
        ESP_LOGW(TAG, "MQTT auth: sem username configurado!");
    }
    ESP_LOGI(TAG, "MQTT topic: %s", topic_ui ? topic_ui : "<vazio>");

    // === 3) Atraso adaptativo CURTO (antes de conectar/publicar) ===
    // lazy-init local para não depender de ordem de inicialização externa
    static adaptive_delay_t s_pub_jitter;
    static bool s_pub_jitter_inited = false;
    if (!s_pub_jitter_inited) {
        // min=150ms, max=600ms, semente=200ms, sal=0..100ms
        // penalidades de heap: <90KiB (+15%), <60KiB (+30%)
        ad_init(&s_pub_jitter, 150, 600, 200, 100, 90, 60);
        s_pub_jitter_inited = true;
    }

    uint32_t jitter_ms = ad_before_work(&s_pub_jitter, /*t0_us_out*/NULL); // ignoramos t0 aqui
    ESP_LOGD(TAG, "MQTT jitter adaptativo: +%u ms (avg=%ums)",
             (unsigned)jitter_ms, (unsigned)s_pub_jitter.ewma_cost_ms);
    vTaskDelay(pdMS_TO_TICKS(jitter_ms));

log_health_snapshot("before_publish");
    // mede custo SOMENTE do trabalho de publish/conectar (exclui o jitter)
    uint64_t t0_work_us = esp_timer_get_time();

    // === 4) Conectar e publicar (QoS1) ===
    mqtt_conn_cfg_t cfg = {
        .host          = host,
        .port          = port,
        .use_tls       = use_tls,
        .ca_cert_pem   = use_tls ? pem : NULL,
        .client_id     = get_device_id(),
        .username      = username,
        .password      = password,
        .keepalive     = 60,
        .clean_session = true,
    };

    mqtt_esp_handle_t h = mqtt_client_esp_create_and_connect(&cfg, 10000);
    if (!h) {
        // atualiza a métrica mesmo em falha de conexão
        ad_after_work(&s_pub_jitter, t0_work_us);
        ESP_LOGE(TAG, "Broker MQTT indisponível.");
        return ESP_FAIL;
    }

    err = mqtt_client_esp_publish(h, topic, payload,
                                  /*qos*/1, /*retain*/false,
                                  /*timeout_ms*/10000, NULL);

    mqtt_client_esp_stop_and_destroy(h);

    // === 5) Atualizar índices APENAS se sucesso ===
    if (err == ESP_OK) {
        if (rec_idx.total_idx > 0) {
            rec_idx.last_read_idx = (rec_idx.last_read_idx + points) % rec_idx.total_idx;
        }
        rec_idx.cursor_position = new_cur;
        save_index_config(&rec_idx);
        ESP_LOGI(TAG, "Envio OK: avançou %u ponto(s), last_read_idx=%u, cursor=%u",
                 (unsigned)points, (unsigned)rec_idx.last_read_idx, (unsigned)rec_idx.cursor_position);
    } else {
        ESP_LOGE(TAG, "Falha no envio; índices NÃO foram avançados.");
    }

    // === 6) Alimenta o EWMA com o custo do publish/conexão ===
    ad_after_work(&s_pub_jitter, t0_work_us);
    
    uint64_t dt_ms = (esp_timer_get_time() - t0_work_us) / 1000ULL;
ESP_LOGI("MQTT/TIME", "publish_cost=%llums", (unsigned long long)dt_ms);
log_health_snapshot("after_publish");

    return err;
}


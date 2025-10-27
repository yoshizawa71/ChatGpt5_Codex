/*
 * http_publisher.c
 *
 *  Created on: 7 de out. de 2025
 *      Author: geopo
 */

#include "http_publisher.h"
#include "http_client_esp.h"
#include "payload_builder.h"   // <— reaproveitamos os MESMOS builders de payload
#include "sdmmc_driver.h"          // índices/cursor
#include "esp_wifi.h"
#include "esp_log.h"
#include <string.h>
#include "http_wifi_cfg.h"

#include "adaptive_delay.h"         // mesmo jitter usado no mqtt_wifi (opcional)

static const char *TAG = "HTTP/WIFI";
static adaptive_delay_t s_pub_jitter;

// ---------- Getters fracos (podem ser substituídos pelo seu front/NVS) ----------
__attribute__((weak)) const char *get_http_url(void)    { return NULL; } // preencha no front
__attribute__((weak)) const char *get_http_ca_pem(void) { return NULL; }
__attribute__((weak)) int http_payload_is_ubidots(void) { return 0; }
__attribute__((weak)) int http_payload_is_weg(void)     { return 0; }

// ---------- Aux de log (igual ao mqtt) ----------
static uint32_t fnv1a32(const char *s) {
    uint32_t h = 2166136261u;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) { h ^= *p++; h *= 16777619u; }
    return h;
}
static void log_payload_preview(const char *where, const char *json, size_t buf_cap) {
    if (!json) return;
    size_t L = strlen(json);
    ESP_LOGI("HTTP/DBG", "%s payload_len=%u buf_cap=%u hash32=0x%08x",
             where ? where : "", (unsigned)L, (unsigned)buf_cap, (unsigned)fnv1a32(json));
    int head = (L < 240) ? (int)L : 240;
    ESP_LOGD("HTTP/DBG", "payload.head: %.*s", head, json);
    if (L > 240) {
        int tail = (L < 120) ? (int)L : 120;
        const char *end = json + L - tail;
        ESP_LOGD("HTTP/DBG", "payload.tail: %.*s", tail, end);
    }
}

// ---------- API ----------
void http_publisher_init(void) {
    // Mesmo perfil do MQTT: min=150ms, max=600ms, seed=200ms, salt=100ms, penalidades 90/60KiB
    ad_init(&s_pub_jitter, 150, 600, 200, 100, 90, 60);
}

static bool wifi_sta_connected(void) {
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
}

esp_err_t http_wifi_publish_now(void)
{
    // 0) STA precisa estar conectada
    if (!wifi_sta_connected()) {
        ESP_LOGW(TAG, "STA não conectada; abortando envio HTTP.");
        return ESP_ERR_INVALID_STATE;
    }

    // 1) Carrega índices/SD
    struct record_index_config rec_idx = {0};
    get_index_config(&rec_idx);

    char     topic[192]    = {0};   // alguns backends HTTP querem um "topic" no corpo
    char     payload[2048] = {0};
    uint32_t points        = 0;
    uint32_t new_cur       = rec_idx.cursor_position;

    // 2) Escolhe o builder (mesmas heurísticas do MQTT)
    esp_err_t err = ESP_OK;
    if (http_payload_is_ubidots()) {
        err = mqtt_payload_build_from_sd_ubidots(topic, sizeof(topic),
                                                 payload, sizeof(payload),
                                                 &rec_idx, &points, &new_cur);
    } else if (http_payload_is_weg()) {
        err = mqtt_payload_build_from_sd_weg_energy(topic, sizeof(topic),
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

    log_payload_preview("build", payload, sizeof(payload));

    // 3) Monta URL a partir do front/NVS
    const char *host   = http_cfg_host();
    uint16_t    port   = http_cfg_port();
    const char *path   = http_cfg_path();
    const char *ca_pem = http_cfg_ca_pem();

    if (!host || !*host) {
        ESP_LOGE(TAG, "Host HTTP vazio. Configure via front/NVS.");
        return ESP_ERR_INVALID_ARG;
    }

    // garante a barra no path
    char path_fix[160];
    if (path && *path) {
        if (path[0] == '/') snprintf(path_fix, sizeof(path_fix), "%s", path);
        else                 snprintf(path_fix, sizeof(path_fix), "/%s", path);
    } else {
        strcpy(path_fix, "/");
    }

    char url[256];
    snprintf(url, sizeof(url), "%s://%s:%u%s",
             (ca_pem ? "https" : "http"),
             host, (unsigned)port, path_fix);
    ESP_LOGI(TAG, "HTTP URL: %s", url);

    // Auth (Bearer ou Basic), reaproveitando suas flags
    const char *bearer     = http_cfg_has_bearer() ? http_cfg_bearer()     : NULL;
    const char *basic_user = http_cfg_has_basic()  ? http_cfg_basic_user() : NULL;
    const char *basic_pass = http_cfg_has_basic()  ? http_cfg_basic_pass() : NULL;

    http_conn_cfg_t hc = {
        .url          = url,
        .ca_cert_pem  = ca_pem,
        .timeout_ms   = 10000,
        .auth_bearer  = bearer,
        .basic_user   = basic_user,
        .basic_pass   = basic_pass,
        .content_type = "application/json",
    };

    // 4) Publica (POST JSON)
    uint64_t t0 = esp_timer_get_time();
    int status = -1;
    err = http_client_esp_post_json(&hc, payload, &status);

    if (err == ESP_OK) {
        // 5) Avança índices igual ao MQTT
        if (rec_idx.total_idx > 0) {
            rec_idx.last_read_idx = (rec_idx.last_read_idx + points) % rec_idx.total_idx;
        }
        rec_idx.cursor_position = new_cur;
        save_index_config(&rec_idx);
        ESP_LOGI(TAG, "HTTP OK (status=%d): +%u ponto(s)", status, (unsigned)points);
    } else {
        ESP_LOGE(TAG, "HTTP falhou (status=%d). Índices NÃO avançados.", status);
    }

    ESP_LOGI("HTTP/TIME", "post_cost=%llums",
             (unsigned long long)((esp_timer_get_time() - t0)/1000ULL));

    return err;
}





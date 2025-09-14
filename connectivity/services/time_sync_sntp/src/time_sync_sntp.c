#include "time_sync_sntp.h"
#include <time.h>
#include "esp_log.h"
#include "esp_idf_version.h"
#include "esp_netif_sntp.h"

static const char *TAG = "TIME_SYNC";

// 2025-01-01 00:00:00 UTC
#define MIN_VALID_EPOCH 1735689600

static esp_err_t s_sync_do(const char *s0, const char *s1, const char *s2, uint32_t timeout_ms)
{
    // Use 1 servidor estático (o pool já resolve para vários IPs); se quiser, troque para s0/s1/s2
    const char *d0 = (s0 && s0[0]) ? s0 : "pool.ntp.org";

    // Config padrão para 1 servidor
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(d0);
    // Opcional: cfg.smooth_sync = true;  // ajuste suave de clock
    // Opcional: cfg.sync_cb = NULL;       // callback após sync

    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "sntp_init: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_netif_sntp_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "sntp_start: %s", esp_err_to_name(err));
        esp_netif_sntp_deinit();
        return err;
    }

    // Aguarda primeira sincronização (bloqueia até timeout)
    err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms));

    // Encerramos direto; o deinit já garante stop interno quando preciso
    esp_netif_sntp_deinit();

    // Valida epoch
    time_t now = 0; 
    time(&now);
    if (now < MIN_VALID_EPOCH) {
        ESP_LOGW(TAG, "Epoch ainda inválido (%ld) após SNTP", (long)now);
        return (err == ESP_OK) ? ESP_FAIL : err;
    }

    struct tm tm_now; 
    localtime_r(&now, &tm_now);
    ESP_LOGI(TAG, "Hora: %04d-%02d-%02d %02d:%02d:%02d",
             tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
             tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
    return ESP_OK;

}

esp_err_t time_sync_sntp_now(uint32_t timeout_ms)
{
    return s_sync_do(NULL, NULL, NULL, timeout_ms);
}

esp_err_t time_sync_sntp_with_servers(const char *const *servers, int num_servers, uint32_t timeout_ms)
{
    const char *s0 = (num_servers > 0 && servers) ? servers[0] : NULL;
    const char *s1 = (num_servers > 1 && servers) ? servers[1] : NULL;
    const char *s2 = (num_servers > 2 && servers) ? servers[2] : NULL;
    return s_sync_do(s0, s1, s2, timeout_ms);
}

void time_sync_set_timezone(const char *tz_string)
{
    if (!tz_string || !tz_string[0]) return;
    setenv("TZ", tz_string, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone: %s", tz_string);
}

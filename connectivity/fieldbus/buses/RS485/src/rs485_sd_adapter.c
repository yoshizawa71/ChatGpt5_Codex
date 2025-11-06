#include "sdmmc_driver.h"
#include "esp_err.h"
#include "esp_log.h"
#include "rs485_sd_adapter.h"

#include <ctype.h>
#include <stdio.h>

static inline esp_err_t legacy_save_record_sd_rs485(int channel, int subindex, const char *value_str)
{
    return save_record_sd_rs485(channel, subindex, value_str);
}

static const char *TAG = "RS485_SD_ADAPTER";

esp_err_t rs485_sd_adapter_save_record(const char *key, float value)
{
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *cursor = key;
    while (*cursor && isspace((unsigned char)*cursor)) {
        ++cursor;
    }

    int channel = 0;
    int subindex = 0;
    int consumed = 0;
    if (sscanf(cursor, "%d.%d%n", &channel, &subindex, &consumed) == 2) {
        // ok
    } else if (sscanf(cursor, "%d%n", &channel, &consumed) == 1) {
        subindex = 0;
    } else {
        ESP_LOGW(TAG, "Chave inválida para gravação no SD: '%s'", key);
        return ESP_ERR_INVALID_ARG;
    }

    const char *tail = cursor + consumed;
    while (*tail) {
        if (!isspace((unsigned char)*tail)) {
            ESP_LOGW(TAG, "Sufixo inválido após chave '%s'", key);
            return ESP_ERR_INVALID_ARG;
        }
        ++tail;
    }

    char value_str[32];
    int len = snprintf(value_str, sizeof(value_str), "%.3f", (double)value);
    if (len <= 0 || len >= (int)sizeof(value_str)) {
        ESP_LOGW(TAG, "Falha ao formatar valor '%f'", (double)value);
        return ESP_FAIL;
    }

    esp_err_t err = legacy_save_record_sd_rs485(channel, subindex, value_str);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao gravar no SD (key=%s ch=%d sub=%d)", key, channel, subindex);
        return err;
    }

    // TODO(Codex): Fase 2 — integrar com drivers específicos por type/subtype antes da gravação.
    return ESP_OK;
}

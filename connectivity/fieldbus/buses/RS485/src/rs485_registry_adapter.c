#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "datalogger_driver.h"   // sensor_map_t, RS485_MAX_SENSORS, load_rs485_config()

#ifndef RS485_REG_GLUE_VERBOSE
#define RS485_REG_GLUE_VERBOSE 1  // coloque 0 para silenciar o dump
#endif

int rs485_registry_adapter_link_anchor = 0;

static const char *TAG = "RS485_REG_GLUE";

static bool streq_ci(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) { if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return false; }
    return true;
}
static bool is_energy(const char *s) {
    return s && *s && streq_ci(s, "energia");
}
static int subtype_to_phases(const char *s) {
    if (!s || !*s) return 3;
    if (streq_ci(s, "monofasico") || streq_ci(s, "monofásico")) return 1;
    if (streq_ci(s, "trifasico")  || streq_ci(s, "trifásico"))  return 3;
    return 3;
}

bool rs485_registry_get_channel_addr(uint8_t channel, uint8_t *out_addr)
{
    sensor_map_t map[RS485_MAX_SENSORS] = {0}; size_t count = 0;
    if (load_rs485_config(map, &count) != ESP_OK) { ESP_LOGW(TAG, "load falhou"); return false; }
    for (size_t i = 0; i < count; ++i) {
        if (map[i].channel == channel && is_energy(map[i].type)) {
            if (out_addr) *out_addr = map[i].address;
            ESP_LOGI(TAG, "get_addr: ch=%u → addr=%u", channel, map[i].address);
            return true;
        }
    }
    ESP_LOGW(TAG, "get_addr: ch=%u não encontrado", channel);
    return false;
}

int rs485_registry_get_channel_phase_count(uint8_t channel)
{
    sensor_map_t map[RS485_MAX_SENSORS] = {0}; size_t count = 0;
    if (load_rs485_config(map, &count) != ESP_OK) return 0;
    for (size_t i = 0; i < count; ++i) {
        if (map[i].channel == channel && is_energy(map[i].type)) {
            int p = subtype_to_phases(map[i].subtype);
            ESP_LOGI(TAG, "get_phases: ch=%u → %d", channel, p);
            return p;
        }
    }
    ESP_LOGW(TAG, "get_phases: ch=%u não encontrado", channel);
    return 0;
}

int rs485_registry_iterate_configured(bool (*cb)(uint8_t, uint8_t, void*), void *user)
{
    if (!cb) return 0;

    sensor_map_t map[RS485_MAX_SENSORS] = {0};
    size_t count = 0;
    esp_err_t lr = load_rs485_config(map, &count);

    // >>>>> É AQUI: logs logo após load_rs485_config <<<<<
    ESP_LOGI(TAG, "load_rs485_config: %s, count=%u",
             (lr == ESP_OK ? "OK" : "ERRO"), (unsigned)count);

#if RS485_REG_GLUE_VERBOSE
    for (size_t i = 0; i < count; ++i) {
        ESP_LOGI(TAG, "[%u] ch=%u addr=%u type='%s' subtype='%s'",
                 (unsigned)i, map[i].channel, map[i].address,
                 map[i].type, map[i].subtype);
    }
#endif

    if (lr != ESP_OK) {
        ESP_LOGW(TAG, "iterate: load falhou");
        return 0;
    }

    int n = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!is_energy(map[i].type)) continue;  // só itens de energia
        ESP_LOGI(TAG, "iterate: ch=%u addr=%u subtype='%s'",
                 map[i].channel, map[i].address, map[i].subtype);
        if (cb(map[i].channel, map[i].address, user)) n++;
    }

    if (n == 0) {
        ESP_LOGW(TAG, "iterate: nenhum item de energia");
    } else {
        ESP_LOGI(TAG, "iterate: %d item(ns) de energia", n);
    }
    return n;
}
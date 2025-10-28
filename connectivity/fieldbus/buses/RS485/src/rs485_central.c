#include "rs485_central.h"

#include <stdio.h>

#include "datalogger_driver.h"
#include "energy_meter.h"
#include "esp_err.h"
#include "esp_log.h"
#include "rs485_registry.h"
#include "rs485_registry_adapter.h"
#include "rs485_sd_adapter.h"

static const char *TAG = "RS485_CENTRAL";

#ifndef RS485_CENTRAL_FIX_CHANNEL
#define RS485_CENTRAL_FIX_CHANNEL   3
#endif
#ifndef RS485_CENTRAL_FIX_ADDRESS
#define RS485_CENTRAL_FIX_ADDRESS   1
#endif

void rs485_central_poll_and_save(uint32_t timeout_ms)
{
    (void)timeout_ms;

    esp_err_t err = energy_meter_save_currents(RS485_CENTRAL_FIX_CHANNEL,
                                               RS485_CENTRAL_FIX_ADDRESS);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Central: lido 1 sensor energia; gravado %d", 3);
    } else {
        ESP_LOGW(TAG, "Central: falha ao gravar energia (ch=%d addr=%d): %s",
                 RS485_CENTRAL_FIX_CHANNEL, RS485_CENTRAL_FIX_ADDRESS, esp_err_to_name(err));
    }

    rs485_sensor_t sensors[RS485_MAX_SENSORS] = {0};
    int total = rs485_registry_get_snapshot(sensors, RS485_MAX_SENSORS);
    if (total <= 0) {
        ESP_LOGW(TAG, "Central: snapshot vazio ou erro (%d)", total);
        ESP_LOGI(TAG, "Central: T/H processados=%d; gravado %d", 0, 0);
        return;
    }

    rs485_sensor_t filtered[RS485_MAX_SENSORS] = {0};
    size_t filtered_count = 0;
    for (int i = 0; i < total && filtered_count < RS485_MAX_SENSORS; ++i) {
        rs485_type_t type = sensors[i].type;
        if (type == RS485_TYPE_TERMOHIGRO ||
            type == RS485_TYPE_TEMPERATURA ||
            type == RS485_TYPE_UMIDADE) {
            filtered[filtered_count++] = sensors[i];
        }
    }

    if (filtered_count == 0) {
        ESP_LOGI(TAG, "Central: T/H processados=%d; gravado %d", 0, 0);
        return;
    }

    rs485_measurement_t measurements[RS485_MAX_SENSORS * 2] = {0};
    int got = rs485_poll_all(filtered, filtered_count,
                             measurements, sizeof(measurements) / sizeof(measurements[0]));
    if (got < 0) {
        ESP_LOGW(TAG, "Central: falha ao ler T/H (ret=%d)", got);
        ESP_LOGI(TAG, "Central: T/H processados=%d; gravado %d", 0, 0);
        return;
    }

    int saved = 0;
    for (int i = 0; i < got; ++i) {
        const rs485_measurement_t *m = &measurements[i];
        int sub = 0;
        switch (m->kind) {
            case RS485_MEAS_TEMP_C:
                sub = 1;
                break;
            case RS485_MEAS_HUM_PCT:
                sub = 2;
                break;
            default:
                break;
        }
        if (sub == 0) {
            continue;
        }

        char key[12];
        int len = snprintf(key, sizeof(key), "%u.%d", (unsigned)m->channel, sub);
        if (len <= 0 || len >= (int)sizeof(key)) {
            ESP_LOGW(TAG, "Central: chave inválida ao gravar T/H (ch=%u sub=%d)",
                     (unsigned)m->channel, sub);
            continue;
        }

        if (rs485_sd_adapter_save_record(key, m->value) == ESP_OK) {
            saved++;
        } else {
            ESP_LOGW(TAG, "Central: falha ao gravar T/H (key=%s)", key);
        }
    }

    ESP_LOGI(TAG, "Central: T/H processados=%d; gravado %d", got, saved);
}

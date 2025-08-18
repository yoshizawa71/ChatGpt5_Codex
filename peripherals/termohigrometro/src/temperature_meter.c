/*
 * temperature_meter.c
 *
 *  Created on: 8 de ago. de 2025
 *      Author: geopo
 */

#include "../../termohigrometro/include/temperature_meter.h"

#include "datalogger_driver.h"      // load_rs485_config(), sensor_map_t
#include "sdmmc_driver.h"           // save_record_sd(int channel, char *data)
#include "esp_log.h"
#include <stdio.h>
#include "../../termohigrometro/include/temperature_rs485.h"      // detecção/leitura temp/umid (driver)

static const char *TAG = "TEMP_METER";

esp_err_t temperature_meter_collect_and_save_once(TickType_t per_sensor_timeout)
{
    sensor_map_t map[10];
    size_t count = 0;

    esp_err_t err = load_rs485_config(map, &count);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "falha ao carregar config RS485 (err=%d)", err);
        return err;
    }
    if (count == 0) {
        ESP_LOGI(TAG, "sem sensores RS485 cadastrados");
        return ESP_OK;
    }

    for (size_t i = 0; i < count; ++i) {
        const int       ch   = (int)map[i].channel;
        const uint8_t   addr = (uint8_t)map[i].address;

        // Se seu sensor_map_t tiver um campo "type", dá pra filtrar aqui:
        // if (map[i].type != SENSOR_TYPE_TEMPERATURE) continue;

        char payload[64];
        esp_err_t e = temperature_rs485_read_payload(addr, payload, sizeof(payload),
                                                     per_sensor_timeout);
        if (e != ESP_OK) {
            // Se quiser ignorar não-temperatura, use:
            // if (e == ESP_ERR_NOT_FOUND || e == ESP_ERR_NOT_SUPPORTED) continue;
            snprintf(payload, sizeof(payload), "ERR=%d", (int)e);
            ESP_LOGW(TAG, "addr=%u falha leitura: %s", addr, esp_err_to_name(e));
        }

        esp_err_t es = save_record_sd(ch, payload);
        if (es != ESP_OK) {
            ESP_LOGE(TAG, "falha ao salvar SD (ch=%d): %s", ch, esp_err_to_name(es));
        }
    }

    return ESP_OK;
}

/*esp_err_t rs485_manager_read_temp_hum(uint8_t address, float *temp_c, float *hum_rh,
                                      TickType_t timeout)
{
    if (!temp_c || !hum_rh) return ESP_ERR_INVALID_ARG;

    rs485_detect_t det;
    esp_err_t err = rs485_manager_detect(address, &det);
    if (err != ESP_OK) return err;

    if (det.kind != RS485_DEV_TEMP_HUM_SIMPLE) return ESP_ERR_NOT_SUPPORTED;

    th_data_t d;
    err = th_simple_read(address, det.base_reg, det.input_regs, det.scale, true, &d, timeout);
    if (err != ESP_OK) return err;

    *temp_c = d.temperature_c;
    *hum_rh = d.humidity_rh;
    return ESP_OK;
}*/


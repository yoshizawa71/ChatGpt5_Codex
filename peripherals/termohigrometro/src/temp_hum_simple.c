/*
 * temp_hum_simple.c
 *
 *  Created on: 8 de ago. de 2025
 *      Author: geopo
 */

#include "temp_hum_simple.h"

#include "modbus_rtu_master.h"
#include "esp_log.h"

static const char *TAG = "TH_SIMPLE";

static inline float scale_val(int16_t v, float scale) {
    return (float)v * scale;
}

esp_err_t th_simple_read(uint8_t unit_id,
                         uint16_t base_reg,
                         bool input_regs,
                         float scale,
                         bool signed_vals,
                         th_data_t *out,
                         TickType_t timeout)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    uint16_t regs[2] = {0};
    esp_err_t err = input_regs
        ? modbus_master_read_input(unit_id, base_reg, 2, regs, timeout)
        : modbus_master_read_holding(unit_id, base_reg, 2, regs, timeout);

    if (err != ESP_OK) return err;

    int16_t t_raw = (int16_t)regs[0];
    int16_t h_raw = (int16_t)regs[1];

    if (!signed_vals) { // raros casos unsigned; normalmente T é signed
        if (t_raw < 0) t_raw = 0;
    }
    out->temperature_c = scale_val(t_raw, scale);
    out->humidity_rh   = scale_val(h_raw, scale);

    // sanity check (ajuste se quiser)
    if (out->temperature_c < -60 || out->temperature_c > 120) return ESP_ERR_INVALID_RESPONSE;
    if (out->humidity_rh   < 0   || out->humidity_rh   > 100) return ESP_ERR_INVALID_RESPONSE;

    return ESP_OK;
}




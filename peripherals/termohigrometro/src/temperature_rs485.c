/*
 * temperature_rs485.c
 *
 *  Created on: 8 de ago. de 2025
 *      Author: geopo
 */

#include "../../termohigrometro/include/temperature_rs485.h"

#include "esp_log.h"
#include <stdio.h>
#include "../../termohigrometro/include/temp_hum_simple.h"      // já criamos antes (FC04/FC03, base, escala)

static const char *TAG = "TEMP_RS485";

static inline bool plausible(float t, float h) {
    return (t > -60 && t < 120) && (h >= 0 && h <= 100);
}

esp_err_t temperature_rs485_detect(uint8_t addr,
                                   temp_rs485_profile_t *out,
                                   TickType_t timeout)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    th_data_t d;
    // caminho “padrão”: Input 0x0001..0x0002 (FC04), escala 0.1
    if (th_simple_read(addr, 0x0001, true, 0.1f, true, &d, timeout) == ESP_OK &&
        plausible(d.temperature_c, d.humidity_rh)) {
        out->base_reg   = 0x0001;
        out->input_regs = true;
        out->scale      = 0.1f;
        out->signed_vals= true;
        return ESP_OK;
    }
    // fallback: Holding 0x0000..0x0001 (FC03), escala 0.1
    if (th_simple_read(addr, 0x0000, false, 0.1f, true, &d, timeout) == ESP_OK &&
        plausible(d.temperature_c, d.humidity_rh)) {
        out->base_reg   = 0x0000;
        out->input_regs = false;
        out->scale      = 0.1f;
        out->signed_vals= true;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t temperature_rs485_read(uint8_t addr,
                                 const temp_rs485_profile_t *prof,
                                 temp_rs485_data_t *out,
                                 TickType_t timeout)
{
    if (!prof || !out) return ESP_ERR_INVALID_ARG;

    th_data_t d;
    esp_err_t e = th_simple_read(addr, prof->base_reg, prof->input_regs,
                                 prof->scale, prof->signed_vals, &d, timeout);
    if (e != ESP_OK) return e;

    out->temperature_c = d.temperature_c;
    out->humidity_rh   = d.humidity_rh;
    return ESP_OK;
}

esp_err_t temperature_rs485_read_payload(uint8_t addr,
                                         char *payload, size_t maxlen,
                                         TickType_t timeout)
{
    if (!payload || !maxlen) return ESP_ERR_INVALID_ARG;

    temp_rs485_profile_t prof;
    esp_err_t e = temperature_rs485_detect(addr, &prof, timeout);
    if (e != ESP_OK) return e;

    temp_rs485_data_t d;
    e = temperature_rs485_read(addr, &prof, &d, timeout);
    if (e != ESP_OK) return e;

    snprintf(payload, maxlen, "TEMP=%.1fC;HUM=%.1f%%", d.temperature_c, d.humidity_rh);
    return ESP_OK;
}




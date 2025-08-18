/*
 * temp_hum_simple.h
 *
 *  Created on: 8 de ago. de 2025
 *      Author: geopo
 */

#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temperature_c;  // °C
    float humidity_rh;    // %RH
} th_data_t;

/**
 * Lê temperatura/umidade em sensores “simples”:
 *  - T em reg base+0, H em base+1
 *  - FC=0x04 (Input) se input_regs=true; senão FC=0x03 (Holding)
 *  - Escala típica 0.1; signed=true permite T negativas
 */
esp_err_t th_simple_read(uint8_t unit_id,
                         uint16_t base_reg,
                         bool input_regs,
                         float scale,
                         bool signed_vals,
                         th_data_t *out,
                         TickType_t timeout);

#ifdef __cplusplus
}
#endif


/*
 * temperature_rs485.h
 *
 *  Created on: 8 de ago. de 2025
 *      Author: geopo
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temperature_c;
    float humidity_rh;
} temp_rs485_data_t;

typedef struct {
    uint16_t base_reg;     // 0x0001 (input) ou 0x0000 (holding)
    bool     input_regs;   // true=FC04, false=FC03
    float    scale;        // tipicamente 0.1
    bool     signed_vals;  // T signed
} temp_rs485_profile_t;

/* Detecta perfil (XY-MD02 e similares) por heurística */
esp_err_t temperature_rs485_detect(uint8_t addr,
                                   temp_rs485_profile_t *out,
                                   TickType_t timeout);

/* Lê usando perfil conhecido */
esp_err_t temperature_rs485_read(uint8_t addr,
                                 const temp_rs485_profile_t *prof,
                                 temp_rs485_data_t *out,
                                 TickType_t timeout);

/* Conveniência: detecta e já retorna payload pronto "TEMP=..;HUM=.." */
esp_err_t temperature_rs485_read_payload(uint8_t addr,
                                         char *payload, size_t maxlen,
                                         TickType_t timeout);

#ifdef __cplusplus
}
#endif

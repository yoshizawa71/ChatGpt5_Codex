/*
 * thermohigrometro.h
 *
 *  Created on: 3 de set. de 2025
 *      Author: geopo
 */

#ifndef CONNECTIVITY_FIELDBUS_DRIVER_XY_MD02_INCLUDE_XY_MD02_DRIVER_H_
#define CONNECTIVITY_FIELDBUS_DRIVER_XY_MD02_INCLUDE_XY_MD02_DRIVER_H_
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Probe simples: tenta FC=0x04 e depois 0x03 em 0x0001.
 * Retorno: 1=ok (used_fc setado p/ 0x04 ou 0x03), 0=sem resposta.
 */
int  temperature_rs485_probe(uint8_t addr, uint8_t *used_fc);

/* Lê temperatura (°C) e, se existir, umidade (%RH).
 * Retorno:
 *   2 -> escreveu temp e hum (has_hum=true)
 *   1 -> escreveu só temp (has_hum=false)
 *  <0 -> erro de comunicação
 */
int  temperature_rs485_read(uint8_t addr, float *temp_c,
                            float *hum_pct, bool *has_hum);


#endif /* CONNECTIVITY_FIELDBUS_DRIVER_XY_MD02_INCLUDE_XY_MD02_DRIVER_H_ */

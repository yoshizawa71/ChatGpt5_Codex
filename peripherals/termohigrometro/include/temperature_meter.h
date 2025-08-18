/*
 * emperature_meter.h
 *
 *  Created on: 8 de ago. de 2025
 *      Author: geopo
 */

#pragma once
#include "freertos/FreeRTOS.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Varrida única: carrega a lista RS485 do LittleFS,
 *  tenta ler sensores de temp/umid (XY-MD02 e similares)
 *  e grava cada amostra no SD via save_record_sd(channel, payload).
 */
esp_err_t temperature_meter_collect_and_save_once(TickType_t per_sensor_timeout);

#ifdef __cplusplus
}
#endif


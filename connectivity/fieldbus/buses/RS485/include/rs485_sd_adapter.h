/*
 * rs485_sd_adapter.h
 *
 *  Created on: 25 de out. de 2025
 *      Author: geopo
 */

#ifndef CONNECTIVITY_FIELDBUS_BUSES_RS485_INCLUDE_RS485_SD_ADAPTER_H_
#define CONNECTIVITY_FIELDBUS_BUSES_RS485_INCLUDE_RS485_SD_ADAPTER_H_

 #pragma once
 #include <stddef.h>
 #include <stdint.h>
 #include "rs485_registry.h"   // rs485_measurement_t
 #include "esp_err.h"

 #ifdef __cplusplus
 extern "C" {
 #endif

 // Se quiser separar HUM em canal+1, mude para 0:
 #ifndef RS485_SD_HUM_SAME_CHANNEL
 #define RS485_SD_HUM_SAME_CHANNEL 1
 #endif

 // Converte um vetor heterogêneo de medições normalizadas para gravações no SD.
 // Retorna o número de linhas gravadas (>0) ou <0 em erro.
 int save_measurements_to_sd(const rs485_measurement_t *m, size_t n);

 #ifdef __cplusplus
 }
 #endif




#endif /* CONNECTIVITY_FIELDBUS_BUSES_RS485_INCLUDE_RS485_SD_ADAPTER_H_ */

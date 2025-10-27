/*
 * rs485_central.h
 *
 *  Created on: 25 de out. de 2025
 *      Author: geopo
 */

#ifndef CONNECTIVITY_FIELDBUS_BUSES_RS485_INCLUDE_RS485_CENTRAL_H_
#define CONNECTIVITY_FIELDBUS_BUSES_RS485_INCLUDE_RS485_CENTRAL_H_
 #pragma once
 #include "esp_err.h"

 #ifdef __cplusplus
 extern "C" {
 #endif

 // Lê todos os sensores cadastrados (qualquer tipo) e grava no SD
 // usando o adaptador (TEMP->.1, HUM->.2; energia mantém o fluxo atual até migrarmos).
 esp_err_t rs485_central_poll_and_save(int timeout_ms);

 #ifdef __cplusplus
 }
 #endif





#endif /* CONNECTIVITY_FIELDBUS_BUSES_RS485_INCLUDE_RS485_CENTRAL_H_ */

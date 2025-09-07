/*
 * modbus_master_api.h
 *
 *  Created on: 6 de set. de 2025
 *      Author: geopo
 */

#ifndef CONNECTIVITY_FIELDBUS_PROTOCOLS_MODBUS_INCLUDE_MODBUS_MASTER_API_H_
#define CONNECTIVITY_FIELDBUS_PROTOCOLS_MODBUS_INCLUDE_MODBUS_MASTER_API_H_
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// Deve ser chamado no seu boot/factory (você já faz isso hoje)
esp_err_t modbus_master_init(void);
void      modbus_master_start_task(void);

// Ping canônico usando a pilha esp-modbus já inicializada.
// Tenta FC=0x04 (reg 0x0001); se falhar, FC=0x03 (reg 0x0000).
// Retorna ESP_OK quando houve resposta; 'alive=true' indica escravo respondeu.
// 'used_fc' recebe 0x04 ou 0x03 quando alive==true; 0x00 caso contrário.
esp_err_t modbus_master_ping(uint8_t slave_addr, bool *alive, uint8_t *used_fc);


#endif /* CONNECTIVITY_FIELDBUS_PROTOCOLS_MODBUS_INCLUDE_MODBUS_MASTER_API_H_ */

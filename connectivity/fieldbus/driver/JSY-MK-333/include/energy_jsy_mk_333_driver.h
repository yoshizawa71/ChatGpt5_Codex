/*
 * energy_jsy_mk_333.h
 *
 *  Created on: 3 de set. de 2025
 *      Author: geopo
 */

#ifndef CONNECTIVITY_FIELDBUS_DRIVER_JSY_MK_333_INCLUDE_ENERGY_JSY_MK_333_DRIVER_H_
#define CONNECTIVITY_FIELDBUS_DRIVER_JSY_MK_333_INCLUDE_ENERGY_JSY_MK_333_DRIVER_H_

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"   // para usar ESP_ERR_INVALID_ARG etc.

/* FC usado pelo equipamento (3=holding, 4=input) */
typedef enum { JSY_FC_AUTO=0, JSY_FC_03=3, JSY_FC_04=4 } jsy_fc_t;

// Registrador de configuração de comunicação:
// high byte = endereço escravo, low byte = baud/formato
#define JSY_REG_COMM_CONFIG   0x0004
#define JSY_ADDR_MIN          1
#define JSY_ADDR_MAX_HARD     247

/* Mapa de registradores/escala (configure conforme o seu aparelho) */
typedef struct {
    jsy_fc_t  fc;              // FC para todas as leituras (use AUTO se desconhecido)
    uint16_t  reg_voltage;     // V (ex.: 0x0000)  escala: /10
    uint16_t  reg_current;     // A (ex.: 0x0001)  escala: /100
    uint16_t  reg_power;       // W (ex.: 0x0002)  escala: /10
    uint16_t  reg_energy;      // kWh (ex.: 0x0003) escala: /100 (depende)
} jsy_map_t;

/* Leitura agrupada (valores em unidades físicas) */
typedef struct {
    float voltage_v;
    float current_a;
    float active_power_w;
    float energy_kwh;
} jsy_values_t;

/* Mapa “placeholder” para começar (AJUSTE depois!)
 * Se o seu JSY usa outros endereços/escala, altere o retorno desta função.
 */
static inline jsy_map_t jsy_mk333_default_map(void) {
    jsy_map_t m = {
        .fc = JSY_FC_AUTO,
        .reg_voltage = 0x0000,
        .reg_current = 0x0001,
        .reg_power   = 0x0002,
        .reg_energy  = 0x0003
    };
    return m;
}

// Registrador de configuração de comunicação do JSY (endereço + baud/paridade)
#define JSY_REG_COMM_CONFIG   0x0004
#define JSY_ADDR_MIN          1
#define JSY_ADDR_MAX_HARD     247

/* Reprograma o endereço Modbus do JSY-MK-333G.
 * Retorna 0 em sucesso; != 0 em falha (normalmente um esp_err_t).
 */
int jsy_mk333_change_address(uint8_t old_addr, uint8_t new_addr);

/*
 * Auto-programa (auto-bind) um único JSY-MK-333 no barramento.
 *
 * - used_addrs/used_count: lista de endereços já cadastrados (serão ignorados no scan).
 * - requested_addr: endereço desejado pelo usuário.
 * - io_final_addr: retorna o endereço efetivo usado após a operação.
 * - out_readdress_done: true se houve mudança de endereço.
 *
 * Retorno:
 *   ESP_OK               -> sucesso (encontrou exatamente 1 JSY e, se necessário, reendereçou)
 *   ESP_ERR_NOT_FOUND    -> nenhum JSY novo encontrado no range de scan
 *   ESP_ERR_INVALID_STATE-> mais de um JSY novo encontrado (conflito)
 *   ESP_ERR_INVALID_ARG  -> parâmetros inválidos
 *   ESP_FAIL / outro     -> falha ao reendereçar ou comunicar
 */
esp_err_t jsy_mk333_auto_program_single(const uint8_t *used_addrs,
                                        size_t         used_count,
                                        int            requested_addr,
                                        int           *io_final_addr,
                                        bool          *out_readdress_done);


/* Tenta detectar o FC e verificar se responde a “algum” registrador do mapa. */
int jsy_mk333_probe(uint8_t addr, jsy_map_t *map, uint8_t *used_fc);

/* Lê os valores principais (usa o mapa informado). Retorna 0 em sucesso. */
int jsy_mk333_read_basic(uint8_t addr, const jsy_map_t *map, jsy_values_t *out);

/* Reprograma o endereço Modbus do JSY-MK-333G.
 *  Retorno: 0 em sucesso; !=0 em falha (normalmente um esp_err_t).
 */
int jsy_mk333_change_address(uint8_t old_addr, uint8_t new_addr);


#endif /* CONNECTIVITY_FIELDBUS_DRIVER_JSY_MK_333_INCLUDE_ENERGY_JSY_MK_333_DRIVER_H_ */

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

/* FC usado pelo equipamento (3=holding, 4=input) */
typedef enum { JSY_FC_AUTO=0, JSY_FC_03=3, JSY_FC_04=4 } jsy_fc_t;

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

/* Tenta detectar o FC e verificar se responde a “algum” registrador do mapa. */
int jsy_mk333_probe(uint8_t addr, jsy_map_t *map, uint8_t *used_fc);

/* Lê os valores principais (usa o mapa informado). Retorna 0 em sucesso. */
int jsy_mk333_read_basic(uint8_t addr, const jsy_map_t *map, jsy_values_t *out);




#endif /* CONNECTIVITY_FIELDBUS_DRIVER_JSY_MK_333_INCLUDE_ENERGY_JSY_MK_333_DRIVER_H_ */

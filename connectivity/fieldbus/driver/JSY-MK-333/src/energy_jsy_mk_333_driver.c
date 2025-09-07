/*
 * energy_jsy_mk_333.c
 *
 *  Created on: 3 de set. de 2025
 *      Author: geopo
 */

#include "energy_jsy_mk_333_driver.h"

#include "modbus_rtu_master.h"
#include "esp_log.h"

static const char *TAG = "JSY_MK333";

/* Helpers p/ ler registrador único c/ FC configurável */
static bool read_one(jsy_fc_t fc, uint8_t addr, uint16_t reg, uint16_t *out)
{
    if (fc == JSY_FC_04) {
        return (modbus_master_read_input_registers(addr, reg, 1, out) == ESP_OK);
    } else {
        return (modbus_master_read_holding_registers(addr, reg, 1, out) == ESP_OK);
    }
}

/* Detecta FC e registra “alguma” vida do equipamento */
int jsy_mk333_probe(uint8_t addr, jsy_map_t *map, uint8_t *used_fc)
{
    if (!map) return -1;

    jsy_fc_t fc_try[2];
    if (map->fc == JSY_FC_AUTO) { fc_try[0] = JSY_FC_04; fc_try[1] = JSY_FC_03; }
    else                         { fc_try[0] = map->fc;  fc_try[1] = map->fc;  }

    uint16_t tmp;
    for (int i = 0; i < 2; ++i) {
        if (read_one(fc_try[i], addr, map->reg_voltage, &tmp) ||
            read_one(fc_try[i], addr, map->reg_current, &tmp) ||
            read_one(fc_try[i], addr, map->reg_power,   &tmp) ||
            read_one(fc_try[i], addr, map->reg_energy,  &tmp)) {
            if (used_fc) *used_fc = (fc_try[i] == JSY_FC_04) ? 0x04 : 0x03;
            map->fc = fc_try[i];
            return 0;
        }
    }
    return -2; // sem resposta
}

int jsy_mk333_read_basic(uint8_t addr, const jsy_map_t *map, jsy_values_t *out)
{
    if (!map || !out) return -1;
    jsy_fc_t fc = (map->fc == JSY_FC_AUTO) ? JSY_FC_04 : map->fc;

    uint16_t u16;
    // Tensao (escala sugestiva /10)
    if (!read_one(fc, addr, map->reg_voltage, &u16)) return -2;
    out->voltage_v = (float)u16 / 10.0f;

    // Corrente (escala sugestiva /100)
    if (!read_one(fc, addr, map->reg_current, &u16)) return -3;
    out->current_a = (float)u16 / 100.0f;

    // Potência ativa (escala sugestiva /10)
    if (!read_one(fc, addr, map->reg_power, &u16))   return -4;
    out->active_power_w = (float)u16 / 10.0f;

    // Energia (escala sugestiva /100)
    if (!read_one(fc, addr, map->reg_energy, &u16))  return -5;
    out->energy_kwh = (float)u16 / 100.0f;

    return 0;
}


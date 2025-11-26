/*
 * energy_jsy_mk_333.c
 *
 *  Created on: 3 de set. de 2025
 *      Author: geopo
 */

#include "energy_jsy_mk_333_driver.h"

#include "modbus_rtu_master.h"
#include "esp_log.h"
#include "esp_err.h"
#include "mbcontroller.h"   // se não estiver sendo incluído por modbus_rtu_master.h

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
	ESP_LOGW("JSY_PROBE", "ENTRY: addr=%u", (unsigned)addr);
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

/*
 * jsy_mk333_change_address()
 *
 * Função utilitária para reprogramar o endereço Modbus do JSY-MK-333G.
 *
 * - Usa o registrador 0x0004 (JSY_REG_COMM_CONFIG), onde:
 *       high byte = endereço escravo
 *       low  byte = configuração de comunicação (baud, formato, etc.)
 *
 * Passos:
 *   1) Lê JSY_REG_COMM_CONFIG via FC03 no endereço antigo (old_addr).
 *   2) Extrai addr_high / cfg_low.
 *   3) Monta new_comm_reg com new_addr no high-byte, preservando cfg_low.
 *   4) Escreve new_comm_reg em JSY_REG_COMM_CONFIG via FC10.
 *   5) Opcionalmente relê o registrador para log/diagnóstico.
 *
 * Retorno:
 *   0           -> sucesso
 *   !=0         -> erro (normalmente um esp_err_t vindo do esp-modbus)
 *
 * OBS IMPORTANTE: o JSY só passa a responder no NOVO endereço depois de um
 * power-cycle físico (desligar/ligar alimentação).
 */
int jsy_mk333_change_address(uint8_t old_addr, uint8_t new_addr)
{
    if (new_addr < JSY_ADDR_MIN || new_addr > JSY_ADDR_MAX_HARD) {
        ESP_LOGE(TAG,
                 "jsy_mk333_change_address: novo endereco %u invalido (permitido: %u..%u)",
                 (unsigned)new_addr, (unsigned)JSY_ADDR_MIN, (unsigned)JSY_ADDR_MAX_HARD);
        return (int)ESP_ERR_INVALID_ARG;
    }

    // 1) Ler reg 0x0004 (COMM CONFIG) no endereco atual
    uint16_t comm_reg = 0;
    {
        mb_param_request_t req = {
            .slave_addr = old_addr,
            .command    = 3,                  // FC03 - Read Holding Registers
            .reg_start  = JSY_REG_COMM_CONFIG,
            .reg_size   = 1
        };

        esp_err_t err = mbc_master_send_request(&req, (void *)&comm_reg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG,
                     "jsy_mk333_change_address: falha ao ler reg 0x%04X no addr=%u (err=%s)",
                     JSY_REG_COMM_CONFIG, (unsigned)old_addr, esp_err_to_name(err));
            return (int)err;
        }
    }

    uint8_t addr_high = (uint8_t)(comm_reg >> 8);
    uint8_t cfg_low   = (uint8_t)(comm_reg & 0x00FF);

    ESP_LOGW(TAG,
             "jsy_mk333_change_address: COMM_PARAM atual reg 0x%04X = 0x%04X "
             "(addr_high=%u cfg_low=0x%02X)",
             JSY_REG_COMM_CONFIG, (unsigned)comm_reg,
             (unsigned)addr_high, (unsigned)cfg_low);

    if (addr_high != old_addr) {
        ESP_LOGW(TAG,
                 "jsy_mk333_change_address: addr_high=%u diferente de old_addr=%u "
                 "(seguindo assim mesmo).",
                 (unsigned)addr_high, (unsigned)old_addr);
    }

    if (old_addr == new_addr) {
        ESP_LOGW(TAG,
                 "jsy_mk333_change_address: endereco atual (%u) ja eh igual ao desejado (%u).",
                 (unsigned)old_addr, (unsigned)new_addr);
        return 0;  // nada a fazer
    }

    // 2) Monta novo valor preservando cfg_low
    uint16_t new_comm_reg = ((uint16_t)new_addr << 8) | cfg_low;
    uint16_t tx_buf[1]    = { new_comm_reg };

    ESP_LOGW(TAG,
             "jsy_mk333_change_address: escrevendo reg 0x%04X: 0x%04X (antes 0x%04X) "
             "-> addr %u -> %u (cfg_low=0x%02X)",
             JSY_REG_COMM_CONFIG,
             (unsigned)new_comm_reg, (unsigned)comm_reg,
             (unsigned)old_addr, (unsigned)new_addr, (unsigned)cfg_low);

    // 3) Escreve via FC10 (Write Multiple Registers) tamanho 1
    {
        mb_param_request_t req_wr = {
            .slave_addr = old_addr,
            .command    = 0x10,              // FC10 - Write Multiple Registers
            .reg_start  = JSY_REG_COMM_CONFIG,
            .reg_size   = 1                  // 1 registrador (2 bytes)
        };

        esp_err_t err = mbc_master_send_request(&req_wr, (void *)tx_buf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG,
                     "jsy_mk333_change_address: falha ao escrever reg 0x%04X no addr=%u (err=%s)",
                     JSY_REG_COMM_CONFIG, (unsigned)old_addr, esp_err_to_name(err));
            return (int)err;
        }
    }

    ESP_LOGW(TAG,
             "jsy_mk333_change_address: escrita em reg 0x%04X concluida (novo=0x%04X). "
             "Lendo novamente para diagnostico...",
             JSY_REG_COMM_CONFIG, (unsigned)new_comm_reg);

    // 4) Relê o registrador para conferir (diagnóstico)
    {
        uint16_t comm_after = 0;
        mb_param_request_t req = {
            .slave_addr = old_addr,
            .command    = 3,                  // FC03 - Read Holding Registers
            .reg_start  = JSY_REG_COMM_CONFIG,
            .reg_size   = 1
        };

        esp_err_t err = mbc_master_send_request(&req, (void *)&comm_after);
        if (err == ESP_OK) {
            uint8_t addr_after = (uint8_t)(comm_after >> 8);
            uint8_t cfg_after  = (uint8_t)(comm_after & 0x00FF);
            ESP_LOGW(TAG,
                     "jsy_mk333_change_address: apos escrita, reg 0x%04X = 0x%04X "
                     "(addr_after=%u cfg_after=0x%02X)",
                     JSY_REG_COMM_CONFIG, (unsigned)comm_after,
                     (unsigned)addr_after, (unsigned)cfg_after);
        } else {
            ESP_LOGE(TAG,
                     "jsy_mk333_change_address: falha ao reler reg 0x%04X apos escrita (err=%s)",
                     JSY_REG_COMM_CONFIG, esp_err_to_name(err));
            // ainda assim consideramos a operacao concluida; o erro e' apenas para log
        }
    }

    ESP_LOGW(TAG,
             "jsy_mk333_change_address: agora DESLIGUE e LIGUE o JSY-MK-333G "
             "e use o novo endereco=%u nas leituras.",
             (unsigned)new_addr);

    return 0;
}


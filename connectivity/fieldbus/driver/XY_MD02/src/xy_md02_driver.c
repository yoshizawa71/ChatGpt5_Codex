/*
 * thermohigrometro.c
 *
 *  Created on: 3 de set. de 2025
 *      Author: geopo
 */

#include "xy_md02_driver.h"

#include "modbus_rtu_master.h"
#include "esp_log.h"

static const char *TAG = "XY_MD02";

/* Registradores do XY-MD02:
 * 0x0001 -> temperatura (décimos de °C)
 * 0x0002 -> umidade     (décimos de %RH)
 */
#define REG_TEMP 0x0001
#define REG_HUM  0x0002

static bool try_fc(uint8_t fc, uint8_t addr, uint16_t reg, uint16_t *out)
{
/*	ESP_LOGI(TAG,"XY_MD02", "try_fc: Tentando addr=%d fc=0x%02X reg=0x%04X", addr, fc, reg);
    esp_err_t err;*/
    ESP_LOGI(TAG, "try_fc: Tentando addr=%d fc=0x%02X reg=0x%04X", addr, fc, reg);
    modbus_guard_t g = {0};
    /* 30 ms: se o barramento estiver ocupado pelo centralizador, aborta rápido */
    if (!modbus_guard_try_begin(&g, 30)) {
        ESP_LOGW(TAG, "busy: barramento em uso, pulando tentativa");
        return false;
    }
    esp_err_t err;
    if (fc == 0x04) {
        err = modbus_master_read_input_registers(addr, reg, 1, out);
    } else {
        err = modbus_master_read_holding_registers(addr, reg, 1, out);
    }
    
    modbus_guard_end(&g);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "try_fc sucesso: addr=%d fc=0x%02X reg=0x%04X value=0x%04X", addr, fc, reg, *out);
        return true;
    } else {
            ESP_LOGW(TAG, "try_fc falha: addr=%d fc=0x%02X reg=0x%04X erro=%s",
                 addr, fc, reg, esp_err_to_name(err));
        return false;
    }
    
}

int temperature_rs485_probe(uint8_t addr, uint8_t *used_fc)
{
    uint16_t v;
    if (try_fc(0x04, addr, REG_TEMP, &v)) { if (used_fc) *used_fc = 0x04; return 1; }
    if (try_fc(0x03, addr, REG_TEMP, &v)) { if (used_fc) *used_fc = 0x03; return 1; }
    return 0;
}

int temperature_rs485_read(uint8_t addr, float *temp_c,
                           float *hum_pct, bool *has_hum)
{
    uint16_t v;
    // 1) Tenta FC=0x04 e cai p/ 0x03 (igual ao seu fluxo de testes)
    uint8_t fc = 0x04;
    if (!try_fc(fc, addr, REG_TEMP, &v)) {
        fc = 0x03;
        if (!try_fc(fc, addr, REG_TEMP, &v)) return -1;
    }
    if (temp_c) *temp_c = (float)v / 10.0f;

    // 2) Umidade (nem todo sensor responde; trate como opcional)
    if (hum_pct || has_hum) {
        uint16_t h;
        if (try_fc(fc, addr, REG_HUM, &h)) {
            if (hum_pct) *hum_pct = (float)h / 10.0f;
            if (has_hum) *has_hum = true;
            return 2;
        } else {
            if (has_hum) *has_hum = false;
            return 1;
        }
    }
    return 1;
}




/*
 * rs485_manager.c — Adapter do barramento RS-485 para o backend Modbus
 * Mantém a API do manager e delega o “motor” para esp-modbus.
 */

#include "rs485_manager.h"
#include "modbus_rtu_master.h"   // backend
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "RS485_MGR";

/* API típica do manager usada no front/handlers:
 *  - timeout: ignorado aqui (esp-modbus já gere), mantido por compat.
 *  - used_fc: 0x04 ou 0x03 quando alive==true; 0x00 caso contrário
 *  - exception: esp-modbus já filtra exception frames de erro → false
 */
bool rs485_manager_ping(uint8_t addr, TickType_t timeout,
                        uint8_t *used_fc, bool *exception)
{
    (void)timeout;  // o backend já possui temporizações internas
    bool alive = false; uint8_t fc = 0;
    esp_err_t err = modbus_master_ping(addr, &alive, &fc);

    if (used_fc)   *used_fc   = alive ? fc : 0x00;
    if (exception) *exception = false;

    ESP_LOGI(TAG, "ping addr=%u -> alive=%d fc=0x%02X err=%s",
             addr, alive, fc, esp_err_to_name(err));
    return (err == ESP_OK) && alive;
}

/* OBS:
 * Se você também possui um header/uso antigo com outra assinatura
 * (ex.: esp_err_t rs485_manager_ping(uint8_t addr, TickType_t)),
 * crie um wrapper diferente (outro nome) para evitar conflito.
 */

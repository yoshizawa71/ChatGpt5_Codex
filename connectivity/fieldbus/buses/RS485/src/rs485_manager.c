// rs485_manager.c — Adapter do barramento RS-485 para o backend Modbus
#include "factory_control.h"
#if CONFIG_MODBUS_SERIAL_ENABLE

#include "rs485_manager.h"
#include "rs485_hw.h"
#include "driver/uart.h"

#include "modbus_rtu_master.h"   // backend genérico (ping básico)
#include "rs485_registry.h"      // rs485_registry_probe_any()

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "RS485_MGR";

static inline bool mb_uart_ready(void)
{
    return uart_is_driver_installed(RS485_UART_NUM);
}

/**
 * Ping “inteligente”:
 *
 * 1) Garante que o master RTU está inicializado (via modbus_master_ping()).
 * 2) Tenta o ping genérico (FC03/FC04 em 0x0000/0x0001).
 * 3) Se não achar nada, usa rs485_registry_probe_any() para deixar os drivers
 *    específicos (JSY, XY_MD02, etc.) tentarem identificar o dispositivo
 *    naquele endereço.
 *
 * - used_fc  = 0x03 ou 0x04 quando alive==true; 0x00 caso contrário.
 * - exception sempre false (esp-modbus já trata exceptions).
 */
bool rs485_manager_ping(uint8_t addr, TickType_t timeout,
                        uint8_t *used_fc, bool *exception)
{
	ESP_LOGW(TAG, "ping ENTRY: addr=%u timeout_ticks=%u",
         (unsigned)addr, (unsigned)timeout);
	
    if (used_fc)   *used_fc   = 0;
    if (exception) *exception = false;

    if (!mb_uart_ready()) {
        ESP_LOGW(TAG, "UART RS485 NÃO INSTALADA (uart_is_driver_installed=%d); abortando ping.",
             uart_is_driver_installed(RS485_UART_NUM));
        return false;
    }

    (void)timeout;  // o backend já possui temporizações internas

    bool    alive = false;
    uint8_t fc    = 0;

    // 1) Ping genérico (também garante modbus_master_init())
    esp_err_t err = modbus_master_ping(addr, &alive, &fc);
    ESP_LOGW(TAG, "ping genérico: addr=%u -> err=%s alive=%d fc=0x%02X",
         (unsigned)addr, esp_err_to_name(err), (int)alive, (int)fc);


    if ((err == ESP_OK) && alive) {
        if (used_fc) *used_fc = fc;
        return true;
    }

    // 2) Fallback: deixa os drivers tentarem identificar o dispositivo nesse endereço
    rs485_type_t    type   = RS485_TYPE_INVALID;
    rs485_subtype_t st     = RS485_SUBTYPE_NONE;
    uint8_t         drv_fc = 0;
    const char     *drv    = NULL;

ESP_LOGW(TAG, "ping drivers: chamando rs485_registry_probe_any(addr=%u)", (unsigned)addr);
    bool found = rs485_registry_probe_any(addr, &type, &st, &drv_fc, &drv);
    ESP_LOGW(TAG, "ping drivers: found=%d type=%d subtype=%d drv=%s fc=0x%02X",
         (int)found, (int)type, (int)st, drv ? drv : "NULL", (int)drv_fc);
    if (found) {
        ESP_LOGI(TAG,
                 "ping driver addr=%u -> FOUND type=%d subtype=%d driver=%s fc=0x%02X",
                 addr, (int)type, (int)st, (drv ? drv : "NULL"), drv_fc);
        if (used_fc) *used_fc = drv_fc;
        // exception segue false
        return true;
    }

    ESP_LOGW(TAG, "ping addr=%u -> nenhum dispositivo reconhecido (timeout/erro).", addr);
    return false;
}

#endif // CONFIG_MODBUS_SERIAL_ENABLE

/*
 * rs485_hw.h
 *
 * Camada fina de RS-485 com controle manual de DE/RE via GPIO
 * e UART do ESP-IDF. Projetada para trabalhar com esp-modbus
 * ou acesso RTU direto, e para integrar com um "session guard".
 */

#ifndef COMMUNICATIONS_RS485_INCLUDE_RS485_HW_H_
#define COMMUNICATIONS_RS485_INCLUDE_RS485_HW_H_
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef struct {
    int uart_num;
    int tx_gpio, rx_gpio;
    int de_re_gpio;
    uint32_t baudrate;
    int data_bits;   // 7 ou 8
    int stop_bits;   // 1 ou 2
    int parity;      // 0:none, 1:odd, 2:even
    int rx_timeout_ms;
} rs485_hw_cfg_t;

esp_err_t rs485_hw_init(const rs485_hw_cfg_t *cfg);

esp_err_t rs485_hw_tx(const uint8_t *data, size_t len, TickType_t tmo);
esp_err_t rs485_hw_rx(uint8_t *data, size_t max_len, size_t *out_len, TickType_t tmo);


#endif /* COMMUNICATIONS_RS485_INCLUDE_RS485_HW_H_ */

/*
 * modbus_master.h
 *
 *  Created on: 9 de ago. de 2025
 *      Author: geopo
 */

#ifndef COMMUNICATIONS_RS485_INCLUDE_MODBUS_RTU_MASTER_H_
#define COMMUNICATIONS_RS485_INCLUDE_MODBUS_RTU_MASTER_H_

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

/*typedef struct {
    int uart_num;            // UART_NUM_0/1/2
    int tx_gpio, rx_gpio;
    int de_re_gpio;          // pino DE/RE (alto = TX; baixo = RX)
    uint32_t baudrate;
    int data_bits;           // 7 ou 8
    int stop_bits;           // 1 ou 2
    int parity;              // 0:none, 1:odd, 2:even
    int rx_timeout_ms;       // timeout de recepção (ms)
} mb_rtu_cfg_t;*/


esp_err_t modbus_master_rtu_init(void);

esp_err_t modbus_master_read_holding(uint8_t addr, uint16_t reg,
                                     uint16_t qty, uint16_t *out,
                                     TickType_t tmo);

esp_err_t modbus_master_read_input(uint8_t addr, uint16_t reg,
                                   uint16_t qty, uint16_t *out,
                                   TickType_t tmo);

esp_err_t modbus_master_write_single(uint8_t addr, uint16_t reg,
                                     uint16_t value, TickType_t tmo);

// tenta ler 1 reg de holding (0x0000) — útil para “pingar” o slave
esp_err_t modbus_master_ping(uint8_t addr, TickType_t tmo);


#endif /* COMMUNICATIONS_RS485_INCLUDE_MODBUS_RTU_MASTER_H_ */

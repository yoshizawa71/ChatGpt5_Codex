/*
 * modbus_rtu_master.h
 * API do master RTU (esp-modbus) + wrappers de leitura usados pelos drivers.
 */
#ifndef CONNECTIVITY_FIELDBUS_PROTOCOLS_MODBUS_INCLUDE_MODBUS_RTU_MASTER_H_
#define CONNECTIVITY_FIELDBUS_PROTOCOLS_MODBUS_INCLUDE_MODBUS_RTU_MASTER_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "mbcontroller.h"

/* -------- Modelagem (mantida) -------- */
typedef struct {
    uint16_t address;       // Endereço do registrador (ex.: 0x0001)
    uint16_t size;          // Quantidade (ex.: 1)
    mb_param_type_t type;   // MB_PARAM_INPUT (FC04) | MB_PARAM_HOLDING (FC03)
} modbus_register_t;

typedef struct {
    uint8_t address;        // Endereço do escravo
    const char* name;
    modbus_register_t* registers;
    uint16_t num_registers;
    void (*process_data)(uint8_t, const modbus_register_t*, const uint16_t*);
} modbus_slave_t;

/* -------- Inicialização / tarefa leitora -------- */
esp_err_t modbus_master_init(void);
esp_err_t modbus_master_deinit(void);

esp_err_t modbus_master_start_task(void);

/* -------- Wrappers de leitura (usados pelos drivers) -------- */
esp_err_t modbus_master_read_input_registers (uint8_t slave_addr,
                                              uint16_t reg_start,
                                              uint16_t words,
                                              uint16_t *out_words);

esp_err_t modbus_master_read_holding_registers(uint8_t slave_addr,
                                               uint16_t reg_start,
                                               uint16_t words,
                                               uint16_t *out_words);

/* -------- Ping canônico (para endpoints/manager) -------- */
esp_err_t modbus_master_ping(uint8_t slave_addr, bool *alive, uint8_t *used_fc);

#endif /* CONNECTIVITY_FIELDBUS_PROTOCOLS_MODBUS_INCLUDE_MODBUS_RTU_MASTER_H_ */

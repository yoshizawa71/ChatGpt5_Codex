#ifndef MODBUS_MASTER_H
#define MODBUS_MASTER_H

#include "esp_err.h"
#include "mbcontroller.h"

// Estrutura para representar um registrador de um escravo
typedef struct {
    uint16_t address;      // Endereço do registrador (ex.: 0x0001 para temperatura do XY-MD02)
    uint16_t size;         // Número de registradores a ler (ex.: 1 para um uint16_t)
    mb_param_type_t type;  // Tipo de registrador (ex.: MB_PARAM_INPUT para Input Registers)
} modbus_register_t;

// Estrutura para representar um escravo Modbus
typedef struct {
    uint8_t address;                     // Endereço do escravo (ex.: 1 para XY-MD02)
    const char* name;                    // Nome do escravo (ex.: "XY-MD02")
    modbus_register_t* registers;        // Lista de registradores a ler
    uint16_t num_registers;              // Número de registradores na lista
    void (*process_data)(uint8_t, const modbus_register_t*, const uint16_t*); // Callback para processar os dados lidos
} modbus_slave_t;

// Funções públicas
void start_modbus(void);
esp_err_t modbus_master_init(void);
esp_err_t modbus_master_start_task(void);

#endif // MODBUS_MASTER_H
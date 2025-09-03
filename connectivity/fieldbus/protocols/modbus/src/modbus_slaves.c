/*
 * modbu_slaves.c
 *
 *  Created on: 3 de set. de 2025
 *      Author: geopo
 */

#include "modbus_slaves.h"
#include "esp_log.h"

// Função de callback para processar os dados do XY-MD02
static void process_xymd02_data(uint8_t slave_addr, const modbus_register_t* reg, const uint16_t* data) {
    float value = data[0] / 10.0; // O valor é em décimos
    if (reg->address == 0x0001) {
        ESP_LOGI("XYMD02", "Slave %d - Temperature: %.1f C (raw: 0x%04x)", slave_addr, value, data[0]);
    } else if (reg->address == 0x0002) {
        ESP_LOGI("XYMD02", "Slave %d - Humidity: %.1f %%RH (raw: 0x%04x)", slave_addr, value, data[0]);
    }
}

// Registradores do XY-MD02
static modbus_register_t xymd02_registers[] = {
    { 0x0001, 1, MB_PARAM_INPUT }, // Temperatura
    { 0x0002, 1, MB_PARAM_INPUT }  // Umidade
};

// Definição do escravo XY-MD02
static modbus_slave_t xymd02_slave = {
    .address = 1, // Endereço padrão do XY-MD02
    .name = "XY-MD02",
    .registers = xymd02_registers,
    .num_registers = sizeof(xymd02_registers) / sizeof(xymd02_registers[0]),
    .process_data = process_xymd02_data
};

// Lista de escravos (não inicializada estaticamente)
static modbus_slave_t modbus_slaves[10]; // Suporte para até 10 escravos
static uint16_t num_modbus_slaves = 0;

// Função para inicializar a lista de escravos
void modbus_slaves_init(void) {
    modbus_slaves[0] = xymd02_slave;
    num_modbus_slaves = 1;
    ESP_LOGI("XYMD02", "Initialized %d slave(s), first slave: %s (address %d, registers: %d)", 
             num_modbus_slaves, modbus_slaves[0].name, modbus_slaves[0].address, modbus_slaves[0].num_registers);
}

// Função para obter a lista de escravos
modbus_slave_t* modbus_slaves_get_list(void) {
    return modbus_slaves;
}

// Função para obter o número de escravos
uint16_t modbus_slaves_get_count(void) {
    return num_modbus_slaves;
}



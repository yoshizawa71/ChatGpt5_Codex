#include "modbus_slaves.h"
#include "esp_log.h"

// Função de callback para processar os dados do XY-MD02
static void process_xymd02_data(uint8_t slave_addr, const modbus_register_t* reg, const uint16_t* data) {
    float value = data[0] / 10.0; // O valor é em décimos
    if (reg->address == 0x0001) {
        ESP_LOGI("XYMD02", "Slave %d - Temperature: %.1f C", slave_addr, value);
    } else if (reg->address == 0x0002) {
        ESP_LOGI("XYMD02", "Slave %d - Humidity: %.1f %%RH", slave_addr, value);
    }
}

// Registradores do XY-MD02
static modbus_register_t xymd02_registers[] = {
    { 0x0001, 1, MB_PARAM_INPUT }, // Temperatura
    { 0x0002, 1, MB_PARAM_INPUT }, // Umidade
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
}

// Função para obter a lista de escravos
modbus_slave_t* modbus_slaves_get_list(void) {
    return modbus_slaves;
}

// Função para obter o número de escravos
uint16_t modbus_slaves_get_count(void) {
    return num_modbus_slaves;
}
/*
 * modbus_rtu_master.c
 *
 *  Created on: 3 de set. de 2025
 *      Author: geopo
 */

#include "modbus_rtu_master.h"
#include "modbus_slaves.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

#define MB_PORT_NUM     0  // UART0, conforme o esquemático
#define MB_DEV_SPEED    9600 // 9600, padrão
#define CONFIG_MB_UART_TXD       1 
#define CONFIG_MB_UART_RXD       3
#define CONFIG_MB_UART_RTS       18

static const char *TAG = "MODBUS_MASTER";

// Função para inicializar o Modbus Master
esp_err_t modbus_master_init(void) {
    mb_communication_info_t comm = {
        .port = MB_PORT_NUM,
#if CONFIG_MB_COMM_MODE_ASCII
        .mode = MB_MODE_ASCII,
#elif CONFIG_MB_COMM_MODE_RTU
        .mode = MB_MODE_RTU,
#endif
        .baudrate = MB_DEV_SPEED,
        .parity = MB_PARITY_NONE
    };
    void* master_handler = NULL;

    ESP_LOGI(TAG, "Initializing Modbus Master on UART%d, baudrate=%d, mode=%s", 
             MB_PORT_NUM, MB_DEV_SPEED, comm.mode == MB_MODE_RTU ? "RTU" : "ASCII");

    esp_err_t err = mbc_master_init(MB_PORT_SERIAL_MASTER, &master_handler);
    if (master_handler == NULL || err != ESP_OK) {
        ESP_LOGE(TAG, "mb controller initialization fail, returns(0x%x).", (int)err);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Setting up Modbus communication parameters");
    err = mbc_master_setup((void*)&comm);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mb controller setup fail, returns(0x%x).", (int)err);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Setting UART RX timeout to 1 character (~1ms at 9600 baud)");
    err = uart_set_rx_timeout(MB_PORT_NUM, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mb uart set rx timeout fail, returns(0x%x).", (int)err);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Configuring UART pins: TXD=%d, RXD=%d, RTS=%d", 
             CONFIG_MB_UART_TXD, CONFIG_MB_UART_RXD, CONFIG_MB_UART_RTS);
    err = uart_set_pin(MB_PORT_NUM, CONFIG_MB_UART_TXD, CONFIG_MB_UART_RXD,
                       CONFIG_MB_UART_RTS, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mb serial set pin failure, uart_set_pin() returned (0x%x).", (int)err);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting Modbus controller");
    err = mbc_master_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mb controller start fail, returned (0x%x).", (int)err);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Setting UART mode to RS485 half-duplex");
    err = uart_set_mode(MB_PORT_NUM, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mb serial set mode failure, uart_set_mode() returned (0x%x).", (int)err);
        return ESP_ERR_INVALID_STATE;
    }

    vTaskDelay(5);
    ESP_LOGI(TAG, "Modbus master stack initialized successfully");
    return ESP_OK;
}

// Tarefa para ler os escravos Modbus
static void modbus_master_task(void *arg) {
    esp_err_t err = ESP_OK;
    uint16_t* data_buffer = (uint16_t*)malloc(128 * sizeof(uint16_t)); // Buffer para armazenar os dados lidos
    if (data_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate data buffer");
        vTaskDelete(NULL);
    }

    // Inicializar a lista de escravos
    modbus_slaves_init();

    ESP_LOGI(TAG, "Starting Modbus reading task...");

    while (1) {
        // Obter a lista de escravos
        modbus_slave_t* slaves = modbus_slaves_get_list();
        uint16_t num_slaves = modbus_slaves_get_count();
        ESP_LOGI(TAG, "Found %d Modbus slaves", num_slaves);

        // Verificar o estado da UART antes de enviar requisições
        size_t queue_size = 0;
        uart_get_buffered_data_len(MB_PORT_NUM, &queue_size);
        ESP_LOGI(TAG, "UART queue status: %d bytes buffered", queue_size);

        // Iterar sobre todos os escravos
        for (uint16_t slave_idx = 0; slave_idx < num_slaves; slave_idx++) {
            modbus_slave_t* slave = &slaves[slave_idx];
            uint8_t slave_addr = slave->address;  // Use apenas o endereço real do slave

            ESP_LOGI(TAG, "Reading slave address %d (name: %s)...", 
                     slave_addr, slave->name);

            // Ler cada registrador do escravo
            for (uint16_t reg_idx = 0; reg_idx < slave->num_registers; reg_idx++) {
                modbus_register_t* reg = &slave->registers[reg_idx];

                // Definir a função Modbus baseada no tipo do registrador
                uint8_t command = (reg->type == MB_PARAM_INPUT) ? 0x04 : 0x03;

                // Configurar a requisição Modbus
                mb_param_request_t request = {
                    .slave_addr = slave_addr,
                    .command = command,
                    .reg_start = reg->address,
                    .reg_size = reg->size
                };

                // Enviar a requisição e ler os dados
                ESP_LOGI(TAG, "Sending request to slave %d, register 0x%04x, size %d, function 0x%02x...", 
                         slave_addr, reg->address, reg->size, command);
                err = mbc_master_send_request(&request, data_buffer);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "Request successful, raw data: 0x%04x", data_buffer[0]);
                    // Chamar a função de callback para processar os dados
                    if (slave->process_data) {
                        slave->process_data(slave_addr, reg, data_buffer);
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to read from slave %d, register 0x%04x, err = 0x%x (%s).",
                             slave_addr, reg->address, (int)err, esp_err_to_name(err));
                    // Logar o estado da UART após o erro
                    uart_get_buffered_data_len(MB_PORT_NUM, &queue_size);
                    ESP_LOGI(TAG, "UART queue status after error: %d bytes buffered", queue_size);
                }

                // Delay de 200ms entre requisições (ajustado para estabilidade)
                vTaskDelay(200 / portTICK_PERIOD_MS);
            }
        }

        // Aguardar 2 segundos antes da próxima leitura (mantido)
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    free(data_buffer);
    ESP_LOGI(TAG, "Destroy master...");
    ESP_ERROR_CHECK(mbc_master_destroy());
    vTaskDelete(NULL);
}

// Função para iniciar a tarefa do Modbus Master
esp_err_t modbus_master_start_task(void) {
    BaseType_t result = xTaskCreate(modbus_master_task, "modbus_task", 4096, NULL, 5, NULL);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Modbus task");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Modbus master task created successfully");
    return ESP_OK;
}



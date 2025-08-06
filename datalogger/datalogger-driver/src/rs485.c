/* Uart Events Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "esp_err.h"
#include "pressure_meter.h"
#include "datalogger_driver.h"
/**
 * This is a example which echos any data it receives on UART back to the sender using RS485 interface in half duplex mode.
*/
//***************************************
// Variáveis Globais
//***************************************
//char comand1[]="ligar";
//int verifica;

#define TAG "RS485"

xTaskHandle RS485_console_taskHandle;

// Note: Some pins on target chip cannot be assigned for UART communication.
// Please refer to documentation for selected board and target to configure pins using Kconfig.

// RTS for RS485 Half-Duplex Mode manages DE/~RE
/*#define RS485_RTS   18 //(CONFIG_ECHO_UART_RTS)

#define RS485TX 1 //(CONFIG_ECHO_UART_TXD)
#define RS485RX 3 //(CONFIG_ECHO_UART_RXD)

// CTS is not used in RS485 Half-Duplex Mode
#define RS485_CTS   (UART_PIN_NO_CHANGE)

#define BUF_SIZE        (2048)
*/
#define BAUD_RATE       (115200)

// Read packet timeout
#define PACKET_READ_TICS        (100 / portTICK_PERIOD_MS)
#define ECHO_TASK_STACK_SIZE    (4096)
#define ECHO_TASK_PRIO          (10)

//#define ECHO_UART_PORT          0 //(UART 0)
// Timeout threshold for UART = number of symbols (~10 tics) with unchanged state on receive pin
#define ECHO_READ_TOUT          (3) // 3.5T * 8 = 28 ticks, TOUT=3 -> ~24..33 ticks

const int uart_num = UART_PORT_NUM;

//*********************************************
//   Funções
//*********************************************

static void echo_send(const int port, const char* str, uint8_t length)
{
    if (uart_write_bytes(port, str, length) != length) {
        ESP_LOGE(TAG, "Send data critical failure.");
        // add your code to handle sending failure here
        abort();
    }
}

static void RS485_console_task(void *arg)
{
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
 
    ESP_ERROR_CHECK(uart_driver_install(uart_num, BUF_SIZE * 2, 0, 0, NULL, 0));

    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
 
    ESP_ERROR_CHECK(uart_set_pin(uart_num, RS485TX, RS485RX, RS485_RTS, RS485_CTS));

    // Configure a temporary buffer for the incoming data
    uint8_t *data = (uint8_t *) malloc(BUF_SIZE);

    while (1) {
        // Read data from the UART
//        int len = uart_read_bytes(RS485DE, data, BUF_SIZE, 20 / portTICK_RATE_MS);
        int len = uart_read_bytes(uart_num, data, BUF_SIZE, PACKET_READ_TICS);
        // Write data back to the UART
        uart_write_bytes(uart_num, (const char *) data, len);
    }
}

void init_rs485_log_console(void)
{
    if (RS485_console_taskHandle == NULL)
	   {
		xTaskCreate(RS485_console_task, "RS485_console_task", ECHO_TASK_STACK_SIZE, NULL, 2, &RS485_console_taskHandle);
        }
}

void deinit_rs485_log_console(void)
{
    vTaskDelete(RS485_console_taskHandle);
    RS485_console_taskHandle = NULL;
}


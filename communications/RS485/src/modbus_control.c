//#include "nvs_flash.h"
#include "esp_log.h"
#include "tcp_log_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modbus_master.h"
#include "esp_err.h"

static const char *TAG = "MASTER_TEST";

// Tarefa para o loop de teste do console no TeraTerm
/*static void console_test_task(void *arg) {
    while (1) {
        ESP_LOGI(TAG, "Agora o console esta no TeraTerm");
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay de 1 segundo
    }
}*/

void start_modbus(void) {
    // Desativar logs na UART0 para evitar interferência com o Modbus
    esp_log_level_set("*", ESP_LOG_NONE);
    esp_err_t ret;
    // Inicializar NVS (necessário para Wi-Fi)
/*    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ret = nvs_flash_erase();
        if (ret != ESP_OK) {
            return;
        }
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        return;
    }*/

    // Iniciar o Wi-Fi no modo AP
/*    ret = start_wifi_ap();
    if (ret != ESP_OK) {
        return;
    }*/

    // Iniciar o servidor TCP para logs
    ret = start_tcp_log_server();
    if (ret != ESP_OK) {
        return;
    }

    // Redirecionar os logs para o TCP
    esp_log_set_vprintf(tcp_log_vprintf);

    // Reativar os logs (agora eles serão enviados apenas via TCP)
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_LOGI(TAG, "Initialization complete. Logs should now appear in TeraTerm via TCP/IP.");

    // Inicializar o Modbus Master na UART0
    ret = modbus_master_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Modbus Master: %s", esp_err_to_name(ret));
        return;
    }

    // Iniciar a tarefa do Modbus Master
    ret = modbus_master_start_task();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Modbus task: %s", esp_err_to_name(ret));
        return;
    }

    // Criar a tarefa para o loop de teste do console
 //   xTaskCreate(console_test_task, "console_task", 2048, NULL, 2, NULL);
}
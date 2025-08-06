#include "datalogger_control.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_pm.h"
#include "soc/rtc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void init_nvs(void);
static void dumpAllTaskStackUsage();
// Definição de TAG para mensagens de log
static const char* TAG = "SYSTEM";

static void init_nvs(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void dumpAllTaskStackUsage() {
    UBaseType_t uxArraySize, x;
    TaskStatus_t *pxTaskStatusArray;

    uxArraySize = uxTaskGetNumberOfTasks();
    // aloca dinamicamente para não inflar a pilha de nenhuma task
    pxTaskStatusArray = pvPortMalloc( uxArraySize * sizeof(TaskStatus_t) );
    if (pxTaskStatusArray == NULL) {
        printf("Erro: sem memória para status das tasks\n");
        return;
    }

    // preenche a array com status
    uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, NULL);

       printf("=== Uso de pilha por tarefa (high water mark) ===\n");
    for (x = 0; x < uxArraySize; x++) {
        printf(" %-16s : %4zu bytes livres (mínimo)\n",
               pxTaskStatusArray[x].pcTaskName,
               (size_t)(pxTaskStatusArray[x].usStackHighWaterMark) * sizeof(StackType_t));
    }
    printf("===============================================\n");

    vPortFree(pxTaskStatusArray);
}

void init_system(void) {
    init_nvs();
    // Inicializar o gerenciador de energia
/*    esp_pm_config_t pm_config_init = {
        .max_freq_mhz = 240, // Frequência inicial
        .min_freq_mhz = 80,
        .light_sleep_enable = false
    };
    esp_err_t err = esp_pm_configure(&pm_config_init);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar esp_pm: %s", esp_err_to_name(err));
    }*/
     // Aqui você injeta o dump do uso de pilha:
   //     dumpAllTaskStackUsage();
}

void set_cpu_frequency(int freq_mhz) {
    uint32_t start_time = esp_log_timestamp(); // Medir tempo de inicio
    esp_pm_config_t pm_config = {
        .max_freq_mhz = freq_mhz,
        .min_freq_mhz = freq_mhz,
        .light_sleep_enable = false
    };
    esp_err_t err = esp_pm_configure(&pm_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao configurar frequencia para %d MHz: %s", freq_mhz, esp_err_to_name(err));
        return;
    }

    // Verifica a frequencia configurada
    rtc_cpu_freq_config_t current_config;
    rtc_clk_cpu_freq_get_config(&current_config);
    uint32_t end_time = esp_log_timestamp(); // Medir tempo de fim
    ESP_LOGI(TAG, "Tempo para configurar frequencia para %d MHz: %" PRIu32 " ms", freq_mhz, end_time - start_time);

    if (current_config.freq_mhz == (uint32_t)freq_mhz) {
        ESP_LOGI(TAG, "Frequencia configurada com sucesso para %" PRIu32 " MHz", current_config.freq_mhz);
    } else {
        ESP_LOGW(TAG, "Frequencia configurada (%" PRIu32 " MHz) difere do solicitado (%" PRIu32 " MHz)", current_config.freq_mhz, (uint32_t)freq_mhz);
    }
}

// Frequências suportadas: 80 MHz, 160 MHz, 240 MHz
void set_cpu_freq_rtc(int freq_mhz) {
    rtc_cpu_freq_config_t cfg;
    if (!rtc_clk_cpu_freq_mhz_to_config(freq_mhz, &cfg)) {
        ESP_LOGE(TAG, "Frequência %d MHz não suportada", freq_mhz);
        return;
    }
    rtc_clk_cpu_freq_set_config(&cfg);
    ESP_LOGI(TAG, "CPU agora rodando a %d MHz", freq_mhz);
}
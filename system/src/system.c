#include "system.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_pm.h"
#include "soc/rtc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_netif.h"
#include "esp_event.h"

extern void set_cpu_freq_rtc(int mhz);

static void init_nvs(void);
static void dumpAllTaskStackUsage();
// Definição de TAG para mensagens de log
static const char* TAG = "SYSTEM";

static portMUX_TYPE s_cpu_boost_mux = portMUX_INITIALIZER_UNLOCKED;
static int s_req160 = 0;
static int s_req240 = 0;
static int s_base_mhz = 80;          // seu default de boot
static int s_curr_mhz = 80;

// Flag estática para evitar inicialização duplicada
static bool s_net_core_inited = false;

static inline int max(int a, int b) { return a > b ? a : b; }

/*typedef struct {
    int prev_mhz;
} cpu_freq_guard_t;*/

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

int cpu_read_current_mhz_rtc(void) {
    rtc_cpu_freq_config_t cfg;
    rtc_clk_cpu_freq_get_config(&cfg);
    return (int)cfg.freq_mhz;
}

void cpu_freq_guard_enter(cpu_freq_guard_t *g, int target_mhz) {
    if (!g) return;
    g->prev_mhz = cpu_read_current_mhz_rtc();
    if (g->prev_mhz != target_mhz) {
        set_cpu_freq_rtc(target_mhz);
    }
}

void cpu_freq_guard_exit(cpu_freq_guard_t *g) {
    if (!g) return;
    if (g->prev_mhz > 0) {
        set_cpu_freq_rtc(g->prev_mhz);
    }
}

static void cpu_apply_locked(void)
{
    int target = s_base_mhz;
    if (s_req240 > 0)      target = 240;
    else if (s_req160 > 0) target = 160;

    if (target != s_curr_mhz) {
        set_cpu_freq_rtc(target);
        s_curr_mhz = target;
        ESP_LOGI(TAG, "CPU freq => %d MHz (base=%d, r160=%d, r240=%d)",
                 s_curr_mhz, s_base_mhz, s_req160, s_req240);
    }
}

esp_err_t system_net_core_init(void)
{
    if (s_net_core_inited) {
        // Já inicializado, nada a fazer
        return ESP_OK;
    }

    esp_err_t err;

    // 1) Inicializa esp_netif (stack de rede do IDF)
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init() falhou: %s", esp_err_to_name(err));
        return err;
    }

    // 2) Cria event loop default (para Wi-Fi, PPP_CTRL_EVENT, etc.)
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default() falhou: %s", esp_err_to_name(err));
        return err;
    }

    s_net_core_inited = true;
    ESP_LOGI(TAG, "Núcleo de rede inicializado (esp_netif + event loop).");
    return ESP_OK;
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

void cpu_boost_set_base_mhz(int mhz)
{
    portENTER_CRITICAL(&s_cpu_boost_mux);
    s_base_mhz = mhz;
    cpu_apply_locked();
    portEXIT_CRITICAL(&s_cpu_boost_mux);
}

int cpu_get_current_mhz(void)
{
    int mhz;
    portENTER_CRITICAL(&s_cpu_boost_mux);
    mhz = s_curr_mhz;
    portEXIT_CRITICAL(&s_cpu_boost_mux);
    return mhz;
}

void cpu_boost_begin_160(void)
{
    portENTER_CRITICAL(&s_cpu_boost_mux);
    ++s_req160;
    cpu_apply_locked();
    portEXIT_CRITICAL(&s_cpu_boost_mux);
}

void cpu_boost_end_160(void)
{
    portENTER_CRITICAL(&s_cpu_boost_mux);
    if (s_req160 > 0) --s_req160;
    cpu_apply_locked();
    portEXIT_CRITICAL(&s_cpu_boost_mux);
}

void cpu_boost_begin_240(void)
{
    portENTER_CRITICAL(&s_cpu_boost_mux);
    ++s_req240;
    cpu_apply_locked();
    portEXIT_CRITICAL(&s_cpu_boost_mux);
}

void cpu_boost_end_240(void)
{
    portENTER_CRITICAL(&s_cpu_boost_mux);
    if (s_req240 > 0) --s_req240;
    cpu_apply_locked();
    portEXIT_CRITICAL(&s_cpu_boost_mux);
}

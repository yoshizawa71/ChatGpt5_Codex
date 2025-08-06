
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_err.h"
#include "ulp_datalogger-control.h"
#include "pulse_meter.h"
#include "sdmmc_driver.h"      // para save_record_sd()
#include "datalogger_control.h"
#include "datalogger_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define FLOW_DATA_FILE "/littlefs/flow_data.bin"

// Estas variáveis ficam em RTC_SLOW_MEM e sobrevivem ao deep-sleep:
/*RTC_DATA_ATTR static uint32_t prev_cnt = 0;
RTC_DATA_ATTR static int64_t  prev_time_us = 0;*/

#define DEBOUNCE_US   10000   // 10 ms

static int64_t  last_edge_time_us   = 0;
//uint32_t current_pulse_count_pc;

//static uint32_t current_volume = 0;
static uint32_t current_pulse_count = 0;   // Pulso sendo medido em tempo real
static uint16_t last_pulse_pcnt_count = 0; // Pulso do contador pcnt

static void pulse_meter_config_init(void);
static void save_default_record_pulse_config(void);
static bool save_flow_data(float vazao, uint32_t timestamp);
static bool read_flow_data(FlowData* data);

#define PULSE_INACTIVITY_TIME   35

xTaskHandle Pulse_Meter_TaskHandle = NULL;
static const char *TAG = "Pulse_Meter";

#define ENABLE_PCNT                        1
#define ENABLE_SLEEP_MODE_PULSE_CNT        1
//--------------------------------------
static SemaphoreHandle_t Mutex_pulse_meter;
//--------------------------------------
struct record_pulse_config rec_config = {0};

//---------------------------------------
static void pulse_meter_config_init(void)
{
	Mutex_pulse_meter = xSemaphoreCreateMutex();

    if (!has_record_pulse_config())
    {
        save_default_record_pulse_config();
    }

    get_record_pulse_config(&rec_config);
    
    current_pulse_count = rec_config.last_pulse_count;

}

static void save_default_record_pulse_config(void)
{
	xSemaphoreTake(Mutex_pulse_meter,portMAX_DELAY);

    rec_config.last_write_idx = UNSPECIFIC_RECORD;
    rec_config.last_read_idx = UNSPECIFIC_RECORD;
    rec_config.total = 0;
    rec_config.last_pulse_count = 0;
    rec_config.current_pulse_count = 0;
    save_record_pulse_config(&rec_config);
    xSemaphoreGive(Mutex_pulse_meter);
}

// Função para gravar os dados no LittleFS
static bool save_flow_data(float vazao, uint32_t timestamp) {
    // Abrir o arquivo para escrita (modo binário)
    FILE* file = fopen(FLOW_DATA_FILE, "wb");
    if (file == NULL) {
        ESP_LOGE("LittleFS", "Erro ao abrir o arquivo para escrita!");
        return false;
    }

    // Criar a estrutura com os dados
    FlowData data = {
        .vazao = vazao,
        .timestamp = timestamp
    };

    // Gravar a estrutura no arquivo
    size_t written = fwrite(&data, sizeof(FlowData), 1, file);
    fclose(file);

    if (written != 1) {
        ESP_LOGE("LittleFS", "Erro ao gravar os dados!");
        return false;
    }

    ESP_LOGI("LittleFS", "Dados gravados com sucesso! Vazão: %.2f, Timestamp: %u", vazao, timestamp);
    return true;
}

// Função para ler os dados do LittleFS
static bool read_flow_data(FlowData* data) {
    // Abrir o arquivo para leitura (modo binário)
    FILE* file = fopen(FLOW_DATA_FILE, "rb");
    if (file == NULL) {
        // Arquivo não existe, retorna valores padrão
        data->vazao = .3f;
        data->timestamp = 0;
        ESP_LOGW("LittleFS", "Arquivo não encontrado, retornando valores padrão.");
        return true; // Não é um erro crítico
    }

    // Ler a estrutura do arquivo
    size_t read = fread(data, sizeof(FlowData), 1, file);
    fclose(file);

    if (read != 1) {
        ESP_LOGE("LittleFS", "Erro ao ler os dados!");
        return false;
    }

    ESP_LOGI("LittleFS", "Dados lidos com sucesso! Vazão: %.8f, Timestamp: %u", data->vazao, data->timestamp);
    return true;
}

//-----------------------------------------------------------------------------
// 1) Extrai _só_ os pulsos do ULP (sleep) e mantém meia-aresta.
//-----------------------------------------------------------------------------
static uint32_t fetch_ulp_pulses(void)
{
    uint32_t edges  = ulp_edge_count & UINT16_MAX;
    // (2) Converte arestas em pulsos completos
    //     2 arestas (subida+descida) = 1 pulso
    uint32_t pulses = edges / 2;
        // (3) Se restou uma aresta solta (meio-pulso), preserva-a
    ulp_edge_count  = edges & 1;  // guarda a meia-aresta
    ESP_LOGD(TAG, "ULP edges=%u → pulses=%u, rem=%u", edges, pulses, ulp_edge_count);
    return pulses;
}

bool pulse_meter_inactivity_task_time(void) //Se parado, inatividade =1, senão inatividade=0
{ 
   bool inactivity=false;
	 time_t last_pulse_time;
		        time_t now;
	        	time (&now);
                
		        while(1){
	                     now++;
	                     vTaskDelay(pdMS_TO_TICKS(1000));//Espera 1 seg
	                     last_pulse_time=get_last_pulse_time();
//	                      printf(">>>>>Time regretion = %lld\n", (now - last_pulse_time));
		                 if((now - last_pulse_time) >= PULSE_INACTIVITY_TIME)
		                   {
	                        inactivity=true;
		                    }
		                     else{
		                	      inactivity=false;
		                         }
		                 return inactivity;
                         }
}

void pulse_meter_prepare_for_sleep(void)
{
    uint32_t ulp_p = fetch_ulp_pulses();
    current_pulse_count += ulp_p;

    // só grava se mudou de fato
    if (current_pulse_count != get_last_pulse_count()) {
        last_pulse_pcnt_count = get_pulse_count();
        last_edge_time_us     = esp_timer_get_time();
        set_last_pulse_count(current_pulse_count);
    }
}

float flow(uint32_t total_count)
{
	printf(">>>>Começa o cálculo de Vazão<<<<\n");
    // Δ pulsos desde a última gravação
    uint32_t previous = get_last_pulse_count();
    uint32_t delta    = total_count - previous;

    // L/pulso
    float scale = get_scale();
    if (scale <= 0.0f) {
        ESP_LOGW(TAG, "scale inválido (%.3f), usando 1.0", scale);
        scale = 1.0f;
    }

    // intervalo fixo em minutos → segundos
    int m = get_deep_sleep_period();
    if (m < 1 || m > 60) {
        ESP_LOGW(TAG, "period_min inválido (%d), usando 1", m);
        m = 1;
    }
    float interval_s = m * 60.0f;

    // vazão média (L/s)
    return (delta * scale) / interval_s;
}

void reset_pulse_meter(void)
{
	xSemaphoreTake(Mutex_pulse_meter,portMAX_DELAY);
    get_record_pulse_config(&rec_config);

    rec_config.last_pulse_count = 0;
    rec_config.current_pulse_count = 0;

    current_pulse_count = rec_config.current_pulse_count;
    save_record_pulse_config(&rec_config);
    xSemaphoreGive(Mutex_pulse_meter);
}

//-------------------------------------------------------------------
// função para salvar na tabela e flash último pulso
esp_err_t save_pulse_measurement(int channel) {
    esp_err_t error    = ESP_FAIL;
    uint32_t previous  = get_last_pulse_count();
    char     vazao_str[16];
    
    uint32_t ulp_p = fetch_ulp_pulses();
    current_pulse_count += ulp_p;

//  get_record_pulse_config(&rec_config);
   
/*    #if ENABLE_SLEEP_MODE_PULSE_CNT
        total_pulse_count = current_pulse_count + get_pulse_count_from_ulp();
        printf("Total Pulses Count  linha 219 pulse_meter.c ===}}}%d\n", total_pulse_count);
        printf("Pulse count from ULP  linha 220 pulse_meter.c ===}}}%d\n", get_pulse_count_from_ulp());
    #else
        total_pulse_count = current_pulse_count;
    #endif*/
        // DEBUG: mostra os dois valores
    printf("Current = %u   Previous = %u\n",
           current_pulse_count, previous);
           
   if (current_pulse_count == previous) {
        if (!should_save_pulse_zero()) {
            // nada a gravar
            return ESP_OK;
        }
         snprintf(vazao_str, sizeof(vazao_str), "%.3f", 0.0f);

    } else {
        // houve pulsos novos → calcula vazão
        printf("++++++HOUVE PULSOS NOVOS---->%d \n",current_pulse_count);
        float v = flow(current_pulse_count);
        printf("Imprime vazao logo depois de chamar flow====> %f\n", v);
        snprintf(vazao_str, sizeof(vazao_str), "%.3f", v);
        printf("Vazão_STR 2 ====> %s\n", vazao_str);
        }
// 4) Grava no SD
    error = save_record_sd(channel, vazao_str);   
// 5) Só atualiza o checkpoint se a gravação foi bem-sucedida
        if (error == ESP_OK) {
            // atualiza o checkpoint na flash
            set_last_pulse_count(current_pulse_count);
        }

ESP_LOGI(TAG,
             "save_pulse_measurement: CH%d cnt=%u → Vazao=%s (%s)",
             channel,
             current_pulse_count,
             vazao_str,
             error == ESP_OK ? "OK" : "FAIL");
             
    return error;
}

//-------------------------------------------------------------------
//      Pulse Counter Configuration Task
//-------------------------------------------------------------------
void Pulse_Meter_Task(void* pvParameters)
{

#if ENABLE_PCNT == 1
init_pcnt();
#endif

// 1) “Semeie” o baseline bruto e o debounce timestamp, para evitar contagem antes de dormir
    last_pulse_pcnt_count = get_pulse_count();
    last_edge_time_us     = esp_timer_get_time();

  while (1) {	  

#if ENABLE_PCNT == 1
    uint32_t cnt = get_pulse_count();
    
     if (cnt != last_pulse_pcnt_count) {
// captura o timestamp atual
//        int64_t now = esp_timer_get_time();
        
// número de arestas desde a última leitura
        uint32_t edge_diff = cnt - last_pulse_pcnt_count;
// converte arestas em pulsos completos (2 arestas = 1 pulso)
        uint32_t pulses    = edge_diff >> 1;

      // só aceita esse pulso se tiver passado ao menos DEBOUNCE_US
 if (pulses > 0) {
                current_pulse_count += pulses;
/*                ESP_LOGI(TAG,
                         "PCNT debounced: +%u pulse(s), total_pc=%u",
                         pulses, current_pulse_count);*/
                         
              ESP_LOGI(TAG,
                 "***>>> Pulsos neste intervalo: %u", pulses);
                 
              ESP_LOGI(TAG, ">>>>total acumulado = %u", current_pulse_count);
            }

            // 6) Atualiza baseline bruta consumindo apenas as arestas usadas
            last_pulse_pcnt_count += pulses * 2;
        }
#endif 

//esp_task_wdt_reset();
vTaskDelay(pdMS_TO_TICKS(100));
     
    }//end of while()

}

void init_pulse_meter_task(void)
{
 pulse_meter_config_init();
 
if (Pulse_Meter_TaskHandle == NULL)
	   {
           xTaskCreate( Pulse_Meter_Task, "Pulse_Meter_Task", 4096, NULL, 1, &Pulse_Meter_TaskHandle);

      }
}



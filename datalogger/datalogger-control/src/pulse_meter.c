
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

//static uint32_t current_volume = 0;
static uint32_t current_pulse_count = 0;   // Pulso sendo medido em tempo real
static uint16_t last_pulse_pcnt_count = 0; // Pulso do contador pcnt
static bool s_pulse_is_optical = false;   // mesmo critério do ULP

RTC_DATA_ATTR static uint32_t s_pulse_accum_rtc = 0;
static void pulse_meter_config_init(void);
static void save_default_record_pulse_config(void);
static void pulse_meter_ulp_defaults(void);

#define PULSE_INACTIVITY_TIME   35

xTaskHandle Pulse_Meter_TaskHandle = NULL;
static const char *TAG = "Pulse_Meter";

#define ENABLE_PCNT                        1
#define ENABLE_SLEEP_MODE_PULSE_CNT        1

#define PM_LOGI(fmt, ...)                                      \
    if (esp_log_level_get(TAG) <= ESP_LOG_WARN) {              \
        printf("Pulse_Meter: " fmt "\n", ##__VA_ARGS__);       \
    } else {                                                   \
        ESP_LOGI(TAG, fmt, ##__VA_ARGS__);                     \
    }
//--------------------------------------
static SemaphoreHandle_t Mutex_pulse_meter;
//--------------------------------------
struct record_pulse_config rec_config = {0};

static inline bool time_is_valid(void) {
    // considera “válido” se passamos de 2020-01-01
    const time_t NOW_MIN = 1577836800; // 2020-01-01T00:00:00Z
    time_t now = time(NULL);
    return (now >= NOW_MIN);
}

// Converte epoch (segundos) -> AAAAMMDD (no fuso local do sistema)
static int daykey_from_epoch(time_t t) {
    struct tm lt;
    localtime_r(&t, &lt);
    return (lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday;
}

// Dia atual em AAAAMMDD
static int current_daykey(void) {
    time_t now = time(NULL);
    return daykey_from_epoch(now);
}

static void pulse_meter_apply_build_mode(void)
{
#if CONFIG_PULSE_INPUT_OPTICAL
    s_pulse_is_optical      = true;
    ulp_pulse_input_mode = 1;   // modo ótico
    ulp_opt_low_min_count = 3;   // por exemplo: 3 ciclos de ULP
#else
    s_pulse_is_optical      = false;
    ulp_pulse_input_mode = 0;   // modo contato seco
    ulp_opt_low_min_count = 0;
#endif
}

//---------------------------------------
static void pulse_meter_config_init(void)
{
	Mutex_pulse_meter = xSemaphoreCreateMutex();

    if (!has_record_pulse_config())
    {
        save_default_record_pulse_config();
            // alinha os registradores do ULP com o que o .S realmente exporta
        pulse_meter_ulp_defaults();
    }

    get_record_pulse_config(&rec_config);
    current_pulse_count = rec_config.last_pulse_count;

}

//----------------------------------------------------------
// inicializa os registradores da ULP só uma vez
// (valores seguros para reed e óptico lento)
//----------------------------------------------------------
// inicializa os registradores da ULP só uma vez
// versão alinhada com o sleep_pulse_cnt.S (1 borda + 2 perfis)

static void pulse_meter_ulp_defaults(void)
{
    // 0 = ainda não conta; quem libera é o sleep_control.c
    ulp_system_stable            = 0;

    // dá uns ciclos pro GPIO assentar depois que o ESP dorme
    // (você já põe 100 lá no start_deep_sleep(), aqui pode ser baixo)
    ulp_sleep_transition_counter = 2;

    // debounce padrão do pulso PRINCIPAL (GPIO36 / RTC0)
    ulp_debounce_max_count       = 4;
    ulp_debounce_counter         = 4;

    // como a gente está contando borda de DESCIDA, o mais seguro é
    // começar dizendo "o nível atual é 1" → assim o primeiro 1→0 conta
    ulp_next_edge                = 1;

    // janela anti-recontagem do próprio ULP (no .S ela começa em 0)
    ulp_inactivity               = 0;

    // contador de pulsos do ULP
    ulp_edge_count               = 0;

    // 0 = não acorda o main só porque contou
    ulp_edge_count_to_wake_up    = 0;

    // qual RTC-IO o ULP vai olhar – o sleep_control.c é quem seta o certo
    ulp_io_number                = 0;

    // ----- RAMO DO IO26 (seu sensor externo / imã) -----
    ulp_io26_debounce_max_count  = 8;
    ulp_io26_debounce_counter    = 8;
    ulp_ext_sensor_status        = 0;
    ulp_ext_sensor_activated     = 0;

    // esses dois existem no teu .S, então já zera aqui:
    ulp_ext_sensor_status_next   = 0;
    ulp_io_ext_sensor            = 0;
}

void pulse_meter_ulp_start(uint32_t rtc_io_num)
{
    ulp_io_number    = rtc_io_num;  // ex.: 7 para RTC 7, etc.
    ulp_system_stable = 1;          // a partir daqui o ULP passa a contar
    ESP_LOGI(TAG, "ULP: start em RTC-IO %lu", (unsigned long)rtc_io_num);
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

// Encapsula a lógica de "reset diário se for dia novo"
static bool maybe_daily_reset_if_needed(void)
{
    // 1) FS ainda não pronto? não faz nada
    if (!config_fs_ready()) {
        return false;
    }

    // 2) usuário/front não habilitou o reset diário? sai
    if (!has_reset_count()) {
        return false;
    }

    // 3) sem hora válida não dá pra comparar dia
    if (!time_is_valid()) {
        ESP_LOGW(TAG, "Hora inválida (sem NTP); adiando checagem de reset diário.");
        return false;
    }

    const int today = current_daykey();
    const int last  = rec_config.last_saved_daykey;   // pode ser 0 no boot limpo

    // 4) primeira vez depois de apagar a flash → só semeia o dia e salva 1x
    if (last == 0) {
        rec_config.last_saved_daykey = today;
        // aqui podemos salvar direto, porque estamos num caminho sem mutex
        save_record_pulse_config(&rec_config);
        return false;
    }

    // 5) mudou o dia → agora sim zera TUDO usando a função que já é protegida
    if (today != last) {
        ESP_LOGI(TAG, "Mudou o dia (%d -> %d). Zerando contador...", last, today);

        // essa função deve continuar do jeito que você já tem
        reset_pulse_meter();  // <- aqui dentro tem o Mutex_pulse_meter

        // depois de resetar, marca o dia de hoje pra não ficar resetando em loop
        get_record_pulse_config(&rec_config);       // recarrega o que o reset escreveu
        rec_config.last_saved_daykey = today;
        save_record_pulse_config(&rec_config);
        return true; 
       }
   return false;
}

//-----------------------------------------------------------------------------
// 1) Extrai _só_ os pulsos do ULP (sleep) e mantém meia-aresta.
//-----------------------------------------------------------------------------
static uint32_t fetch_ulp_pulses(void)
{
// ULP NOVO: cada incremento de ulp_edge_count já é 1 pulso válido
    uint32_t pulses = ulp_edge_count & UINT16_MAX;
    // zera para não repetir na próxima leitura
    ulp_edge_count = 0;
    ESP_LOGD(TAG, "ULP pulses(read)=%u", pulses);
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

 //   current_pulse_count = rec_config.current_pulse_count;
    save_record_pulse_config(&rec_config);
    
    current_pulse_count = 0;
    last_pulse_pcnt_count = 0;
    last_edge_time_us = 0;
      
    xSemaphoreGive(Mutex_pulse_meter);
}

//-------------------------------------------------------------------
// função para salvar na tabela e flash último pulso
esp_err_t save_pulse_measurement(int channel) {
    esp_err_t error    = ESP_FAIL;
  
    // 1) vê se hoje precisou zerar
    bool did_daily_reset = maybe_daily_reset_if_needed();
  
    uint32_t previous  = get_last_pulse_count();
//    char     vazao_str[16];
    char     value_str[16];
  

// 3) pega o que o ULP contou, MAS só se não teve reset agora
    uint32_t ulp_p = 0;
    if (!did_daily_reset) {
        ulp_p = fetch_ulp_pulses();
        printf(">>>>>PulseMETER>>>>> ULP==> %d\n", ulp_p);

    }

    // 4) agora sim acumula o que sobrou da ULP
    current_pulse_count += ulp_p;
    
        // DEBUG: mostra os dois valores
    printf("Current = %u   Previous = %u\n",
           current_pulse_count, previous);
           
#if CONFIG_PULSE_METER_SAVE_FLOW
    // ----------------- MODO "FLOW" (vazÃ£o) -----------------           
   if (current_pulse_count == previous) {
        if (!should_save_pulse_zero()) {
            // nada a gravar
            return ESP_OK;
        }
         snprintf(value_str, sizeof(value_str), "%.3f", 0.0f);

    } else {
        // houve pulsos novos â†’ calcula vazÃ£o
        printf("++++++HOUVE PULSOS NOVOS---->%d \n",current_pulse_count);
        float v = flow(current_pulse_count);
        printf("Imprime vazao logo depois de chamar flow====> %f\n", v);
        snprintf(value_str, sizeof(value_str), "%.3f", v);
        printf("VazÃ£o_STR 2 ====> %s\n", value_str);
        }
        
        
  #else
    // --------------- MODO "PULSE" (contador) ---------------
    // Em modo pulso, por padrÃ£o nÃ£o salvamos repetiÃ§Ã£o se nÃ£o mudou
    if (current_pulse_count == previous && !should_save_pulse_zero()) {
        return ESP_OK;
    }
    // grava o CONTADOR ACUMULADO atual (valor absoluto)
    snprintf(value_str, sizeof(value_str), "%u", (unsigned)current_pulse_count);
#endif
  
// 4) Grava no SD
    error = save_record_sd(channel, value_str);   
// 5) SÃ³ atualiza o checkpoint se a gravaÃ§Ã£o foi bem-sucedida
     if (error == ESP_OK) {
            // atualiza o checkpoint na flash
        set_last_pulse_count(current_pulse_count);
                // *** NOVO: manter RTC acompanhado do Ãºltimo bom ***
        s_pulse_accum_rtc = current_pulse_count;
        
        if (time_is_valid()) {
            const uint32_t today = (uint32_t)current_daykey();
            get_record_pulse_config(&rec_config);
            if (rec_config.last_saved_daykey != today) {
                rec_config.last_saved_daykey = today;
                save_record_pulse_config(&rec_config); // 1x/dia tÃ­pico
               }
           }
        }  
        
ESP_LOGI(TAG,
             "save_pulse_measurement: CH%d cnt=%u â†’ Vazao=%s (%s)",
             channel,
             current_pulse_count,
             value_str,
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

            // ---- AQUI entra a escolha contato seco x ótico ----
            uint32_t pulses = 0;
            if (s_pulse_is_optical) {
                // sensor ótico: PCNT está contando subida+descida,
                // então 2 arestas = 1 pulso
                pulses = edge_diff >> 1;
            } else {
                // contato seco: queremos 1 pulso por borda
                // (ideal é PCNT já estar configurado pra 1 borda,
                // mas se ele ainda conta 2, tratamos aqui)
                pulses = edge_diff;
            }
      // só aceita esse pulso se tiver passado ao menos DEBOUNCE_US
     if (pulses > 0) {
                      current_pulse_count += pulses;      
                      PM_LOGI("***>>> Pulsos neste intervalo: %u", pulses);
                 
                       PM_LOGI(">>>>total acumulado = %u", current_pulse_count);
                     }

// atualizar o baseline conforme o modo
            if (s_pulse_is_optical) {
                // consumimos 2 arestas por pulso
                last_pulse_pcnt_count += pulses * 2;
              } else {
                // consumimos 1 aresta por pulso
                last_pulse_pcnt_count += pulses;
                }
     }
#endif 

//esp_task_wdt_reset();
vTaskDelay(pdMS_TO_TICKS(100));
     
    }//end of while()

}

void init_pulse_meter_task(void)
{
// 1) cria mutex, lê config da flash e já chama pulse_meter_ulp_defaults()
    pulse_meter_config_init();

    pulse_meter_ulp_start(0);       // GPIO36 → RTC 0
    
    pulse_meter_apply_build_mode();

    // 4) task do lado do CPU (PCNT, SD, etc)
if (Pulse_Meter_TaskHandle == NULL)
	   {
           xTaskCreate( Pulse_Meter_Task, "Pulse_Meter_Task", 4096, NULL, 1, &Pulse_Meter_TaskHandle);

      }
}



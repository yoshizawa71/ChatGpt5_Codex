//#include <include/datalogger_control.h>
#include "datalogger_control.h"
#include "driver/i2c.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "i2c_dev_master.h"
#include "pulse_meter.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_sleep.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "rom/gpio.h"

#include "sara_r422.h"
#include "sleep_preparation.h"
#include "soc/gpio_num.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp32/ulp.h"
#include "system.h"
#include "ulp_datalogger-control.h"
#include "sdmmc_driver.h"
#include <inttypes.h>

#include "esp_task_wdt.h"
#include "sleep_control.h"
#include "wakeup_stub.h"
#include "esp_log.h"

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_datalogger_control_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_datalogger_control_bin_end");

static const char *TAG = "Sleep Control";

//static void restore_power_pin_after_wakeup(void);
static void init_ulp_program(void);
//static void update_pulse_count(void);
static void sleep_time(void);
static void init_sleep_gpio(void);
static void set_pulse_count_from_ulp (uint32_t pulse_count);

extern uint32_t ulp_edge_count;
static uint32_t last_pulse_counter=0;

#define uS_TO_S_FACTOR 1000000ULL
//#define PERIODIC_SLEEP_TIME 2

#define ENABLE_SLEEP_MODE_PULSE_CNT        1 //Contagem de pulsos mesmo no deep sleep

#define GPIO_SARA_PWR_CTRL   GPIO_NUM_23
#define SARA_PWR_OFF_LEVEL   1   // exemplo: se OFF for nível alto; se no seu teste for baixo, troque para 0

// Flag em RTC para pedir uma "janela de recarga" do supercapacitor
// Na próxima chamada de start_deep_sleep(), se for true, o ESP vai
// dormir por um tempo curto SEM cortar o VSYS (GPIO27 fica ligado).
RTC_DATA_ATTR static bool s_cap_recharge_pending = false;

// Tempo da janela de recarga em segundos (ajustável entre 60 e 300, por exemplo)
#define CAP_RECHARGE_WINDOW_SEC   180  // 3 minutos
static uint32_t s_cap_recharge_window_sec = CAP_RECHARGE_WINDOW_SEC;

wake_up_cause_t check_wakeup_cause(void)
{
   esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    uint16_t ulp_gpio_extern_sensor_from_ulp = (ulp_gpio_extern_sensor & UINT16_MAX);
    esp_reset_reason_t       rst   = esp_reset_reason();
    
   if(cause==ESP_SLEEP_WAKEUP_TIMER)
     {
	  return WAKE_UP_TIME;	 
	 }
            
   if (cause==ESP_SLEEP_WAKEUP_ULP ){
	   
/*	       if ((ulp_pulsed & UINT16_MAX) == 1) {
                     return WAKE_UP_PULSE;
                  }*/
           /*  if((ulp_rang & UINT16_MAX) == 1)
                          {
                           ret = WAKE_UP_RING;
                           }*/        
                           
            if ((ulp_gpio_extern_sensor_from_ulp & (1<<rtc_io_number_get(GPIO_NUM_26)))==0)
                {
                  return WAKE_UP_EXTERN_SENSOR;
                 }   
     }          
             
            // Se NÃO foi deep‑sleep wake, é boot “normal” → preciso restaurar o pino
    if (cause == ESP_SLEEP_WAKEUP_UNDEFINED) {
       // restore_power_pin_after_wakeup();
       switch(rst)
            {  
		     case ESP_RST_POWERON:	
                  ulp_edge_count    = 0;
                  ulp_system_stable = 0;
                  return WAKE_UP_BOOT;
				
             case ESP_RST_WDT:
                  ulp_edge_count    = 0;
                  ulp_system_stable = 0;
                  return WAKE_UP_WDT;
                  
             case ESP_RST_SW:
                  if((ulp_inactivity & UINT16_MAX) == 1){
					 return WAKE_UP_INACTIVITY;
				  }
				  return WAKE_UP_BOOT;
				  
			 case ESP_RST_BROWNOUT:	  
		          set_inactivity();
		          return WAKE_UP_INACTIVITY;
		          
		     case ESP_RST_UNKNOWN:
				  set_inactivity();
		          return WAKE_UP_INACTIVITY;
				  
		    default:
            // GPIO externo, brown‑out, etc.
                printf("Wakeup não reconhecido: %d\n", cause);
                return WAKE_UP_NONE;  
            }
      }
//        ulp_system_stable = 1;// passou para o main
        return WAKE_UP_NONE;;
}

static void init_sleep_gpio(void)
{
   gpio_num_t gpio_ring = GPIO_NUM_32; //GPIO 32 - RTC 9
   gpio_num_t gpio_pulse = GPIO_NUM_36; //GPIO 36 - RTC 0 
   gpio_num_t ext_gpio_sensor = GPIO_NUM_26; //GPIO 26 - RTC 7
   gpio_num_t gpio_low_pwr = GPIO_NUM_27;
   gpio_num_t gpio_dtr = GPIO_NUM_33;

   int rtcio_num = rtc_io_number_get(gpio_pulse);
   assert(rtc_gpio_is_valid_gpio(gpio_pulse) && "GPIO used for pulse counting must be an RTC IO");
   
    //remove isr handler for gpio number.
    
//--------------------------------------------------------------------------------
//               ULP PULSE
//--------------------------------------------------------------------------------

#if ENABLE_SLEEP_MODE_PULSE_CNT

    ulp_debounce_counter = 8;//era 5
    ulp_debounce_max_count = 8;//era 5
    ulp_next_edge = 1; // Ajuste conforme o sinal
    ulp_io_number = rtcio_num;  //map from GPIO# to RTC_IO# 
//    ulp_edge_count_to_wake_up = 10;
    ulp_gpio_extern_sensor= 0;
    ulp_inactivity = 0;
# else    
	ulp_pulsed = 0;
    ulp_pulse_status = 0;
    ulp_pulse_status_next = 0;
#endif  
//--------------------------------------------------------

//--------------------------------------------------------    
        /* Initialize selected GPIO as RTC IO, enable input, disable pullup and pulldown */
    ESP_ERROR_CHECK(rtc_gpio_init(gpio_pulse));
    ESP_ERROR_CHECK(rtc_gpio_set_direction(gpio_pulse, RTC_GPIO_MODE_INPUT_ONLY));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(gpio_pulse));
    ESP_ERROR_CHECK(rtc_gpio_pullup_dis(gpio_pulse));
 //   ESP_ERROR_CHECK(rtc_gpio_hold_dis(gpio_pulse));

 //   prepare_led_for_sleep();

//--------------------------------------------------------------------------------
//               ULP RING
//--------------------------------------------------------------------------------

/*    ulp_rang = 0;
    ulp_ring_status=0;
    ulp_ring_status_next = 0;*/
   
    
    rtc_gpio_init(gpio_ring);
    rtc_gpio_set_direction(gpio_ring, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(gpio_ring);
    rtc_gpio_pulldown_dis(gpio_ring);

 //--------------------------------------------------------------------------------
//               ULP EXTERNAL SENSOR
//--------------------------------------------------------------------------------
// Configuração do IO26 debounce
    ulp_io26_debounce_counter = 8;  // Inicializa com 8 iterações
    ulp_io26_debounce_max_count = 8; // Máximo de 8 iterações 
    
    ulp_ext_sensor_activated = 0;
    ulp_ext_sensor_status = 0;
    ulp_ext_sensor_status_next = 0;
    
    ESP_ERROR_CHECK(rtc_gpio_init(ext_gpio_sensor));
    ESP_ERROR_CHECK(rtc_gpio_set_direction(ext_gpio_sensor, RTC_GPIO_MODE_INPUT_ONLY));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(ext_gpio_sensor));
    ESP_ERROR_CHECK(rtc_gpio_pullup_en(ext_gpio_sensor));
    //ESP_ERROR_CHECK(rtc_gpio_pullup_dis(ext_gpio_sensor));
    ESP_ERROR_CHECK(rtc_gpio_hold_en(ext_gpio_sensor));
  //  ESP_ERROR_CHECK(rtc_gpio_isolate(ext_gpio_sensor)); 

  //--------------------------------------------------------
//        ULP Low Power Activate
//--------------------------------------------------------  
    // 1) Inicializa como RTC IO e saÃ­da
 
/*    ESP_ERROR_CHECK(rtc_gpio_init(gpio_low_pwr));
//    ESP_ERROR_CHECK(rtc_gpio_set_direction(gpio_low_pwr, RTC_GPIO_MODE_OUTPUT_ONLY));
    ESP_ERROR_CHECK(rtc_gpio_set_direction(gpio_low_pwr, RTC_GPIO_MODE_INPUT_ONLY));
    ESP_ERROR_CHECK(rtc_gpio_pullup_dis(gpio_low_pwr));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(gpio_low_pwr));
    ESP_ERROR_CHECK(rtc_gpio_set_level(gpio_low_pwr, 0));
    ESP_ERROR_CHECK(rtc_gpio_hold_en(gpio_low_pwr));
    ESP_ERROR_CHECK( rtc_gpio_isolate(gpio_low_pwr));*/
    
//--------------------------------------------------------
//        ULP DTR
//--------------------------------------------------------     
       rtc_gpio_hold_dis(gpio_dtr);
       gpio_hold_dis(gpio_dtr);
       gpio_reset_pin(gpio_dtr);
       gpio_set_direction(gpio_dtr, GPIO_MODE_OUTPUT);
       gpio_set_level(gpio_dtr, 1);
       ESP_ERROR_CHECK(rtc_gpio_init(gpio_dtr));
       ESP_ERROR_CHECK(rtc_gpio_set_direction(gpio_dtr, RTC_GPIO_MODE_OUTPUT_ONLY));
       ESP_ERROR_CHECK(rtc_gpio_pullup_dis(gpio_dtr));
       ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(gpio_dtr));
       ESP_ERROR_CHECK(rtc_gpio_set_level(gpio_dtr, 1));
       ESP_ERROR_CHECK(rtc_gpio_hold_en(gpio_dtr));
}

static void init_ulp_program(void)
{
    esp_err_t err = ulp_load_binary(0, ulp_main_bin_start,
        (ulp_main_bin_end - ulp_main_bin_start) / sizeof(uint32_t));
    ESP_ERROR_CHECK(err);

    ulp_set_wakeup_period(0, 10000);
    ulp_system_stable = 0;

    esp_deep_sleep_disable_rom_logging();
}

static void sleep_time(void)
{
//------------------------------------------------------------------
// Defined Sleep time by deep sleep period
//------------------------------------------------------------------
	uint32_t sleep_time_minutes=0;
	uint32_t sleep_time_seconds=0;
	uint32_t sleep_time=0;
	
	  if(has_factory_config()){
		  sleep_time_minutes=get_time_minute();
	        while((sleep_time_minutes%get_deep_sleep_period())!=0)
	 	    {
	 	    	sleep_time_minutes++;
	 	     }
	 	    sleep_time_minutes=sleep_time_minutes-get_time_minute();
	 	    sleep_time_seconds= get_time_second();

	    if (sleep_time_minutes==0)
	       {sleep_time_minutes=get_deep_sleep_period();}
	       
	    sleep_time= sleep_time_minutes*60-sleep_time_seconds;
        printf("Time of Sleep ----->> %ld (total %ld s)\n", sleep_time_minutes, sleep_time);
	    esp_sleep_enable_timer_wakeup(sleep_time*uS_TO_S_FACTOR); //Tempo de DeepSleep
	    
	    /* Start the program */
# if ENABLE_SLEEP_MODE_PULSE_CNT
        esp_err_t err = ulp_run(&ulp_entry - RTC_SLOW_MEM);
	    ESP_ERROR_CHECK(err);
	    printf(">>>Sleep Mode Pulse Count<<<\n");
	    #else
	    esp_err_t err = ulp_run(&ulp_entrance - RTC_SLOW_MEM);
	    ESP_ERROR_CHECK(err);
	    printf(">>>Continuous Pulse Count<<<\n");
# endif

	    }
}

static void gpio_set_pins_high_impedance(void)
{
	   const gpio_num_t gpios[] = {
    //    GPIO_NUM_0,  // IO0
        GPIO_NUM_2,  // IO2
        GPIO_NUM_4,  // IO4
        GPIO_NUM_12,  // IO12
        GPIO_NUM_13,  // IO13
        GPIO_NUM_14,  // IO14
        GPIO_NUM_15,  // IO15    
    };
    for (size_t i = 0; i < sizeof(gpios)/sizeof(gpios[0]); i++) {
        gpio_reset_pin(gpios[i]);
        gpio_set_direction(gpios[i], GPIO_MODE_INPUT);
        gpio_pullup_dis(gpios[i]);
        gpio_pulldown_dis(gpios[i]);
    }
	
if (!s_cap_recharge_pending) {
	ESP_LOGI(TAG, "Low-power completo: isolando GPIO27 e cortando VSYS");	
    ESP_ERROR_CHECK(rtc_gpio_init(GPIO_NUM_27));
    ESP_ERROR_CHECK(rtc_gpio_set_direction(GPIO_NUM_27, RTC_GPIO_MODE_INPUT_ONLY));
    ESP_ERROR_CHECK(rtc_gpio_pullup_dis(GPIO_NUM_27));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(GPIO_NUM_27));
    ESP_ERROR_CHECK(rtc_gpio_set_level(GPIO_NUM_27, 0));
    ESP_ERROR_CHECK(rtc_gpio_hold_en(GPIO_NUM_27));
    ESP_ERROR_CHECK( rtc_gpio_isolate(GPIO_NUM_27));  
	} else {
        ESP_LOGI(TAG, "Janela de recarga ativa: mantendo GPIO27 ligado (VSYS ON)");
        // NÃO mexe no GPIO27 aqui: ele já está em nível alto configurado no boot
        // via restore_power_pin_after_wakeup() em main.c
    }	
}

static void hibernate(void) {
    ESP_LOGI(TAG, "Hibernate completo: desligando I2C e configurando GPIOs...");
        // Em hibernate, NUNCA queremos janela de recarga.
    // Usa a API para garantir que nenhuma lógica futura da flag seja ignorada.
   
    sleep_clear_cap_recharge_window();
    
    gpio_set_pins_high_impedance();
    
 //   hold_sara_pwr_ctrl_off(); 
    // 4) Desabilita quaisquer fontes de wakeup configuradas
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    ESP_LOGI(TAG, "Entrando em Hibernacao");
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_deep_sleep_start();
}


void start_deep_sleep(void)
{
	  if(!has_device_active()||!has_factory_config())
	    {
	     hibernate();
	    }
	      
	pulse_meter_prepare_for_sleep();
	
	 //Salvar tudo na flash antes de dormir
    time_t system_time;
    time(&system_time);
    set_last_sys_time(system_time);
    save_config(); //Verificar nos testes se precisa
    
	init_ulp_program();
    init_sleep_gpio();
    
  // Sincronizar IO26 e inicializar antes de iniciar o ULP
    uint32_t io26_state = rtc_gpio_get_level(GPIO_NUM_26);
    ulp_ext_sensor_status = io26_state;
/*    ulp_io26_debounce_counter = 10;
    ulp_io26_debounce_max_count = 10;*/
    
 // Inicializar o contador de transiÃ§Ã£o para ignorar pulsos por 1 segundo (100 ciclos de 10 ms)
    ulp_sleep_transition_counter = 100;

    // deixa ESTÁVEL pra poder contar no sono
    ulp_system_stable = 1;
    
//------------------------------------------------------------------
      
 if (!s_cap_recharge_pending) {
        ESP_LOGI(TAG, "Deep sleep normal: usando período configurado pelo usuário");
        sleep_time();  // define timer e inicia o ULP (edge/pulse counting)
     } else {
 /*       ESP_LOGI(TAG, "Deep sleep em janela de recarga: %d segundos com VSYS ligado",
                 (int)CAP_RECHARGE_WINDOW_SEC);

        // Timer para a janela de recarga
        uint64_t recharge_time_us = (uint64_t)CAP_RECHARGE_WINDOW_SEC * uS_TO_S_FACTOR;
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(recharge_time_us));

        // Inicia o ULP também na janela de recarga (se quiser contar pulsos/monitorar IO26)
    #if ENABLE_SLEEP_MODE_PULSE_CNT
        esp_err_t err = ulp_run(&ulp_entry - RTC_SLOW_MEM);
        ESP_ERROR_CHECK(err);
        ESP_LOGI(TAG, ">>> Recharge window com Sleep Mode Pulse Count <<<");
    #else
        esp_err_t err = ulp_run(&ulp_entrance - RTC_SLOW_MEM);
        ESP_ERROR_CHECK(err);
        ESP_LOGI(TAG, ">>> Recharge window com Continuous Pulse Count <<<");
    #endif
    }*/
    
     uint32_t period_min = get_deep_sleep_period();
     if (period_min == 1) {
        // Caso especial: envio configurado para 1 em 1 minuto.
        // Mantemos VSYS ligado, mas a janela também passa a ser 1 minuto (60 s).
        s_cap_recharge_window_sec = 60;
        ESP_LOGW(TAG,
                 "Periodo = 1 min: usando janela de recarga de %lus (VSYS ON, sem low-power agressivo).",
                 (unsigned long)s_cap_recharge_window_sec);
    } else {
        // Para os outros períodos, usamos a janela padrão (3 minutos).
        s_cap_recharge_window_sec = CAP_RECHARGE_WINDOW_SEC;
        ESP_LOGI(TAG,
                 "Deep sleep em janela de recarga: %lus com VSYS ligado (periodo=%lu min).",
                 (unsigned long)s_cap_recharge_window_sec,
                 (unsigned long)period_min);
    }

    // Timer para a janela de recarga
    uint64_t recharge_time_us = (uint64_t)s_cap_recharge_window_sec * uS_TO_S_FACTOR;
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(recharge_time_us));

    // Inicia o ULP também na janela de recarga (se quiser contar pulsos/monitorar IO26)
#if ENABLE_SLEEP_MODE_PULSE_CNT
    esp_err_t err = ulp_run(&ulp_entry - RTC_SLOW_MEM);
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, ">>> Recharge window com Sleep Mode Pulse Count <<<");
#else
    esp_err_t err = ulp_run(&ulp_entrance - RTC_SLOW_MEM);
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, ">>> Recharge window com Continuous Pulse Count <<<");
#endif
}
    
//------------------------------------------------------------------

    ESP_ERROR_CHECK( esp_sleep_enable_ulp_wakeup());// IO26 com debounce
//    printf("ULP running, IO26 initial state: %ld\n", ulp_ext_sensor_status);
//    printf("IO26 state before sleep: %d\n", rtc_gpio_get_level(GPIO_NUM_26));
    printf("Debounce counter: %ld\n", ulp_io26_debounce_counter);
	printf("!!!>>> TIME : %s <<<!!!\n", get_time());
	printf("Entering deep sleep\n\n");
	
//	sleep_prepare(/*maybe_stop_wifi=*/true);
	vTaskDelay(pdMS_TO_TICKS(10));
	 gpio_set_pins_high_impedance();
	 
	 fflush(stdout);
     vTaskDelay(pdMS_TO_TICKS(10));
	
    esp_deep_sleep_start();
}

//--------------------------------------------------------------------------------------
// API simples para controlar a janela de recarga do supercapacitor
//--------------------------------------------------------------------------------------

// Chame isto DEPOIS de um envio de dados via SARA, quando você souber
// que o supercapacitor foi descarregado e precisa de uma janela de recarga.
// Ex: sleep_request_cap_recharge_window();
void sleep_request_cap_recharge_window(void)
{
    s_cap_recharge_pending = true;
    ESP_LOGI(TAG, "Janela de recarga do supercapacitor SOLICITADA");
}

// Opcional: pode ser chamado no boot ou se quiser cancelar a janela.
void sleep_clear_cap_recharge_window(void)
{
    s_cap_recharge_pending = false;
    ESP_LOGI(TAG, "Janela de recarga do supercapacitor LIMPA");
}

// Para debug / decisões no main, se você quiser saber se a próxima dormida
// vai manter VSYS ligado para recarga.
bool sleep_is_cap_recharge_window_pending(void)
{
    return s_cap_recharge_pending;
}

void set_inactivity(void)
{
    ulp_inactivity = 1;
}




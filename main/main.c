#include <datalogger_control.h>
#include "battery_monitor.h"
#include "sdkconfig.h"
#include "datalogger_driver.h"
#include "esp_log.h"
#include "driver/pcnt.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "nvs.h"

#include <stdio.h>
#include <string.h>

#include"pulse_meter.h"
#include"pressure_meter.h"
#include "sdmmc_driver.h"
#include "TCA6408A.h"
#include "oled_display.h"
#include "main.h"

#include "sara_r422.h"
#include "modbus_rtu_master.h"
#include "system.h"
#include "i2c_dev_master.h"
#include "rele.h"
#include "pulse_meter.h"
#include "sleep_control.h"
#include "led_blink_control.h"

#include "log_mux.h"

#define ENABLE_OTA              0
#define ENABLE_FACTORY_CONFIG   1
#define ENABLE_PCNT             1 //PCNT foi deslocado para outro arquivo
#define ENABLE_DISPLAY          0 // A definição se encontra no oled.display.h

//For Test
#define ENABLE_OTA_BYPASS       0
#define ENABLE_RS485            0
#define ENABLE_RELAY            0

#define ADS1015_ADDRESS 0x48  // Endereço padrão (ADDR conectado a GND)
#define TIME_REFERENCE 1735689601 // 1º de janeiro de 2025, 00:00:01 UTC

extern RTC_DATA_ATTR int stub_executed;
//enum wake_up_cause wkupcause;
extern RTC_DATA_ATTR int gpio27_level;

static const char *TAG = "Main datalogger";

static void check_i2c_bus(void);
static void restore_power_pin_after_wakeup(void);
static void release_rtc_holds(void);

bool first_factory_setup=false;
bool wifi_on = false;
bool wakeup_inactivity=false;

extern xTaskHandle TimeManager_TaskHandle;

/*Causas do wakeup
WAKE_UP_BOOT          -->cause 0
WAKE_UP_TIME          -->cause 1
WAKE_UP_RING          -->cause 2
WAKE_UP_PULSE         -->cause 3
WAKE_UP_EXTERN_SENSOR -->cause 4
WAKE_UP_INACTIVITY    -->cause 5
WAKE_UP_NONE          -->cause 6
*/

/*Causas do Reset
0--> ESP_RST_UNKNOWN,    //!< Reset reason can not be determined
1--> ESP_RST_POWERON,    //!< Reset due to power-on event
2--> ESP_RST_EXT,        //!< Reset by external pin (not applicable for ESP32)
3--> ESP_RST_SW,         //!< Software reset via esp_restart
4--> ESP_RST_PANIC,      //!< Software reset due to exception/panic
5--> ESP_RST_INT_WDT,    //!< Reset (software or hardware) due to interrupt watchdog
6--> ESP_RST_TASK_WDT,   //!< Reset due to task watchdog
7--> ESP_RST_WDT,        //!< Reset due to other watchdogs
8--> ESP_RST_DEEPSLEEP,  //!< Reset after exiting deep sleep mode
9--> ESP_RST_BROWNOUT,   //!< Brownout reset (software or hardware)
10-> ESP_RST_SDIO,       //!< Reset over SDIO
*/

//----------------------------------------------------------
#define GPIO_INPUT_RING     32
#define ESP_INTR_FLAG_DEFAULT 0

//*****************************************************************************************
//  Functions
//*****************************************************************************************
/*
static void deactivate_calibration(void)
{
		struct operation_config restore_calibration;
       vTaskDelay(pdMS_TO_TICKS(10));
		if (has_calibration()|| has_level_zero() || has_correction())
		{
	    get_operation_config(&restore_calibration);
	    vTaskDelay(pdMS_TO_TICKS(80));
		enable_calibration(false);
		enable_level_zero(false);
		enable_correction(false);
		save_operation_config(&restore_calibration);
		vTaskDelay(pdMS_TO_TICKS(80));

		}
		return;
}
*/

/*int get_wakeup_cause(void)
{
	return wkupcause;

}*/

// Função para verificar o estado do barramento I2C

static void check_i2c_bus(void) {
    ESP_LOGI(TAG, "Verificando estado do barramento I2C...");

    // Configura os pinos SDA e SCL como entradas para leitura
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << I2C_MASTER_SDA_IO) | (1ULL << I2C_MASTER_SCL_IO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Lê os níveis lógicos de SDA e SCL
    int sda_level = gpio_get_level(I2C_MASTER_SDA_IO);
    int scl_level = gpio_get_level(I2C_MASTER_SCL_IO);

    ESP_LOGI(TAG, "SDA (GPIO %d) nível: %d", I2C_MASTER_SDA_IO, sda_level);
    ESP_LOGI(TAG, "SCL (GPIO %d) nível: %d", I2C_MASTER_SCL_IO, scl_level);

    if (sda_level == 0 || scl_level == 0) {
        ESP_LOGE(TAG, "Barramento I2C travado! SDA ou SCL em nível baixo.");
        // Tenta resetar o barramento I2C manualmente
        ESP_LOGI(TAG, "Tentando resetar o barramento I2C manualmente...");
        for (int i = 0; i < 9; i++) {
            gpio_set_direction(I2C_MASTER_SCL_IO, GPIO_MODE_OUTPUT);
            gpio_set_level(I2C_MASTER_SCL_IO, 0);
            vTaskDelay(pdMS_TO_TICKS(1));
            gpio_set_level(I2C_MASTER_SCL_IO, 1);
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        // Verifica novamente
        sda_level = gpio_get_level(I2C_MASTER_SDA_IO);
        scl_level = gpio_get_level(I2C_MASTER_SCL_IO);
        ESP_LOGI(TAG, "Após reset: SDA nível: %d, SCL nível: %d", sda_level, scl_level);
    } else {
        ESP_LOGI(TAG, "Barramento I2C parece estar OK.");
    }
}

static void logmux_early_init(void)
{
    // Instala o mux como vprintf do ESP_LOG
    logmux_init(NULL);          // ainda sem writer TCP
    // Duplicação por padrão: mantemos Serial ligada
    logmux_enable_uart(true);
    logmux_enable_tcp(false);   // TCP liga quando o console subir
}


static void restore_power_pin_after_wakeup(void)
{
    gpio_num_t PWR_GPIO = GPIO_NUM_27;

    // 1) Desativa o hold para retomar controle pelo GPIO “normal”
    rtc_gpio_hold_dis(PWR_GPIO);

    // 2) Configura como GPIO de saída (modo “normal”) e seta nível alto
    gpio_reset_pin(PWR_GPIO);
    gpio_set_direction(PWR_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(PWR_GPIO, 1);
    
}

// Função para liberar o hold de todos os pinos que você definiu
static void release_rtc_holds(void) {
    // Liste aqui TODOS os GPIOs que foram colocados em hold
    const gpio_num_t pins_to_unhold[] = {
        GPIO_NUM_26,  // I2C SDA
        GPIO_NUM_32,  // Pulso ULP
        GPIO_NUM_2,   // SDMMC D0
        GPIO_NUM_4,   // SDMMC D1
        GPIO_NUM_12,  // SDMMC D2
        GPIO_NUM_13,  // SDMMC D3
        GPIO_NUM_14,  // SDMMC CLK
        GPIO_NUM_15   // SDMMC CMD
   
        // Se precisar, acrescente mais GPIOs aqui
    };
    const size_t count = sizeof(pins_to_unhold) / sizeof(pins_to_unhold[0]);
    for (size_t i = 0; i < count; i++) {
        rtc_gpio_hold_dis(pins_to_unhold[i]);
    }
}


//*****************************************************************************************
//   Main Block
//*****************************************************************************************

void app_main(void)
{
//	set_cpu_freq_rtc(80); // Definir frequência para 80 MHz
	restore_power_pin_after_wakeup();
//	vTaskDelay(pdMS_TO_TICKS(300));
	release_rtc_holds();
	wake_up_cause_t wkupcause = check_wakeup_cause();
	ulp_system_stable = 1;
	
	logmux_early_init();
	esp_err_t ret;

ESP_LOGI(TAG, "Frequência inicial ajustada para 80 MHz para reduzir consumo de corrente");
	init_queue_notification();
	// Inicializa o barramento I2C
	
	// Verifica o estado do barramento I2C antes de inicializar
    check_i2c_bus();
    ret = init_i2c_master();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar o I2C");
        return;
    }
    
    ESP_LOGI(TAG, "Boot cause = %d esp_reset reason = %d", wkupcause, esp_reset_reason());
    ESP_LOGI(TAG, "VERSÃO OTA: %s", get_ota_version());
    
    // Inicializa o TCA6408A
    ret = init_tca6408a();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar o TCA6408A");
        return;
    }
    else{
		 activate_mosfet(disable_analog_sensors);
		}
     
    init_system();
    mount_driver();
    server_comm_init(); // Inicializa o mutex recIndexMutex
//    esp_task_wdt_init(30, true);
    init_config_control();
    
    battery_monitor_init(false);
 //   calibrate_power_source(7.19f); // ajusta e salva se cololcar battery_monitor_init =true
     
    mount_sd_card();

     blink_init();
     

/*
    listFilesInDir();
    littlefs_read_file_with_content("/littlefs/pressure_data_set.json");
*/

#if !CONFIG_ESP_TASK_WDT_INIT
    // If the TWDT was not initialized automatically on startup, manually intialize it now
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = TWDT_TIMEOUT_MS,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,    // Bitmask of all cores
        .trigger_panic = false,
    };
    ESP_ERROR_CHECK(esp_task_wdt_init(&twdt_config));
    printf("TWDT initialized\n");
#endif // CONFIG_ESP_TASK_WDT_INIT

    ESP_LOGI(TAG, "!!!RAM left %d", esp_get_free_heap_size());
    
    
#if ENABLE_RS485
    init_rs485_log_console();
#endif


#if ENABLE_RELAY == 1
    rele_init();

#endif


//*********************************************
//   Atualização da data e hora
//*********************************************
 
    if(wkupcause == WAKE_UP_BOOT||wkupcause==WAKE_UP_INACTIVITY||wkupcause==WAKE_UP_NONE)
        {
            struct timeval last_time = { .tv_sec = get_last_sys_time() };
            printf(">>>>>>>>>>>>>>Last TIME = %lld\n", last_time);
            settimeofday(&last_time, NULL);
        }

//-----------------------------------------------------------------------------------------
    if(!has_factory_config())
      {	//set_cpu_freq_rtc(160);
        wifi_on=true;
        init_factory_task();
        first_factory_setup=true;
       }    
    else{
            switch(wkupcause)
            {
             case WAKE_UP_BOOT :
            	  printf("-->Wakeup Boot\n");
            	 // set_cpu_freq_rtc(160);
            	  init_pulse_meter_task();
            	  init_timemanager_task();
            	  wifi_on=true;
            	  init_factory_task();         	     
        	          
             break;

             case WAKE_UP_TIME :
            	  printf("-->Wakeup Time\n");
            	  init_pulse_meter_task();
            	  init_timemanager_task();

              break;

              case WAKE_UP_RING :
            	   printf("-->Wakeup Ring\n");
            	 //  set_cpu_freq_rtc(160);
            	   init_pulse_meter_task();
            	   wifi_on=true;
            	   init_factory_task();
            	   init_timemanager_task();
  		           
              	   
              break;

/*            case WAKE_UP_PULSE :
               	   printf("-->Wakeup Pulse\n");
               	   init_timemanager_task();
               	   init_factory_task();//Só para teste
                   init_pulse_meter_task();
               	   set_last_pulse_time();
               	   set_last_pulse_milisec();

              break;
 */               
              case WAKE_UP_EXTERN_SENSOR :
               	   printf("-->Wakeup EXTERN SENSOR\n");
               	 //  set_cpu_freq_rtc(160);
               	   init_pulse_meter_task();
               	   wifi_on=true;
               	   init_factory_task();
               	   init_timemanager_task();
               	   
              break;        

              case WAKE_UP_INACTIVITY :
            	   printf("------------> Wakeup Inactivity <-------------\n");
            	   wakeup_inactivity=true;
                   if (TimeManager_TaskHandle == NULL) {
            			init_timemanager_task();
            		    }else
            		         {
            		    	  start_deep_sleep();
            		          }

              break;

              case WAKE_UP_NONE :
      	   	       printf("&&&&&&&&&&&&&&&& WAKE UP NONE &&&&&&&&&&&&&&\n");
               	   start_deep_sleep();
              break;

              default : printf("Wakeup was not caused by deep sleep: %d\n",wkupcause);
              start_deep_sleep();
              break;
             }
         }   
}
//-----------------------------------------------------------------------------------------












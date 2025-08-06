/*
 * main_control.c
 *
 *  Created on: 18 de out. de 2024
 *      Author: geopo
 */
 
#include "TCA6408A.h"
#include "battery_monitor.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "main.h"
#include "time.h"
#include <sys/time.h>
#include <datalogger_control.h>
#include "datalogger_driver.h"

#include"pulse_meter.h"
#include"pressure_meter.h"
#include "sdmmc_driver.h"

#include "oled_display.h"
#include "sara_r422.h"
#include "comm_wifi.h"
#include "nvs_flash.h"
#include "4G_network.h"
#include "system.h"

#include "esp_log.h"
#include "sleep_control.h"
#include "led_blink_control.h"

xTaskHandle TimeManager_TaskHandle = NULL;

static const char *TAG = "Main_Control";

#define UPDATE_SYSTEM_TIME      12       //Hora para atualizar o rel�gio interno do ESP32
#define WKUP_BOOT 0
#define WKUP_RING 2

#define ENABLE_DEEP_SLEEP       1
#define ENABLE_SLEEP_MODE_PULSE_CNT        1

#define ENABLE_DISPLAY       0

#define SAVE_OK                    0
#define SAVE_PRESSURE_1_FAILED    (1 << 0)
#define SAVE_PULSE_FAILED         (1 << 1)
#define SAVE_PRESSURE_2_FAILED    (1 << 2)

extern uint32_t ulp_inactivity;
extern bool factory_task_ON;
extern bool wakeup_inactivity;
extern bool ap_active;
extern bool wifi_on;
//----------------------------------------
QueueHandle_t xQueue_NetConnect;
QueueHandle_t xQueue_Factory_Control;
QueueHandle_t xQueue_get_network_time;
bool Receive_NetConnect_Task_ON=false;
bool Receive_FactoryControl_Task_ON = false;
bool Receive_Get_Network_Time_Task_ON = false;
bool Send_Response_Factory_Control=false;
//----------------------------------------

void TimeManager_Task(void* pvParameters);
static void save_last_processed_minute(int minute);
static int load_last_processed_minute(void);
static void lte_send_data_to_server(void);
static uint8_t save_sensor_data(void);
//static void update_system_time(void);

/*void init_sensor_pwr_supply(void)
{

	    activate_mosfet(0);

}*/
//------------------------------------
//Função somente de teste
static void force_ap_down_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(30000));  // aguarda 5 s
    ESP_LOGW("TEST", "Auto-test: forçando queda do SoftAP (modo STA-only)");
    ap_active = false;
    // só troca o modo, sem parar o driver nem derrubar o HTTP server
    esp_wifi_set_mode(WIFI_MODE_STA);
    vTaskDelete(NULL);
}

//------------------------------------

static void save_last_processed_minute(int minute)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_set_i32(handle, "last_minute", minute);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

static int load_last_processed_minute(void)
{
    nvs_handle_t handle;
    int32_t minute = -1; // Valor padrão
    esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        nvs_get_i32(handle, "last_minute", &minute);
        nvs_close(handle);
    }
    return minute;
}


static bool has_passed_midnight(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    setenv("TZ", "GMT+3", 1);
    tzset();

    localtime_r(&now, &timeinfo);

    timeinfo.tm_hour = 0;
    timeinfo.tm_min = 0;
    timeinfo.tm_sec = 0;

    time_t midnight = mktime(&timeinfo);

    return (now - midnight) <= 80;
}

static uint8_t save_sensor_data(void)
{
printf("+++++>>> Vai Gravar dados <<<+++++\n");
activate_mosfet(enable_analog_sensors);
bool pressure_sensor_1 = false;
bool pressure_sensor_2 = false;
uint8_t result = SAVE_OK;

	pressure_sensor_read(&pressure_sensor_1, &pressure_sensor_2);

    if (pressure_sensor_1)
    {
		if(save_pressure_measurement(0)==ESP_OK){
		    printf("Dados de Pressão_1 Salvos!!!\n");
           
        }else
		     {
			  printf("Dados de Pressão do sensor 1 não foram salvos\n");
			  result |= SAVE_PRESSURE_1_FAILED;
		      }
	}
	
//	pulse_meter_config_init();
	#if ENABLE_SLEEP_MODE_PULSE_CNT
	
	  if(save_pulse_measurement(1)==ESP_OK){
		   printf("Dados de pulsos Salvos!!!\n");
		    result |= SAVE_PULSE_FAILED;
	     }else
		      {
			    printf("Dados de pulsos não salvos\n");
		      }
	    
    #endif
    
      if (pressure_sensor_2)
    {
		if(save_pressure_measurement(2)==ESP_OK)
          {
		    printf("Dados de Pressão_2 Salvos!!!\n");
           }
        else
	        {
			  printf("Dados de Pressão do sensor 2 não foram salvos\n");
			  result |= SAVE_PRESSURE_2_FAILED;
		    }
	}
	
	activate_mosfet(disable_analog_sensors);
	   return result;
}

static void lte_send_data_to_server(void){

	if (is_send_mode_freq()){
		if(get_send_period()<=60){
			if (get_time_minute() % get_send_period()==0)
			   {
				   activate_mosfet(enable_sara);
				   init_LTE_System(); 
			   }
		   } else if((get_time_hour()%(get_send_period()/60)==0)&& (get_time_minute()<1)){
			
			         activate_mosfet(enable_sara);
			         init_LTE_System();
		            }
	   }
	   
	 if (is_send_mode_time()&&(get_time_minute()<1)){
		   
	    if(get_time_hour()==get_send_time1()||get_time_hour()==get_send_time2()||get_time_hour()==get_send_time3()||get_time_hour()==get_send_time4()){
		  activate_mosfet(enable_sara);
		  init_LTE_System();
		  }
       }
}
void init_queue_notification(void)
{
  xQueue_Factory_Control = xQueueCreate(2, sizeof(Receive_FactoryControl_Task_ON));
	
		if( xQueue_Factory_Control == 0 )
      {
       printf("Failed to create xQueue_Factory_Control= %p \n",xQueue_Factory_Control); // Failed to create the queue.
       }
	
  xQueue_NetConnect = xQueueCreate(2, sizeof(Receive_NetConnect_Task_ON)); 
	if( xQueue_NetConnect == 0 )
      {
       printf("Failed to create xQueue_NetConnect= %p \n",xQueue_NetConnect); // Failed to create the queue.
       }
       
  xQueue_get_network_time = xQueueCreate(2, sizeof(Receive_Get_Network_Time_Task_ON)); 
	if( xQueue_get_network_time == 0 )
      {
       printf("Failed to create xQueue_NetConnect= %p \n",xQueue_get_network_time); // Failed to create the queue.
       
       }
}

/*static void update_system_time(void) {
	
    // Obter o tempo atual do sistema
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    time_t nowtime = tv_now.tv_sec;
    ESP_LOGI(TAG, ">>>>>>>>>>>>>> Current TIME = %lld", (long long)nowtime);

    // Obter o último tempo salvo
    struct timeval last_time = { .tv_sec = get_last_sys_time(), .tv_usec = 0 };
    ESP_LOGI(TAG, ">>>>>>>>>>>>>> Last TIME = %lld", (long long)last_time.tv_sec);

    // Verificar se o tempo atual é inválido e o último tempo salvo é válido
    if (last_time.tv_sec > nowtime ) {
        settimeofday(&last_time, NULL);
        ESP_LOGI(TAG, "RTC atualizado com last_time: %lld", (long long)last_time.tv_sec);

    } else {
        ESP_LOGI(TAG, "NÃO PRECISA ATUALIZAR O TIME SYSTEM");
        }
 
}
            */
//------------------------------------------------
//  Gerencia o Datalogger
//------------------------------------------------
void TimeManager_Task(void* pvParameters)
{ 
 blink_set_profile(BLINK_PROFILE_DEVICE_RUNNING);
 battery_monitor_update();
// Inicializa o NVS 
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    
    time_t now, keepAlive;
    
   	struct tm timeinfo;//Cria a estrutura que contem as informacoes da data.
//   	uint8_t periodo = get_send_period();

/*   	if (get_time_minute()==0)
   	{
   		set_last_minute(60);
   	}*/

//	esp_task_wdt_add(NULL);
bool flag=false;
uint8_t counter=0;

 if ((ulp_inactivity & UINT16_MAX) == 1)
	 {
		 activate_mosfet(enable_sara);
		 //cell_get_local_time();
	 }
	 
//Somente para teste
//=====================================	 
/*	  xTaskCreate(
        force_ap_down_task,    // função
        "force_ap_down",       // nome da task
        2048,                  // tamanho da stack
        NULL,                  // parâmetro (unused)
        tskIDLE_PRIORITY + 1,  // prioridade
        NULL                   // handle (unused)
    );*/
//=====================================	 
while(1)
{
/*float vbat = battery_monitor_get_voltage();          // bateria em volts
float vsource = get_power_source_volts();            // fonte em volts
uint16_t batt_pct = get_battery();                  // % da bateria (0..100)
float soc = battery_monitor_get_soc();               // 0..1

ESP_LOGI(TAG, "Bateria: %.2fV (%u%%, soc=%.2f), Fonte: %.2fV",
         vbat, (unsigned)batt_pct, soc, vsource);*/
       
	vTaskDelay(pdMS_TO_TICKS(1000));
	
//----------------------------------------------------------
//Show Display
//----------------------------------------------------------
 #if ENABLE_DISPLAY
// printf ( "Display Enabled \n");
if ((get_wakeup_cause() == WAKE_UP_EXTERN_SENSOR)&& counter<=7)
{
	counter++;
   if (!flag)
   {
	   write_logo_LWS();
	   sensor_data_on_display(); 
	 	flag=true;   
   }
   if (counter==7)
     {
		clear_display(); 
	 }
 }
 #endif
 
//----------------------------------------------------------
//        Keep working even if awake
//----------------------------------------------------------
if ((get_time_minute() % get_deep_sleep_period()==0) && (get_time_minute()!=load_last_processed_minute())&&has_device_active())// acrescentado para não enviar duas vezes
   {
	save_last_processed_minute(get_time_minute());  
    
	uint8_t save_ret = save_sensor_data();
	if (save_ret != SAVE_OK) {
    ESP_LOGW(TAG, "save_sensor_data falhou com máscara 0x%02x", save_ret);
}
	
  if(has_measurement_to_send()&&!Receive_NetConnect_Task_ON &&!wifi_on&&(has_network_http_enabled()||has_network_mqtt_enabled()))
	 {
           printf(">>>>Tem dados para enviar<<<\n");
           lte_send_data_to_server();
	     }
    
    if (!ap_active)
    {wifi_on=false;}
    
//        Send Keep Alive
//-----------------------------------------------------------
  time(&now);
  get_keep_alive_time(&timeinfo);
  keepAlive = mktime(&timeinfo);

   }//finish periodic

//***********************************************************

xQueueReceive(xQueue_NetConnect, &Receive_NetConnect_Task_ON , (TickType_t)5);
xQueueReceive(xQueue_get_network_time, &Receive_Get_Network_Time_Task_ON , (TickType_t)5);
#if ENABLE_DEEP_SLEEP
xQueueReceive(xQueue_Factory_Control, &Receive_FactoryControl_Task_ON , (TickType_t)5);	
/*
printf("Factory task on = %d ####  NetConnect = %d #### Receive_Get_Network_Time =%d \n ",
       Receive_FactoryControl_Task_ON, Receive_NetConnect_Task_ON,Receive_Get_Network_Time_Task_ON);
 */      
if (!Receive_FactoryControl_Task_ON && !Receive_NetConnect_Task_ON &&!Receive_Get_Network_Time_Task_ON)
   {
	vTaskDelay(pdMS_TO_TICKS(500));
//	 turn_off_sara();
 	start_deep_sleep();
 	}

#endif

//   esp_task_wdt_reset();
   }//end loop
   
}

void init_timemanager_task(void)
{
 if(TimeManager_TaskHandle==NULL)

   xTaskCreatePinnedToCore( TimeManager_Task, "TimeManager_Task", 10000, NULL, 1, &TimeManager_TaskHandle,1);


}




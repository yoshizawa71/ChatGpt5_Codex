/*
 * main_control.c
 *
 *  Created on: 18 de out. de 2024
 *      Author: geopo
 */
 
#include "TCA6408A.h"
#include "battery_monitor.h"
#include "energy_meter.h"
#include "rs485_central.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_publisher.h"
#include "main.h"
#include "reboot_test.h"
#include "time.h"
#include <sys/time.h>
#include <datalogger_control.h>
#include "sara_r422.h"
#include "tcp_log_server.h"
#include "datalogger_driver.h"

#include"pulse_meter.h"
#include"pressure_meter.h"
#include "sdmmc_driver.h"

#include "oled_display.h"
#include "nvs_flash.h"
#include "4G_network.h"
#include "system.h"

#include "esp_log.h"
#include "sleep_control.h"
#include "led_blink_control.h"

#include "tcp_log_server.h"  
#include "mqtt_publisher.h"
#include "wifi_link.h"
#include "wifi_softap_sta.h"
#include "esp_netif.h"
#include <lwip/netdb.h>
#include "portal_state.h"
#include "sdkconfig.h"

#include "payload_time.h"

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

#ifndef SAVE_ENERGY_FAILED
#define SAVE_ENERGY_FAILED   (1u << 3)  // use um bit livre; ajuste se 1<<3 já estiver em uso
#endif


// Guard de sessão sem mutex/alocação
static volatile bool s_send_in_progress = false;
static portMUX_TYPE s_send_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_last_send_ticks = 0;   // (opcional) debounce temporal

extern uint32_t ulp_inactivity;

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
static bool console_tcp_start = false;

//----------------------------------------

void TimeManager_Task(void* pvParameters);
static void save_last_processed_minute(int minute);
static int load_last_processed_minute(void);
static void lte_send_data_to_server(void);
static void wifi_send_data_to_server(void);
static uint8_t save_sensor_data(void);
//static void update_system_time(void);

// ====== VALIDAÇÃO FIXA (desligue depois) ======
#define ENERGY_VALIDATE_FIXED   0
#define ENERGY_FIX_CHANNEL      3
#define ENERGY_FIX_ADDRESS      1
// ==============================================
// ---- Gancho fraco: estado do portal/factory (será sobreposto por portal_state.c se presente)
__attribute__((weak)) bool factory_portal_active(void) {
    return false; // fallback: assume headless
}

// ---- Fallback fraco de bring-up STA (será sobreposto por wifi_link.c se presente)
__attribute__((weak)) esp_err_t wifi_link_ensure_ready_sta(int timeout_ms, bool force_sta_only)
{
    // Reaproveita seus utilitários existentes
    wifi_mode_t m = WIFI_MODE_NULL;
    esp_wifi_get_mode(&m);

    if (m != WIFI_MODE_STA && m != WIFI_MODE_APSTA) {
        esp_err_t err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

        err = esp_netif_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

        if (start_wifi_ap_sta() != ESP_OK) {
            ESP_LOGW("WiFi Fallback", "Falha ao iniciar WiFi.");
            return ESP_FAIL;
        }
    }

    if (force_sta_only) {
        wifi_ap_force_disable(); // garante STA-only no modo headless
    }

    // Espera GOT_IP até timeout_ms
    TickType_t t0 = xTaskGetTickCount();
    esp_netif_ip_info_t ip;
    while (true) {
        memset(&ip, 0, sizeof(ip));
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0) {
            ESP_LOGI("WiFi Fallback", "GOT_IP: %s", ip4addr_ntoa((const ip4_addr_t*)&ip.ip));
            return ESP_OK;
        }
        if ((xTaskGetTickCount() - t0) > pdMS_TO_TICKS(timeout_ms)) {
            ESP_LOGW("WiFi Fallback", "Timeout aguardando GOT_IP.");
            return ESP_ERR_TIMEOUT;
        }
//        vTaskDelay(pdMS_TO_TICKS(200));
    }
}


//------------------------------------

// Opcional: se você tem um httpd rodando, registra GET /logs para visualizar
// o ring buffer de logs no navegador (http://<ip>/logs)
static void console_tcp_register_http_logs(void *httpd_handle)
{
    if (httpd_handle) {
        ESP_ERROR_CHECK( tcp_log_register_http_endpoint(httpd_handle, "/__logs") );
        ESP_LOGI("CONSOLE", "HTTP /logs endpoint enabled");
    }
}

//----------------------------------------
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
//printf("+++++>>> Vai Gravar dados <<<+++++\n");
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

// ---------------------------------------------------------------------
// RS-485 (Energia): salva correntes dos medidores cadastrados.
// Usa o registry (canal -> endereço e nº de fases).
// Se não houver nenhum cadastrado (ESP_ERR_NOT_FOUND), NÃO é erro.
// ---------------------------------------------------------------------
static void save_sensor_data_rs485(void)
{
#if ENERGY_VALIDATE_FIXED
    ESP_LOGI("RS485", "VALIDAÇÃO: ler CH=%d ADDR=%d (fixo)", ENERGY_FIX_CHANNEL, ENERGY_FIX_ADDRESS);
    esp_err_t e = energy_meter_save_currents(ENERGY_FIX_CHANNEL, ENERGY_FIX_ADDRESS);
    if (e == ESP_OK) {
        ESP_LOGI("RS485", "VALIDAÇÃO: gravação OK (3.1/3.2/3.3)");
    } else {
        ESP_LOGW("RS485", "VALIDAÇÃO: falha ao gravar (fixo): %s", esp_err_to_name(e));
    }
#else
    esp_err_t e = energy_meter_save_registered_currents();
    if (e == ESP_OK) {
        ESP_LOGI("RS485", "Energy: dados salvos a partir do cadastro.");
    } else if (e == ESP_ERR_NOT_FOUND) {
        ESP_LOGW("RS485", "Nenhum medidor RS485 cadastrado.");
    } else {
        ESP_LOGW("RS485", "Falha ao salvar energia: %s", esp_err_to_name(e));
    }
#endif
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

static void wifi_send_data_to_server(void)
{
    // ----------------- Guard de sessão (sem mutex) -----------------
    bool already_sending;
    portENTER_CRITICAL(&s_send_mux);
    already_sending = s_send_in_progress;
    if (!already_sending) s_send_in_progress = true;
    portEXIT_CRITICAL(&s_send_mux);

    if (already_sending) {
        ESP_LOGW(TAG, "Envio já em progresso; ignorando novo trigger.");
        return;
    }

    // (Opcional) Debounce de 15s para qualquer fonte ruidosa
    uint32_t now = xTaskGetTickCount();
    if (s_last_send_ticks && (now - s_last_send_ticks) < pdMS_TO_TICKS(15000)) {
        ESP_LOGW(TAG, "Debounce envio (15s): ignorando.");
        portENTER_CRITICAL(&s_send_mux);
        s_send_in_progress = false;
        portEXIT_CRITICAL(&s_send_mux);
        return;
    }
    s_last_send_ticks = now;

    // ---------- BOOST (160 MHz) ----------
    cpu_freq_guard_t _g;
    cpu_freq_guard_enter(&_g, 160);
    // (por decisão sua, NÃO chamaremos cpu_freq_guard_exit(&_g);)

    // Levanta “rede ativa” para bloquear deep-sleep durante o envio
    Receive_NetConnect_Task_ON = true;
    if (xQueue_NetConnect) {
        bool v = true;
        xQueueSend(xQueue_NetConnect, &v, 0);
    }

    // ----------------- Garante Wi-Fi pronto (STA) -----------------
    // Se o portal/factory estiver ativo, mantemos AP+STA; se headless, forçamos STA-only.
    bool sta_only = !factory_portal_active();
    if (wifi_link_ensure_ready_sta(20000 /*ms*/, sta_only) != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi não ficou pronto; abortando envio.");
        goto done;
    }

    // ----------------- (Opcional) Pré-resolve DNS do destino -----------------
    // Mantemos seu pré-resolve apenas para MQTT; HTTP resolve dentro do esp_http_client.
    if (has_network_mqtt_enabled()) {
        const char *host = get_mqtt_url();
        if (!host || !host[0]) {
            ESP_LOGE(TAG, "Host MQTT vazio; abortando.");
            goto done;
        }
        bool resolved = false;
        for (int i = 0; i < 10; ++i) { // 10 * 500 ms ~ 5 s
            struct addrinfo *ai = NULL;
            if (getaddrinfo(host, NULL, NULL, &ai) == 0 && ai) {
                freeaddrinfo(ai);
                resolved = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        if (!resolved) {
            ESP_LOGW(TAG, "DNS não respondeu para '%s'; abortando envio.", host);
            goto done;
        }
    }

    // ----------------- Publica (1x) -----------------
    // Prioriza MQTT se ambos estiverem habilitados (mantenho sua política típica).
    if (has_network_mqtt_enabled()) {
        mqtt_wifi_publish_now();   // internamente atualiza índices se sucesso
    } else if (has_network_http_enabled()) {
        http_wifi_publish_now();   // idem
    } else {
        ESP_LOGW(TAG, "Nenhum protocolo de aplicação habilitado (MQTT/HTTP).");
    }

done:
    // NÃO baixamos a CPU aqui (você decidiu manter em 160MHz até o deep sleep)

    // Baixa “rede ativa”
    Receive_NetConnect_Task_ON = false;
    if (xQueue_NetConnect) {
        bool v = false;
        xQueueSend(xQueue_NetConnect, &v, 0);
    }

    // Libera o guard
    portENTER_CRITICAL(&s_send_mux);
    s_send_in_progress = false;
    portEXIT_CRITICAL(&s_send_mux);
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
		 //activate_mosfet(enable_sara);
		// cell_get_local_time();
		Receive_FactoryControl_Task_ON=false;
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
	vTaskDelay(pdMS_TO_TICKS(1000));
/*float vbat = battery_monitor_get_voltage();          // bateria em volts
float vsource = get_power_source_volts();            // fonte em volts
uint16_t batt_pct = get_battery();                  // % da bateria (0..100)
float soc = battery_monitor_get_soc();               // 0..1

ESP_LOGI(TAG, "Bateria: %.2fV (%u%%, soc=%.2f), Fonte: %.2fV",
         vbat, (unsigned)batt_pct, soc, vsource);*/
       
	
	
/*	printf("AP Active = %d e modbus start = %d\n", ap_active, console_tcp_start);
	if (ap_active && !console_tcp_start)
	{
		printf(">>>>START CONSOLE TCP<<<<\n");
        console_tcp_enable(3333);
        ESP_LOGI("SELFTEST", "Hello TCP! tick=%u", (unsigned)esp_log_timestamp());
		console_tcp_start = true;
	}*/
	
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
	
    ESP_LOGI("Save Sensor Data", "TESTE: vou chamar save_sensor_data()");
    
 //===============================
//  Leitores do sensor interno
//-------------------------------   
	uint8_t save_ret = save_sensor_data();
	if (save_ret != SAVE_OK) {
    ESP_LOGW(TAG, "save_sensor_data falhou com máscara 0x%02x", save_ret);
}

//===============================
//  Leitores dos sensores RS485 externos
//-------------------------------
#if CONFIG_MODBUS_SERIAL_ENABLE
    rs485_central_poll_and_save(5000);
#endif
	
  if(has_measurement_to_send()&&(ulp_inactivity & UINT16_MAX) != 1)
	 {
		 if (has_network_http_enabled()||has_network_mqtt_enabled()){
			 
			 if (has_activate_sta()){
			     wifi_send_data_to_server();
			     }else{
				       if (!Receive_NetConnect_Task_ON &&!wifi_on){
					   lte_send_data_to_server();
				      }
				  } 
		     }
//       printf(">>>>Tem dados para enviar<<<\n");
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
	vTaskDelay(pdMS_TO_TICKS(10));
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




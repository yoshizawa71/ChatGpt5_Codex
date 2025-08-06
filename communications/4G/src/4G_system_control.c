#include "datalogger_control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_blink_control.h"
#include "main.h"
#include "sara_r422.h"
#include "u_cell_sms.h"
#include "system.h"
#include "esp_log.h"
#include "TCA6408A.h"
#include "u_device.h"

# ifdef U_CFG_OVERRIDE
#  include "u_cfg_override.h" // For a customer's configuration override
# endif

static const char *TAG = "4G_SYSTEM_CONTROL";

xTaskHandle LTE_System_TaskHandle = NULL;
extern xTaskHandle network_time_TaskHandle;

extern QueueHandle_t xQueue_NetConnect;

bool Send_NetConnect_Task_ON=false;
static void deinit_LTE_System(void);
//static void cell_Net_Connection_Control(void);
 static void server_connection_control(void);

uDeviceHandle_t devHandle = NULL;

esp_err_t turn_off_sara(uAtClientHandle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "AT handle é NULL — módulo já pode estar desligado");
        return ESP_FAIL;
    }

    uAtClientLock(handle);
    uAtClientCommandStart(handle, "AT+CPWROFF");
    uAtClientCommandStop(handle);
    uAtClientResponseStart(handle, NULL);
    uAtClientResponseStop(handle);
    uAtClientUnlock(handle);

    ESP_LOGI(TAG, "Comando AT+CPWROFF enviado.");
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "Alimentação do SARA desligada.");
    return ESP_OK;
}

uCellNetStatus_t cell_Net_Connection_Control(void)
{
	int32_t err;
	uCellNetStatus_t net_status;
	blink_set_profile(BLINK_PROFILE_COMM_START);
	
	err = cell_Net_Register_Connect(&devHandle);
    if (err < 0) {
        printf("Error Code =>>%d\n",err);
    }
    cellSmsInit(devHandle);
    net_status = uCellNetGetNetworkStatus(devHandle, U_CELL_NET_REG_DOMAIN_PS);
    printf(">>>>> Status PS = %d\n", net_status);
    
    return net_status;
    
    /*for(int8_t i=0; i<3; i++)
   {
	   if(cellNetConnect()==U_CELL_NET_STATUS_REGISTERED_HOME)
	     {
		 break;  
	     }
	   else {
		     printf(">>>Tentativa %d\n", i+1);
              }
   }*/
 
 } 
 
static void server_connection_control(void)
 {	
	 bool delivery=false;
	  // Usar HTTP se habilitado
    if (has_network_http_enabled()) {
        printf(">>>>>>> HTTP Client <<<<<<<\n");
        delivery = ucell_Http_connection(devHandle);
        printf(">>>Error = %d\n",cellNet_Close_CleanUp(devHandle));
    }

    // Usar MQTT se habilitado
    if (has_network_mqtt_enabled()) {
        printf(">>>>>>> MQTT Client <<<<<<<\n");
        delivery = ucell_MqttClient_connection (devHandle); 
        printf(">>>Error = %d\n",cellNet_Close_CleanUp(devHandle));   
    }
    
    printf("DELIVERY ====>>> %d\n", delivery);
 //   blink_set_profile(BLINK_PROFILE_NONE);
 }  

static void LTE_System_Task (void* pvParameters)                
{

//DevLteConfig_init ();//Verificar se precisa e melhorar

uCellNetStatus_t net_status = cell_Net_Connection_Control();

if (net_status==U_CELL_NET_STATUS_REGISTERED_HOME || net_status==U_CELL_NET_STATUS_REGISTERED_ROAMING) {
	server_connection_control();
}

/*for (;;){
	
	if(uCellNetGetNetworkStatus(devHandle, U_CELL_NET_REG_DOMAIN_PS)==U_CELL_NET_STATUS_REGISTERED_HOME){
		
	}
	vTaskDelay(pdMS_TO_TICKS(1000));
}*/

  Send_NetConnect_Task_ON=false;
  xQueueSend(xQueue_NetConnect,(void *)&Send_NetConnect_Task_ON, (TickType_t)0);
  deinit_LTE_System();
  
   
}

void init_LTE_System(void)
{
	set_cpu_freq_rtc(80);
	if (LTE_System_TaskHandle == NULL && network_time_TaskHandle==NULL)
	   {
		Send_NetConnect_Task_ON = true;
        xQueueSend(xQueue_NetConnect,(void *)&Send_NetConnect_Task_ON, (TickType_t)0);
        xTaskCreatePinnedToCore( LTE_System_Task, "LTE_System_Task", 15000, NULL, 1, &LTE_System_TaskHandle,1);
       }
 else {
          Send_NetConnect_Task_ON = false;
          xQueueSend(xQueue_NetConnect,(void *)&Send_NetConnect_Task_ON, (TickType_t)0);
          }
}

static void deinit_LTE_System(void)
{
  vTaskDelete(LTE_System_TaskHandle);
  LTE_System_TaskHandle = NULL;

}

 
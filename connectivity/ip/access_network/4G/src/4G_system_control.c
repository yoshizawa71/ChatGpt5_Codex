#include "battery_monitor.h"
#include "driver/rtc_io.h"
#include "sara_r422.h"
#include "u_cell_sms.h"
#include "datalogger_control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_blink_control.h"
#include "main.h"
#include "system.h"
#include "esp_log.h"
#include "TCA6408A.h"
#include "u_cell_power_strategy.h" 
#include "u_device.h"
#include "sleep_control.h"

# ifdef U_CFG_OVERRIDE
#  include "u_cfg_override.h" // For a customer's configuration override
# endif

static const char *TAG = "4G_SYSTEM_CONTROL";

xTaskHandle LTE_System_TaskHandle = NULL;
extern xTaskHandle network_time_TaskHandle;

extern QueueHandle_t xQueue_NetConnect;

extern int32_t cell_OpenDevice_NoReg(uDeviceHandle_t *pDevHandle);

bool Send_NetConnect_Task_ON=false;
static void deinit_LTE_System(void);
//static void cell_Net_Connection_Control(void);
 static void server_connection_control(void);

uDeviceHandle_t devHandle = NULL;

static void lte_dtr_wake_before_open(void)
{
    gpio_reset_pin(GPIO_NUM_33);
    gpio_set_direction(GPIO_NUM_33, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_33, 0);   // acorda o Sara
    // se quiser, tira hold do RTC aqui também
    rtc_gpio_init(GPIO_NUM_33);
    rtc_gpio_set_direction(GPIO_NUM_33, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(GPIO_NUM_33, 0);
}

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
     bool keep_registered = false;   // se PSM ou DTR funcionarem, não vamos fazer cleanup
     int32_t err;
	  // 1 Usar HTTP se habilitado
    if (has_network_http_enabled()) {
        printf(">>>>>>> HTTP Client <<<<<<<\n");
        delivery = ucell_Http_connection(devHandle);
 
    }

    // 2 Usar MQTT se habilitado
    if (has_network_mqtt_enabled()) {
        printf(">>>>>>> MQTT Client <<<<<<<\n");
        delivery = ucell_MqttClient_connection (devHandle); 

    }
    
    // 3) se entregou, tenta economizar
    if (delivery) {
        // 3.1 tenta PSM 3GPP (oficial) primeiro
        err = lte_power_apply(devHandle,
                              LTE_PWR_STRATEGY_3GPP_FIRST,   // <- mudou aqui
                              LTE_PWR_ACTIVE_TIME_DEFAULT_SEC,
                              NULL);
        if (err == 0) {
            keep_registered = true;
            ESP_LOGI(TAG, "PSM 3GPP solicitado, mantendo contexto.");
        } else {
        ESP_LOGW(TAG, "Falha ao solicitar PSM 3GPP (err=%ld). Tentando desligar totalmente.",
                 (long)err);

        // 3.2 segunda opção: desligar completamente (cleanup)
        int32_t cle = cellNet_Close_CleanUp(devHandle);
        ESP_LOGI(TAG, "cellNet_Close_CleanUp() retornou %ld.", (long)cle);

        if (cle == 0) {
            // Cleanup OK: não mantém registro, modem será desligado via hardware
            keep_registered = false;
            ESP_LOGI(TAG, "Cleanup concluído com sucesso, modem pode ser desligado pelo GPIO.");
        } else {
            ESP_LOGW(TAG, "Cleanup falhou (err=%ld). Tentando fallback UPSV/DTR.",
                     (long)cle);

            // 3.3 terceira opção: UPSV/DTR como último recurso
            int32_t upsv_state = -1;
            err = lte_uart_psm_enable(devHandle, true, &upsv_state);
            if (err == 0) {
                keep_registered = true;
                ESP_LOGI(TAG,
                         "UPSV/DTR ativado (UPSV=%ld), mantendo contexto como último recurso.",
                         (long)upsv_state);
            } else {
                ESP_LOGW(TAG,
                         "Falha também no UPSV/DTR (err=%ld). Modem ficará ativo até corte de alimentação.",
                         (long)err);
            }
        }
    }
} else {
    // Sem entrega, não faz sentido manter contexto -> vai direto para cleanup
    int32_t cle = cellNet_Close_CleanUp(devHandle);
    ESP_LOGI(TAG, "Sem entrega: cellNet_Close_CleanUp() retornou %ld.", (long)cle);
    keep_registered = false;
}

    printf("DELIVERY ====>>> %d\n", delivery);
}

int32_t lte_open_for_sms_if_needed(void)
{
    if (devHandle != NULL) {
        // Já está aberto (vindo de outro uso: HTTP/MQTT ou registro anterior)
        return 0;
    }

 int32_t err = cell_OpenDevice_NoReg(&devHandle);
    if (err < 0) {
        ESP_LOGE(TAG,
                 "lte_open_for_sms_if_needed: falha ao abrir device para SMS, err=%ld",
                 (long)err);
        devHandle = NULL;
    } else {
        ESP_LOGI(TAG,
                 "lte_open_for_sms_if_needed: devHandle aberto para SMS (%p)",
                 (void *)devHandle);
    }

    return err;
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
  sleep_request_cap_recharge_window();
  xQueueSend(xQueue_NetConnect,(void *)&Send_NetConnect_Task_ON, (TickType_t)0);
  deinit_LTE_System();
   
}

void init_LTE_System(void)
{
set_cpu_freq_rtc(160);
lte_dtr_wake_before_open();
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

 
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
#include "u_device.h"
#include "u_error_common.h"
#include "u_cell_power_strategy.h"

/*#define LTE_PWR_STRATEGY_DEFAULT   LTE_PWR_PSM_MIN
#define LTE_PWR_TAU_DEFAULT_SEC    (3 * 60 * 60)*/
/* ===== Política padrão: separar “normal” x “low-power” =====
 * - OFF_ALWAYS  -> legacy: sempre desliga duro ao fim do ciclo
 * - PSM_MIN     -> economia: nunca desliga duro; entra em PSM
 * - force_off_on_fail=false => mesmo em falha NÃO desliga (mantém benefício)
 */
#define LTE_PWR_MODE_DEFAULT      LTE_PWR_PSM_MIN
#define LTE_FORCE_OFF_ON_FAIL     false
#define LTE_PWR_TAU_DEFAULT_SEC   (3 * 60 * 60)
#define LTE_PWR_T3324_DEFAULT_SEC 0

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
    } else {
        /* Habilita modo “UART PSM” (UPSV=4) e fixa AT&D=1 logo após abrir o device.
           Isso garante que o DTR (IO33) controle o sono da UART do modem neste boot. */
        int32_t upsv_after = -1;
        int32_t err_psm = lte_power_set_uart_psm(devHandle, /*enable=*/true, /*set_atd1=*/true, &upsv_after);
        ESP_LOGI("LTE_PWR", "lte_power_set_uart_psm => err=%ld, UPSV=%ld", (long)err_psm, (long)upsv_after);
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
         bool skip_cleanup=false;
         int32_t power_error = (int32_t)U_ERROR_COMMON_SUCCESS;

          // Usar HTTP se habilitado
    if (has_network_http_enabled()) {
        printf(">>>>>>> HTTP Client <<<<<<<\n");
        delivery = ucell_Http_connection(devHandle);
    }

    // Usar MQTT se habilitado
    if (has_network_mqtt_enabled()) {
        printf(">>>>>>> MQTT Client <<<<<<<\n");
        delivery = ucell_MqttClient_connection (devHandle);
    }

     /* ===== Aplicar POLÍTICA após o ciclo (independente de sucesso) ===== */
    const lte_pwr_policy_t policy = {
        .mode               = LTE_PWR_MODE_DEFAULT,
        .force_off_on_fail  = LTE_FORCE_OFF_ON_FAIL,
        .tau_seconds        = LTE_PWR_TAU_DEFAULT_SEC,
        .t3324_seconds      = LTE_PWR_T3324_DEFAULT_SEC
    };
    power_error = lte_apply_policy_after_tx(devHandle, delivery, &policy, &skip_cleanup);
    if (power_error != 0) {
        ESP_LOGW(TAG, "lte_apply_policy_after_tx falhou (%ld); seguindo para cleanup.", (long)power_error);
        skip_cleanup = false;
    }
    if (!skip_cleanup) {
        printf(">>>Error = %d\n", cellNet_Close_CleanUp(devHandle));
    } else {
		/* Ativa economia de UART no modem (UPSV=4) e confirma com +UPSV? */
        int32_t upsv_after = -1;
        int32_t err = lte_power_set_uart_psm(devHandle, /*enable=*/true, /*set_atd1=*/true, &upsv_after);
        ESP_LOGI("LTE_PWR", "lte_power_set_uart_psm => err=%ld, UPSV=%ld", (long)err, (long)upsv_after);
        printf(">>>Policy low-power ativa: pulando hard-off.\n");
    }
    
    int32_t upsv_after = -1;
/* ativa UPSV=4 e seta AT&D=1 uma vez; lê de volta com +UPSV? */
int32_t err = lte_power_set_uart_psm(devHandle, /*enable=*/true, /*set_atd1=*/true, &upsv_after);
ESP_LOGI("LTE_PWR", "lte_power_set_uart_psm => err=%ld, UPSV=%ld", (long)err, (long)upsv_after);

    printf("DELIVERY ====>>> %d\n", delivery);

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
//	 cpu_boost_begin_160();
set_cpu_freq_rtc(160);
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

 
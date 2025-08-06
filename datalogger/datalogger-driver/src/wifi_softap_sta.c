#include "datalogger_control.h"
#include "datalogger_driver.h"
#include <string.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

//#include "esp_system.h"
#include "esp_mac.h" //substitui esp_system.h

#include "esp_wifi.h"
#include "esp_wifi_types.h" // Inclui explicitamente para ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD
#include "esp_event.h"
#include "esp_log.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#define ESP_WIFI_CHANNEL   6
#define MAX_STA_CONN       4

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

//#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WPA_WPA2_PSK

#define ESP_MAXIMUM_RETRY           5

extern bool ap_active;  // Vem do Factory Control
extern bool user_initiated_exit;

xTaskHandle Wifi_ap_sta_Task_TaskHandle = NULL;

/* FreeRTOS event group to signal when we are connected/disconnected */
static EventGroupHandle_t s_wifi_event_group;

static const char *TAG_AP = "WiFi SoftAP";
static const char *TAG_STA = "WiFi Sta";

static int s_retry_num = 0;
volatile bool sta_connected = false; // Definição global
volatile bool sta_intentional_disconnect = false; // Definição global
static esp_netif_t *ap_netif = NULL;
static esp_netif_t *sta_netif = NULL;


static void esp_netif_destroy_default_wifi_ap_sta(void) {
    sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA");
    if (sta_netif) esp_netif_destroy(sta_netif);
    ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP");
    if (ap_netif) esp_netif_destroy(ap_netif);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
	wifi_event_sta_disconnected_t *disconn;
	
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG_AP, "SoftAP iniciado com sucesso");
                break;
            case WIFI_EVENT_AP_STOP:
                 ESP_LOGI(TAG_AP, "SoftAP parado (AP_STOP)");
                 ap_active = false;
                 // Só auto-restart se NÃO for um exit intencional nem uma transição STA→AP
                 if (!user_initiated_exit && !has_activate_sta()) {
                 ESP_LOGI(TAG_AP, "Auto‐restart do SoftAP pelo evento AP_STOP");
                 ap_restart_cb(NULL);
                 } else {
                          ESP_LOGI(TAG_AP, "AP_STOP detectado mas bloqueado (exit ou STA ativo).");
                         }
                 break;
            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG_AP, "Estação conectada ao AP");
                break;
            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG_AP, "Estação desconectada do AP");
                break;
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG_STA, "Modo STA iniciado");
                if (has_activate_sta()) {
                    wifi_config_t wifi_config = {
                        .sta = {
                            .ssid = {0},
                            .password = {0},
                            .scan_method = WIFI_ALL_CHANNEL_SCAN,
                            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
                        },
                    };
                    strncpy((char *)wifi_config.sta.ssid, get_ssid_sta(), sizeof(wifi_config.sta.ssid) - 1);
                    strncpy((char *)wifi_config.sta.password, get_password_sta(), sizeof(wifi_config.sta.password) - 1);

                    if (strlen(get_ssid_sta()) > 0 && strlen(get_password_sta()) > 0) {
                        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
                        ESP_LOGI(TAG_STA, "STA ativado, conectando ao SSID: %s", get_ssid_sta());
                        esp_wifi_connect();
                    } else {
                        ESP_LOGW(TAG_STA, "SSID ou senha do STA vazios, conexão não iniciada");
                    }
                }
                
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG_STA, "Conectado ao roteador (STA)");
                sta_connected = true;
                s_retry_num = 0;
                sta_intentional_disconnect = false;
                xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                disconn = (wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGI(TAG_STA, "Desconectado do roteador, motivo: %d", disconn->reason);
                sta_connected = false;
                xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                if (!sta_intentional_disconnect && has_activate_sta() && s_retry_num < ESP_MAXIMUM_RETRY) {
                    ESP_LOGI(TAG_STA, "Tentando reconectar STA (tentativa %d/%d)", s_retry_num + 1, ESP_MAXIMUM_RETRY);
                    esp_wifi_connect();
                    s_retry_num++;
                } else if (sta_intentional_disconnect) {
                    ESP_LOGI(TAG_STA, "Desconexão intencional, não reconectando");
                }
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG_STA, "IP do STA obtido: " IPSTR, IP2STR(&event->ip_info.ip));
        sta_connected = true;
        s_retry_num = 0;
        sta_intentional_disconnect = false;
    }
}

esp_err_t start_wifi_ap_sta(void)
//static void Wifi_ap_sta_Task(void* pvParameters)
{
//	esp_err_t err;
 	char* ssid_ap = get_ssid_ap();
	char* psw_ap = get_password_ap();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
     
       ap_netif = esp_netif_create_default_wifi_ap();
    if (!ap_netif) {
        ESP_LOGI(TAG_AP, "Erro ao criar interface AP");
        return ESP_FAIL;
    }

    sta_netif = esp_netif_create_default_wifi_sta();
    if (!sta_netif) {
        ESP_LOGI(TAG_AP, "Erro ao criar interface STA");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
        /* Initialize event group */
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
                                                        
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));                                                     

	   wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(ssid_ap),
            .channel = ESP_WIFI_CHANNEL,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    strcpy((char*)wifi_config.ap.ssid,ssid_ap);
    strcpy((char*)wifi_config.ap.password,psw_ap);

    if (strlen(psw_ap) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_LOGI(TAG_AP, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             ssid_ap, psw_ap, ESP_WIFI_CHANNEL);
 

    ESP_ERROR_CHECK(esp_wifi_start());


    ESP_LOGI(TAG_AP, "Wi-Fi AP+STA iniciado. Acesse http://192.168.4.1");

    return ESP_OK;
}


//esp_err_t start_wifi_ap_sta(void)
/*void init_wifi_task(void)
{
	if (Wifi_ap_sta_Task_TaskHandle == NULL)
	   {
        xTaskCreatePinnedToCore( Wifi_ap_sta_Task, "Wifi_ap_sta_Task", 2048, NULL, 1, &Wifi_ap_sta_Task_TaskHandle,0);
        }
}*/


void stop_wifi_ap_sta(void)
{
	printf("Entrou no Stop Wifi \n");
	esp_wifi_disconnect();
	esp_err_t err = esp_wifi_stop();
        if (err != ESP_OK) {
            ESP_LOGI("Factory Control", ">>>Erro ao parar WiFi: %s\n", esp_err_to_name(err));
/*            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Erro ao parar WiFi");
            return ESP_FAIL;*/
        }

    esp_wifi_set_mode(WIFI_MODE_NULL);
/*    err = esp_wifi_set_mode(WIFI_MODE_NULL);
     if (err != ESP_OK) {
    ESP_LOGE("Factory Control", "Erro ao definir modo NULL: %s", esp_err_to_name(err));
    return ESP_FAIL;*/
    esp_netif_destroy_default_wifi_ap_sta();
    esp_wifi_deinit();
    esp_event_loop_delete_default();
    esp_netif_deinit();
/*      vTaskDelete(Wifi_ap_sta_Task_TaskHandle);
  Wifi_ap_sta_Task_TaskHandle = NULL;*/
  printf("***WIFI Finished***\n");

}

void ap_restart_cb(void* arg)
{
    // Se foi um exit intencional, não religa o AP
    if (user_initiated_exit) {
        ESP_LOGI(TAG_AP, "ap_restart_cb(): exit intencional ativo → não religa SoftAP");
        return;
    }

    ESP_LOGI(TAG_AP, "ap_restart_cb(): religando SoftAP");
    // Recoloca em modo AP+STA (ou só AP, se preferir)
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_start();
}
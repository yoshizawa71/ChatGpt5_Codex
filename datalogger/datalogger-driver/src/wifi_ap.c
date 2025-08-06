#include "datalogger_control.h"
#include "datalogger_driver.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

//#include "esp_system.h"
#include "esp_mac.h" //substitui esp_system.h

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#define ESP_WIFI_CHANNEL   1
#define MAX_STA_CONN       4

static const char *TAG = "wifi AP";

void get_mac_address(char* baseMacChr) {

  uint8_t baseMac[6];
  esp_read_mac(baseMac, ESP_MAC_WIFI_SOFTAP);
  sprintf(baseMacChr, "%02X%02X%02X%02X%02X%02X",
                        baseMac[0], baseMac[1], baseMac[2],
                        baseMac[3], baseMac[4], baseMac[5]);

}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

//bool start_wifi_ap(char* ssid, char* pw)
esp_err_t start_wifi_ap(void)
{
    char* ssid_ap = get_ssid_ap();
	char* psw_ap = get_password_ap();
	
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
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

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             ssid_ap, psw_ap, ESP_WIFI_CHANNEL);

    return true;
}

void stop_wifi_ap(void)
{
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_event_loop_delete_default();
    esp_netif_deinit();

}
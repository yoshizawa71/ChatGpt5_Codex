#include <include/datalogger_control.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "cJSON.h"
#include "esp_modem.h"
#include "sleep_control.h"
#include "esp_log.h"


static void ota_task(void *pvParameter);
static bool ota_url_change_validation(void);
esp_err_t ota_error=0;


#define FIRMWARE_VERSION "0.0.1"

static char firmware_upgrade_url[256] = {0};
static char firmware_upgrade_url1[256] = {0};
static char firmware_upgrade_url2[256] = {0};


static const char *TAG = "ota_control";

bool check_update(int *http_status)
{
    char output_buffer[1024] = {0};
    int content_length = 0;
    bool should_update = false;
    char query_data[70]= {0};
    strcat(query_data, get_config_server_url());
    strcat(query_data,"?");
    strcat(query_data,"serie=");
    strcat(query_data,get_serial_number());
    strcat(query_data,"&");
    strcat(query_data,"versao=");
    strcat(query_data,FIRMWARE_VERSION);
    printf("URL OTA Check Update %s\n",query_data );
    esp_http_client_config_t config = {
        .url = query_data,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } else {
        content_length = esp_http_client_fetch_headers(client);
        if (content_length < 0) {
            ESP_LOGE(TAG, "HTTP client fetch headers failed");
        } else {
            int data_read = esp_http_client_read_response(client, output_buffer, 1024);
            if (data_read >= 0) {
                uint32_t status = esp_http_client_get_status_code(client);
                ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                status,
                esp_http_client_get_content_length(client));
                ESP_LOGI(TAG, "%s", output_buffer );
                
                if(status == 200) {
                    cJSON *root = cJSON_Parse(output_buffer);
                    *http_status=status;
        
                    if(strcmp(cJSON_GetObjectItem(root, "version")->valuestring, FIRMWARE_VERSION) != 0)
                    {
                        should_update = true;
                        strcpy(firmware_upgrade_url1, cJSON_GetObjectItem(root, "url1")->valuestring);
                        strcpy(firmware_upgrade_url2, cJSON_GetObjectItem(root, "url2")->valuestring);
                    }
                    cJSON_Delete(root);
                }
                else
                {
                	printf("Connection_OTA_server_error\n");
                }
            } else {
                ESP_LOGE(TAG, "Failed to read response");
            }
        }
    }
    esp_http_client_close(client);

    return should_update;
}

void ota_start_transfer(void)
{
	//xTaskCreatePinnedToCore(&ota_task, "ota_task", 10000, NULL, 5, NULL,1);
	xTaskCreate(&ota_task, "ota_task", 10000, NULL, 5, NULL);

}

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}

static void ota_task(void *pvParameter)
{
	short int try_ota_transfer=1;
	bool ota_ret=false;
	bool flagurl1=true;
	bool flagurl2=true;
	printf("Start Ota Transfer via GSM\n");

    while (1) {

        switch(try_ota_transfer)
        {
         case 1 :
        	 if (flagurl1)
        	 {
        	  flagurl1=false;
              strcpy(firmware_upgrade_url,firmware_upgrade_url1);
        	  printf("firmware_upgrade_url = %s\n",firmware_upgrade_url);
        	  ota_ret=ota_url_change_validation();
        	  if (!ota_ret)
        	     {
        		   ESP_LOGE(TAG, "Firmware upgrade failed %d\n", try_ota_transfer);
        		   try_ota_transfer++;

        	      }
        	  }
        	  break;
         case 2 :
        	 if (flagurl2)
        	 {
        	  flagurl2=false;
              strcpy(firmware_upgrade_url,firmware_upgrade_url2);
        	  printf("firmware_upgrade_url = %s\n",firmware_upgrade_url);
        	  ota_ret=ota_url_change_validation();
        	  if (!ota_ret)
        	      {
        	        ESP_LOGE(TAG, "Firmware upgrade failed %d\n", try_ota_transfer);
        	        try_ota_transfer++;

        	       }
        	  }
             break;
          case 3 :
        	  ESP_LOGE(TAG, ">>>>>Firmware upgrade failed twice<<<<<");
        	  set_inactivity();
        	  save_system_config_data_time();
        	  esp_restart();
              break;
         default : printf("Unespected Falt\n");
        	            set_inactivity();
        	            save_system_config_data_time();
        	            esp_restart();
              break;
        }

 //***********************
    if (ota_ret == true) {
        set_inactivity();
        save_system_config_data_time();
        esp_restart();
       }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

char* get_ota_version()
{
    return FIRMWARE_VERSION;
}

static bool ota_url_change_validation(void)
{
    char query_transfer[120]= {0};
    strcat(query_transfer, firmware_upgrade_url);
    strcat(query_transfer,"?");
    strcat(query_transfer,"serie=");
    strcat(query_transfer,get_serial_number());
    strcat(query_transfer,"&");
    strcat(query_transfer,"versao=");
    strcat(query_transfer,FIRMWARE_VERSION);
    printf("URL OTA transfer %s\n",query_transfer);
	esp_http_client_config_t config = {
	        .url = query_transfer,
	        .event_handler = _http_event_handler,
	    };

	    esp_https_ota_config_t ota_config = {
	        .http_config = &config,
	    };

	    esp_https_ota_handle_t https_ota_handle;
	    esp_app_desc_t app_desc;

	    printf("URL %s\n", config.url);

	    config.skip_cert_common_name_check = true;

	    // esp_err_t ret = esp_https_ota(&config);
        printf("### HTTPS OTA BEGIN ###\n");
	    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
	    if (err != ESP_OK) {
	                        ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed");
	                        goto ota_change_url;
	                        }

	    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
	    if (err != ESP_OK) {
	            ESP_LOGE(TAG, "esp_https_ota_read_img_desc failed");
	            goto ota_change_url;
	        }
	    // validate_image_header(&app_desc);

//	    esp_err_t err;

	       while (1) {
	           err = esp_https_ota_perform(https_ota_handle);
	           if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
	               printf("ERR: %d\n", err);
	               break;
	           }

	           if(check_modem_conn_fail()){
	               break;
	           }
	       }
	       bool ret = esp_https_ota_is_complete_data_received(https_ota_handle);
	       esp_https_ota_finish(https_ota_handle);

	       return ret;

ota_change_url:
      return ret=false;


}






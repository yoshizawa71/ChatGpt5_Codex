#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "cJSON.h"
#include "unity.h"
#include "datalogger_driver.h"

#include "pulse_meter.h"
#include "pressure_meter.h"
#include "energy_meter.h"

#include "nvs_flash.h"
#include "nvs.h"

#include <dirent.h>
#include "system.h"

static const char *TAG = "Config_Driver";

static SemaphoreHandle_t file_mutex;

#define littlefs_base_path "/littlefs"

#define DEVICE_CONFIG_FILE  "/littlefs/dev_config.json"
#define NETWORK_CONFIG_FILE  "/littlefs/net_config.json"
#define OPERATION_CONFIG_FILE  "/littlefs/op_config.json"
#define RECORD_PULSE_CONFIG_FILE  "/littlefs/rec_pulse_config.json"
#define SYSTEM_CONFIG_FILE  "/littlefs/system_config.json"
#define RECORD_LAST_UNIT_TIME_FILE  "/littlefs/record_last_unit_time.json"
#define SELF_MONITORING_DATA  "/littlefs/self_monitoring_data.json"
#define PRESSURE_DATA_SET "/littlefs/pressure_data_set.json"
#define INDEX_CONTROL "/littlefs/index_control.json"
#define PRESSURE_INDEX_CONTROL "/littlefs/pressure_index_control.json"
#define ENERGY_INDEX_CONTROL "/littlefs/energy_index_control.json"
#define ENERGY_MEASURED "/littlefs/energy_measured.json"

bool has_device_config(void)
{
    struct stat st;
    bool exists = false;

    if (xSemaphoreTake(file_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (stat(DEVICE_CONFIG_FILE, &st) == 0) {
            exists = true;
        }
        xSemaphoreGive(file_mutex);
    } else {
        // opcional: logar timeout
        printf("has_device_config: timeout ao obter mutex\n");
    }

    return exists;
}

bool has_network_config(void)
{
    struct stat st;
    bool exists = false;
  
    if (xSemaphoreTake(file_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (stat(NETWORK_CONFIG_FILE, &st) == 0) {
            exists = true;
        }
        xSemaphoreGive(file_mutex);
    } else {
        // opcional: logar timeout
        printf("has_network_config: timeout ao obter mutex\n");
    }

    return exists;
}

bool has_operation_config(void)
{
    struct stat st;
    bool exists = false;
    
    if (xSemaphoreTake(file_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (stat(OPERATION_CONFIG_FILE, &st) == 0) {
            exists = true;
        }
        xSemaphoreGive(file_mutex);
    } else {
        // opcional: logar timeout
        printf("has_operation_config: timeout ao obter mutex\n");
    }

    return exists;
}

bool has_record_index_config(void)
{
    struct stat st;
    bool exists = false;
    
    if (xSemaphoreTake(file_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (stat(INDEX_CONTROL, &st) == 0) {
            exists = true;
        }
        xSemaphoreGive(file_mutex);
    } else {
        // opcional: logar timeout
        printf("has_record_index_config: timeout ao obter mutex\n");
    }
    
    return exists;
}

bool has_record_pulse_config(void)
{
    struct stat st;
    bool exists = false;
    
    if (xSemaphoreTake(file_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (stat(RECORD_PULSE_CONFIG_FILE, &st) == 0) {
            exists = true;
        }
        xSemaphoreGive(file_mutex);
    } else {
        // opcional: logar timeout
        printf("has_record_pulse_config: timeout ao obter mutex\n");
    }

    return exists;
}

/*bool has_record_pressure_index_config(void)
{
    struct stat st;
    if (stat(PRESSURE_INDEX_CONTROL, &st) == 0)
    {
        return true;
    }
    return false;
}*/

/*bool has_record_energy_index_config(void)
{
    struct stat st;
    if (stat(ENERGY_INDEX_CONTROL, &st) == 0)
    {
        return true;
    }
    return false;
}*/


bool has_system_config(void)
{
    struct stat st;
    bool exists = false;
    
    if (xSemaphoreTake(file_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (stat(SYSTEM_CONFIG_FILE, &st) == 0) {
            exists = true;
        }
        xSemaphoreGive(file_mutex);
    } else {
        // opcional: logar timeout
        printf("has_system_config: timeout ao obter mutex\n");
    }

    return exists;
}
//**********
bool has_record_last_unit_time(void)
{
    struct stat st;
    bool exists = false;
    
    if (xSemaphoreTake(file_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (stat(RECORD_LAST_UNIT_TIME_FILE, &st) == 0) {
            exists = true;
        }
        xSemaphoreGive(file_mutex);
    } else {
        // opcional: logar timeout
        printf("has_record_last_unit_time: timeout ao obter mutex\n");
    }

    return exists;
}

bool has_self_monitoring_data(void)
{
    struct stat st;
    bool exists = false;
    
    if (xSemaphoreTake(file_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (stat(SELF_MONITORING_DATA, &st) == 0) {
            exists = true;
        }
        xSemaphoreGive(file_mutex);
    } else {
        // opcional: logar timeout
        printf("has_self_monitoring_data: timeout ao obter mutex\n");
    }

    return exists;
}

bool has_pressure_data(void)
{
    struct stat st;
    bool exists = false;
    
    if (xSemaphoreTake(file_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (stat(PRESSURE_DATA_SET, &st) == 0) {
            exists = true;
        }
        xSemaphoreGive(file_mutex);
        
    } else {
        // opcional: logar timeout
        printf("has_pressure_data: timeout ao obter mutex\n");
    }

    return exists;
}


//---------------------

void unmount_driver(void)
{

    deinit_filesystem();
}

void mount_driver(void)
{
    ESP_LOGI(TAG, "Initializing LittelFS");
    init_filesystem();

    file_mutex = xSemaphoreCreateMutex();
    if (file_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create file mutex");
    return;
       }
}

void save_device_config(struct device_config *config)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id", config->id);
    cJSON_AddStringToObject(root, "name", config->name);
    cJSON_AddStringToObject(root, "phone", config->phone);
    cJSON_AddStringToObject(root, "ssid_ap", config->ssid_ap);
    cJSON_AddStringToObject(root, "wifi_password_ap", config->wifi_password_ap);
     if(config->activate_sta)
    {
        cJSON_AddTrueToObject(root, "activate_sta");
    }
    else
    {
        cJSON_AddFalseToObject(root, "activate_sta");
    }
    cJSON_AddStringToObject(root, "ssid_sta", config->ssid_sta);
    cJSON_AddStringToObject(root, "wifi_password_sta", config->wifi_password_sta);
    
    // sempre serialize o campo send_mode, send_period e send_times
    cJSON_AddStringToObject(root, "send_mode", config->send_mode);
    cJSON_AddNumberToObject(root, "send_period", config->send_period);
    {
      int times_int[4];
      for (int i = 0; i < 4; i++) {
        times_int[i] = config->send_times[i];
      }
      cJSON *arr = cJSON_CreateIntArray(times_int, 4);
      cJSON_AddItemToObject(root, "send_times", arr);
}

    cJSON_AddNumberToObject(root, "deep_sleep_period", config->deep_sleep_period);
    
    if(config->save_pulse_zero)
    {
        cJSON_AddTrueToObject(root, "save_pulse_zero");
    }
    else
    {
        cJSON_AddFalseToObject(root, "save_pulse_zero");
    }
    cJSON_AddNumberToObject(root, "scale", config->scale);
    cJSON_AddNumberToObject(root, "flow_rate", config->flow_rate);
    cJSON_AddStringToObject(root, "date", config->date);
    cJSON_AddStringToObject(root, "time", config->time);
    
    if(config->finished_factory)
    {
        cJSON_AddTrueToObject(root, "finished_factory");
    }
    else
    {
        cJSON_AddFalseToObject(root, "finished_factory");
    }
    
     if(config->device_active)
    {
        cJSON_AddTrueToObject(root, "device_active");
    }
    else
    {
        cJSON_AddFalseToObject(root, "device_active");
    }
    
/*    if(config->send_value)
    {
        cJSON_AddTrueToObject(root, "send_value");
    }
    else
    {
        cJSON_AddFalseToObject(root, "send_value");
    }*/
 
    char *device_config = cJSON_PrintUnformatted(root);
    
 if (xSemaphoreTake(file_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    FILE *f = fopen(DEVICE_CONFIG_FILE, "w");
    if (f != NULL){
       if (fputs(device_config, f) == EOF) {
                printf("*** save_device_config --> Error writing to file: %s ***\n", strerror(errno));
                fclose(f);
                xSemaphoreGive(file_mutex);
                free(device_config);
                cJSON_Delete(root);
                return;
            }
        // fecha e checa erro ao fechar
            if (fclose(f) != 0) {
                printf("*** save_device_config --> Error closing file: %s ***\n", strerror(errno));
                xSemaphoreGive(file_mutex);
                free(device_config);
                cJSON_Delete(root);
                return;
                }
    }else{
    	  printf ("*** save_device_config --> File = Null (is /littlefs mounted?) ***\n");
         }
    xSemaphoreGive(file_mutex);
  } // end if-take
  
    free(device_config);
    cJSON_Delete(root);  
  
}

void get_device_config(struct device_config *config)
{
    xSemaphoreTake(file_mutex,portMAX_DELAY);
    FILE *f = fopen(DEVICE_CONFIG_FILE, "r");

if (f == NULL)
  {
   printf ("### get_device_config --> File = Null ###\n");
   xSemaphoreGive(file_mutex);
   return;
   }
   
	  vTaskDelay(pdMS_TO_TICKS(10));
      fseek(f, 0L, SEEK_END);
      long size = ftell(f);
      rewind(f);
      
        if (size <= 0) {
        // Arquivo vazio ou erro de tamanho
        fclose(f);
        printf("### get_device_config --> SIZE = %ld ###\n", size);
        vTaskDelay(pdMS_TO_TICKS(100));
        xSemaphoreGive(file_mutex);
        return;
        }
      
    // Lê conteúdo em buffer
    char buf[1024];
    size_t bytes_read = fread(buf, 1, (size_t) (size < (long)sizeof(buf) - 1 ? size : (long)sizeof(buf) - 1), f);
    buf[bytes_read] = '\0';
    // Fecha o arquivo agora que a leitura foi concluída
    fclose(f);
    
 // Parse do JSON
        cJSON *root = cJSON_Parse(buf);
        if (root == NULL) {
        printf("### get_device_config --> JSON Parse Error ###\n");
        xSemaphoreGive(file_mutex);
        return;
        }
        cJSON *item;

//************************************************
//        Print Json File
//************************************************
 //       char *my_json_string = cJSON_Print(root);
//        ESP_LOGI(TAG, ">>>LOAD_DEVICE_CONFIG_FILE<<<\n%s",my_json_string);
//        strlcpy(config->id, cJSON_GetObjectItem(root, "id")->valuestring,  sizeof(config->id));

//************************************************
        strcpy(config->id, cJSON_GetObjectItem(root, "id")->valuestring);
        strcpy(config->name, cJSON_GetObjectItem(root, "name")->valuestring);
        strcpy(config->phone, cJSON_GetObjectItem(root, "phone")->valuestring);
        strcpy(config->ssid_ap, cJSON_GetObjectItem(root, "ssid_ap")->valuestring);
        strcpy(config->wifi_password_ap, cJSON_GetObjectItem(root, "wifi_password_ap")->valuestring);
        if(cJSON_IsTrue(cJSON_GetObjectItem(root, "activate_sta")))
        {
            config->activate_sta = true;
        }
        else
        {
            config->activate_sta = false;
        }       
        strcpy(config->ssid_sta, cJSON_GetObjectItem(root, "ssid_sta")->valuestring);
        strcpy(config->wifi_password_sta, cJSON_GetObjectItem(root, "wifi_password_sta")->valuestring);
        
//-------------------------------------------------
    cJSON *jm = cJSON_GetObjectItem(root, "send_mode");
    if (jm && cJSON_IsString(jm)) {
        strncpy(config->send_mode, jm->valuestring, sizeof(config->send_mode)-1);
        config->send_mode[sizeof(config->send_mode)-1] = '\0';
    } else {
        strcpy(config->send_mode, "freq"); // padrão
    }

    // 6) Lê send_period (sempre presente)
    cJSON *p = cJSON_GetObjectItem(root, "send_period");
    if (p && cJSON_IsNumber(p)) {
        config->send_period = p->valueint;
    }

    // 7) Lê send_times, só sobrescreve se for número (inclusive 0)
    cJSON *arr = cJSON_GetObjectItem(root, "send_times");
    if (arr && cJSON_IsArray(arr)) {
        int len = cJSON_GetArraySize(arr);
        for (int i = 0; i < len && i < 4; i++) {
            cJSON *t = cJSON_GetArrayItem(arr, i);
            if (t && cJSON_IsNumber(t)) {
                config->send_times[i] = (uint8_t)t->valueint;
            }
            // se vier JSON null ou não-numérico, pula e mantém valor antigo
        }
    }
   
//--------------------------------------------------------------  
       config->deep_sleep_period = cJSON_GetObjectItem(root, "deep_sleep_period")->valueint;

        if(cJSON_IsTrue(cJSON_GetObjectItem(root, "save_pulse_zero")))
        {
            config->save_pulse_zero = true;
        }
        else
        {
            config->save_pulse_zero = false;
        }
        config->scale = cJSON_GetObjectItem(root, "scale")->valueint;
        // Campo: flow_rate
        item = cJSON_GetObjectItem(root, "flow_rate");
        if (item && cJSON_IsNumber(item)) {
        config->flow_rate = (float)item->valuedouble;
        } else {
        ESP_LOGW("DeviceConfig", "Campo 'flow_rate' não encontrado ou inválido");
        config->flow_rate = 0.0f;
        }
        
        strcpy(config->date, cJSON_GetObjectItem(root, "date")->valuestring);
        strcpy(config->time, cJSON_GetObjectItem(root, "time")->valuestring);
        if(cJSON_IsTrue(cJSON_GetObjectItem(root, "finished_factory")))
        {
            config->finished_factory = true;
        }
        else
        {
            config->finished_factory = false;
        }
        
        if(cJSON_IsTrue(cJSON_GetObjectItem(root, "device_active")))
        {
            config->device_active = true;
        }
        else
        {
            config->device_active = false;
        }
        
        
        cJSON_Delete(root);
     
/*    else
         {
          fclose(f);
          printf ("### get_device_config --> SIZE = %d ###\n",size);
          vTaskDelay(pdMS_TO_TICKS(100));

          }
          */  

    xSemaphoreGive(file_mutex);
}

void save_network_config(struct network_config *config)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "apn", config->apn);
    cJSON_AddStringToObject(root, "lte_user", config->lte_user);
    cJSON_AddStringToObject(root, "lte_pw", config->lte_pw);
    cJSON_AddStringToObject(root, "data_server_url", config->data_server_url);
    cJSON_AddNumberToObject(root, "data_server_port", config->data_server_port);
    cJSON_AddStringToObject(root, "data_server_path", config->data_server_path);
    
    cJSON_AddStringToObject(root, "user", config->user);
    cJSON_AddStringToObject(root, "token", config->token);
    cJSON_AddStringToObject(root, "pw", config->pw);
    
    cJSON_AddStringToObject(root, "mqtt_url", config->mqtt_url);
    cJSON_AddNumberToObject(root, "mqtt_port", config->mqtt_port);
    cJSON_AddStringToObject(root, "mqtt_topic", config->mqtt_topic);
 //   printf("@@@ Save MQTT TOPIC =%s\n", config->mqtt_topic); 
 
     if(config->http_en)
    {
        cJSON_AddTrueToObject(root, "http_enable");
    }
     else
    {
        cJSON_AddFalseToObject(root, "http_enable");
    }
     
    if(config->user_en)
    {
        cJSON_AddTrueToObject(root, "user_en");
    }
    else
    {
        cJSON_AddFalseToObject(root, "user_en");
    }
    if(config->token_en)
    {
        cJSON_AddTrueToObject(root, "token_en");
    }
    else
    {
        cJSON_AddFalseToObject(root, "token_en");
    }
    if(config->pw_en)
    {
        cJSON_AddTrueToObject(root, "pw_en");
    }
    else
    {
        cJSON_AddFalseToObject(root, "pw_en");
    }
    
    if(config->mqtt_en)
    {
        cJSON_AddTrueToObject(root, "mqtt_enable");
    }
    else
    {
        cJSON_AddFalseToObject(root, "mqtt_enable");
    }
    
    char *network_config = cJSON_PrintUnformatted(root);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    if (xSemaphoreTake(file_mutex, portMAX_DELAY) == pdTRUE) {
    FILE *f = fopen(NETWORK_CONFIG_FILE, "w");
    if (f != NULL)
    {
//          fputs(network_config, f);
        if (fputs(network_config, f) == EOF) {
                printf("*** save_network_config --> Error writing to file: %s ***\n", strerror(errno));
                fclose(f);
                xSemaphoreGive(file_mutex);
                free(network_config);
                cJSON_Delete(root);
                return;
            }
        fclose(f);
    }
    else{
    	 printf ("*** save_network_config --> File = Null ***\n");
         }
      xSemaphoreGive(file_mutex);  
     }
     
    free(network_config);
    cJSON_Delete(root);  
}

void get_network_config(struct network_config *config)
{
    xSemaphoreTake(file_mutex,portMAX_DELAY);
        // Tenta abrir o arquivo de configuração
    FILE *f = fopen(NETWORK_CONFIG_FILE, "r");

    if (f == NULL){  
        printf ("### get_network_config --> File = Null ###\n");
        xSemaphoreGive(file_mutex);
        return;
    }

       vTaskDelay(pdMS_TO_TICKS(10));
       fseek(f, 0L, SEEK_END);
       long size = ftell(f);
       rewind(f);
       
        if (size <= 0) {
        // Arquivo vazio ou erro de tamanho
        fclose(f);
        printf("### get_network_config --> SIZE = %ld ###\n", size);
        vTaskDelay(pdMS_TO_TICKS(100));
        xSemaphoreGive(file_mutex);
        return;
        }

 char buf[1024];
      size_t bytes_read = fread(buf, 1, (size_t) (size < (long)sizeof(buf) - 1 ? size : (long)sizeof(buf) - 1), f);
      buf[bytes_read] = '\0';
    // Fecha o arquivo agora que a leitura foi concluída
      fclose(f);
      
    // Parse do JSON
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        printf("### get_network_config --> JSON Parse Error ###\n");
        xSemaphoreGive(file_mutex);
        return;
    }
    cJSON *item;
        
        strcpy(config->apn, cJSON_GetObjectItem(root, "apn")->valuestring);
        strcpy(config->lte_user, cJSON_GetObjectItem(root, "lte_user")->valuestring);
        strcpy(config->lte_pw, cJSON_GetObjectItem(root, "lte_pw")->valuestring);
        strcpy(config->data_server_url, cJSON_GetObjectItem(root, "data_server_url")->valuestring);
        config->data_server_port = cJSON_GetObjectItem(root, "data_server_port")->valueint;
        strcpy(config->data_server_path, cJSON_GetObjectItem(root, "data_server_path")->valuestring);
        strcpy(config->user, cJSON_GetObjectItem(root, "user")->valuestring);
        strcpy(config->token, cJSON_GetObjectItem(root, "token")->valuestring);
        strcpy(config->pw, cJSON_GetObjectItem(root, "pw")->valuestring);
        
//-------------------------------------------------------------------------------------------------------        
        strcpy(config->mqtt_url, cJSON_GetObjectItem(root, "mqtt_url")->valuestring);
        config->mqtt_port = cJSON_GetObjectItem(root, "mqtt_port")->valueint;
        strcpy(config->mqtt_topic, cJSON_GetObjectItem(root, "mqtt_topic")->valuestring);
 //       printf("@@@ Load MQTT TOPIC =%s\n", config->mqtt_topic);
//-------------------------------------------------------------------------------------------------------    
        if(cJSON_IsTrue(cJSON_GetObjectItem(root, "http_enable")))
        {
            config->http_en = true;
        }
        else
        {
            config->http_en = false;
        }
         
        if(cJSON_IsTrue(cJSON_GetObjectItem(root, "user_en")))
        {
            config->user_en = true;
        }
        else
        {
            config->user_en = false;
        }
        if(cJSON_IsTrue(cJSON_GetObjectItem(root, "token_en")))
        {
            config->token_en = true;
        }
        else
        {
            config->token_en = false;
        }
        if(cJSON_IsTrue(cJSON_GetObjectItem(root, "pw_en")))
        {
            config->pw_en = true;
        }
        else
        {
            config->pw_en = false;
        }
        
        if(cJSON_IsTrue(cJSON_GetObjectItem(root, "mqtt_enable")))
        {
            config->mqtt_en = true;
        }
        else
        {
            config->mqtt_en = false;
        } 
        
        cJSON_Delete(root);

    xSemaphoreGive(file_mutex);
}

void save_operation_config(struct operation_config *config)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "serial_number", config->serial_number);
    cJSON_AddStringToObject(root, "company", config->company);
    cJSON_AddStringToObject(root, "ds_start", config->deep_sleep_start);
    cJSON_AddStringToObject(root, "ds_end", config->deep_sleep_end);
    if(config->reset_count)
    {
        cJSON_AddTrueToObject(root, "reset_count");
    }
    else
    {
        cJSON_AddFalseToObject(root, "reset_count");
    }
    cJSON_AddStringToObject(root, "keep_alive", config->keep_alive);
/*    
    if(config->log_level_1)
    {
        cJSON_AddTrueToObject(root, "log1");
    }
    else
    {
        cJSON_AddFalseToObject(root, "log1");
    }
    if(config->log_level_2)
    {
        cJSON_AddTrueToObject(root, "log2");
    }
    else
    {
        cJSON_AddFalseToObject(root, "log2");
    }
*/

//------------------------------------------
    if(config->enable_post)
    {
        cJSON_AddTrueToObject(root, "en_post");
    }
    else
    {
        cJSON_AddFalseToObject(root, "en_post");
    }
    if(config->enable_get)
    {
        cJSON_AddTrueToObject(root, "en_get");
    }
    else
    {
        cJSON_AddFalseToObject(root, "en_get");
    }

    cJSON_AddStringToObject(root, "config_server_url", config->config_server_url);
    cJSON_AddNumberToObject(root, "config_server_port", config->config_server_port);
    cJSON_AddStringToObject(root, "config_server_path", config->config_server_path);
    
    cJSON_AddNumberToObject(root, "level_min", config->level_min);
    cJSON_AddNumberToObject(root, "level_max", config->level_max);

    char *operation_config = cJSON_PrintUnformatted(root);
    
    if (xSemaphoreTake(file_mutex, portMAX_DELAY) == pdTRUE) {
    FILE *f = fopen(OPERATION_CONFIG_FILE, "w");
    if (f != NULL)
    {
//        fputs(operation_config, f);
          if (fputs(operation_config, f) == EOF) {
                printf("*** save_operation_config --> Error writing to file: %s ***\n", strerror(errno));
                fclose(f);
                xSemaphoreGive(file_mutex);
                free(operation_config);
                cJSON_Delete(root);
                return;
            }
        fclose(f);
    }
    else{
    	 printf ("*** save_operation_config --> File = Null ***\n");
         }
      xSemaphoreGive(file_mutex);
     }
    free(operation_config);
    cJSON_Delete(root);
}

void get_operation_config(struct operation_config *config)
{
    xSemaphoreTake(file_mutex,portMAX_DELAY);
    
    FILE *f = fopen(OPERATION_CONFIG_FILE, "r");
    
    if (f == NULL)
     {
   	  printf ("### get_operation_config --> File = Null ###\n");
   	  xSemaphoreGive(file_mutex);
      return;
     }
    	vTaskDelay(pdMS_TO_TICKS(10));
        fseek(f, 0L, SEEK_END);
        long size = ftell(f);
        rewind(f);
        
         if (size <= 0) {
        // Arquivo vazio ou erro de tamanho
        fclose(f);
        printf("### get_operation_config --> SIZE = %ld ###\n", size);
        vTaskDelay(pdMS_TO_TICKS(100));
        xSemaphoreGive(file_mutex);
        return;
        }
  
		char buf[1024];   
        size_t bytes_read = fread(buf, 1, (size_t) (size < (long)sizeof(buf) - 1 ? size : (long)sizeof(buf) - 1), f);
        buf[bytes_read] = '\0';
        // Fecha o arquivo agora que a leitura foi concluída
        fclose(f);
    // Parse do JSON
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        printf("### get_operation_config --> JSON Parse Error ###\n");
        xSemaphoreGive(file_mutex);
        return;
    }
    cJSON *item;
    
        strcpy(config->serial_number, cJSON_GetObjectItem(root, "serial_number")->valuestring);
        strcpy(config->company, cJSON_GetObjectItem(root, "company")->valuestring);
        strcpy(config->deep_sleep_start, cJSON_GetObjectItem(root, "ds_start")->valuestring);
        strcpy(config->deep_sleep_end, cJSON_GetObjectItem(root, "ds_end")->valuestring);
        if(cJSON_IsTrue(cJSON_GetObjectItem(root, "reset_count")))
        {
            config->reset_count = true;
        }
        else
        {
            config->reset_count = false;
        }
        strcpy(config->keep_alive, cJSON_GetObjectItem(root, "keep_alive")->valuestring);
        
/*        if(cJSON_IsTrue(cJSON_GetObjectItem(root, "log1")))
        {
            config->log_level_1 = true;
        }
        else
        {
            config->log_level_1 = false;
        }
        if(cJSON_IsTrue(cJSON_GetObjectItem(root, "log2")))
        {
            config->log_level_2 = true;
        }
        else
        {
            config->log_level_2 = false;
        }
//---------------------------------------------------
 */
        if(cJSON_IsTrue(cJSON_GetObjectItem(root, "en_post")))
        {
            config->enable_post = true;
        }
        else
        {
            config->enable_post = false;
        }
        if(cJSON_IsTrue(cJSON_GetObjectItem(root, "en_get")))
        {
            config->enable_get = true;
        }
        else
        {
            config->enable_get = false;
        }

        strcpy(config->config_server_url, cJSON_GetObjectItem(root, "config_server_url")->valuestring);
        config->config_server_port = cJSON_GetObjectItem(root, "config_server_port")->valueint;
        strcpy(config->config_server_path, cJSON_GetObjectItem(root, "config_server_path")->valuestring);
        
        config->level_min = cJSON_GetObjectItem(root, "level_min")->valueint;
        config->level_max = cJSON_GetObjectItem(root, "level_max")->valueint;

    cJSON_Delete(root);
    xSemaphoreGive(file_mutex);
}

//==============================================================
esp_err_t save_index_config(struct record_index_config *config)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        printf("*** save_index_config --> Falha ao criar JSON ***\n");
        return ESP_FAIL;
    }
    cJSON_AddNumberToObject(root, "last_write_idx", config->last_write_idx);
    cJSON_AddNumberToObject(root, "last_read_idx", config->last_read_idx);
    cJSON_AddNumberToObject(root, "total_idx", config->total_idx);
    cJSON_AddNumberToObject(root, "cursor_position", config->cursor_position);

    char *general_index = cJSON_PrintUnformatted(root);
    
    if (!general_index) {
        printf("*** save_index_config --> Falha ao criar string JSON ***\n");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
 
    if (xSemaphoreTake(file_mutex, portMAX_DELAY) == pdTRUE) {
       FILE *f = fopen(INDEX_CONTROL, "w");
    if (f != NULL) {
        
 //       fprintf(f, "%s",general_index);
//        fputs(general_index, f);
        if (fputs(general_index, f) == EOF) {
                printf("*** save_index_config --> Error writing to file: %s ***\n", strerror(errno));
                fclose(f);
                xSemaphoreGive(file_mutex);
                free(general_index);
                cJSON_Delete(root);
                return ESP_FAIL;
            }
        fclose(f);
     }else{
		   printf("*** save_index_config --> File = Null ***\n");
		   }
        xSemaphoreGive(file_mutex);
      }
         
    free(general_index);
//------------------------------------------------
//        Print Json File
//------------------------------------------------
       char *my_json_string = cJSON_Print(root);
        ESP_LOGI(TAG, ">>>save_index_config<<<\n%s",my_json_string);      
//------------------------------------------------   
    cJSON_Delete(root);
    
    return ESP_OK;
}

esp_err_t get_index_config(struct record_index_config *config)
{
    xSemaphoreTake(file_mutex,portMAX_DELAY);
    FILE *f = fopen(INDEX_CONTROL, "r");
    if (f == NULL) {
        printf("### get_index_config --> File = Null ###\n");
        xSemaphoreGive(file_mutex);
        return ESP_FAIL;
    } 
        fseek(f, 0L, SEEK_END);
        long size = ftell(f);
        rewind(f);
      
    if (size <= 0) {
        // Arquivo vazio ou erro de tamanho
        fclose(f);
        printf("### get_index_config --> SIZE = %ld ###\n", size);
        vTaskDelay(pdMS_TO_TICKS(100));
        xSemaphoreGive(file_mutex);
        return ESP_FAIL;
    } 
    
    char buf[256];
    size_t bytes_read = fread(buf, 1, (size_t) (size < (long)sizeof(buf) - 1 ? size : (long)sizeof(buf) - 1), f);
    buf[bytes_read] = '\0';
    // Fecha o arquivo agora que a leitura foi concluída
    fclose(f);
    
    // Parse do JSON
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        printf("### get_index_config --> JSON Parse Error ###\n");
        xSemaphoreGive(file_mutex);
        return ESP_FAIL;
     }
     
    cJSON *item;
    
        config->last_write_idx = cJSON_GetObjectItem(root, "last_write_idx")->valueint;
        config->last_read_idx = cJSON_GetObjectItem(root, "last_read_idx")->valueint;
        config->total_idx = cJSON_GetObjectItem(root, "total_idx")->valueint;
        config->cursor_position = cJSON_GetObjectItem(root, "cursor_position")->valueint;
        
//------------------------------------------------
//        Print Json File
//------------------------------------------------
   /*    char *my_json_string = cJSON_Print(root);
        ESP_LOGI(TAG, "<<<get_index_config>>>\n%s",my_json_string);
        */      
//------------------------------------------------   
        cJSON_Delete(root);

   xSemaphoreGive(file_mutex);
   return ESP_OK;

}

//==============================================================

void save_record_pulse_config(struct record_pulse_config *config)
{
//	printf(">>>> Save Record Pulse Config<<<<\n");
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "last_write_idx", config->last_write_idx);
    cJSON_AddNumberToObject(root, "last_read_idx", config->last_read_idx);
    cJSON_AddNumberToObject(root, "total", config->total);
    cJSON_AddNumberToObject(root, "last_pulse_count", config->last_pulse_count);
    cJSON_AddNumberToObject(root, "current_pulse_count", config->current_pulse_count);

    char *pulse_counter_dataset = cJSON_PrintUnformatted(root);  
   if (xSemaphoreTake(file_mutex, portMAX_DELAY) == pdTRUE) {
    FILE *f = fopen(RECORD_PULSE_CONFIG_FILE, "w");
    if (f != NULL)
    {
//       fputs(pulse_counter_dataset, f);
       if (fputs(pulse_counter_dataset, f) == EOF) {
                printf("*** save_record_pulse_config --> Error writing to file: %s ***\n", strerror(errno));
                fclose(f);
                xSemaphoreGive(file_mutex);
                free(pulse_counter_dataset);
                cJSON_Delete(root);
                return;
            }
       fclose(f);
    }
    else{
    	 printf ("*** save_record_pulse_config --> File = Null ***\n");
         }
    xSemaphoreGive(file_mutex);
    }
//------------------------------------------------
//        Print Json File
//------------------------------------------------
/*       char *my_json_string = cJSON_Print(root);
        ESP_LOGI(TAG, ">>>save_record_pulse_config<<<\n%s",my_json_string); */     
//------------------------------------------------        
    free(pulse_counter_dataset);
    cJSON_Delete(root);
    
}

void get_record_pulse_config(struct record_pulse_config *config)
{
//	printf(">>>> Get Record Pulse Config<<<<\n");
    xSemaphoreTake(file_mutex,portMAX_DELAY);
    FILE *f = fopen(RECORD_PULSE_CONFIG_FILE, "r");

    if (f == NULL) {
        // Se não conseguir abrir, loga e retorna liberando o mutex
        printf("### get_record_pulse_config --> File = Null ###\n");
        xSemaphoreGive(file_mutex);
        return;
     }
    	vTaskDelay(pdMS_TO_TICKS(10));
        fseek(f, 0L, SEEK_END);
        long size = ftell(f);
        rewind(f);
        
        if (size <= 0) {
        // Arquivo vazio ou erro de tamanho
        fclose(f);
        printf("### get_record_pulse_config --> SIZE = %ld ###\n", size);
        vTaskDelay(pdMS_TO_TICKS(100));
        xSemaphoreGive(file_mutex);
        return;
    }
        
	char buf[256];
    size_t bytes_read = fread(buf, 1, (size_t) (size < (long)sizeof(buf) - 1 ? size : (long)sizeof(buf) - 1), f);
    buf[bytes_read] = '\0';
    // Fecha o arquivo agora que a leitura foi concluída
    fclose(f);
        
 // Parse do JSON
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        printf("### get_record_pulse_config --> JSON Parse Error ###\n");
        xSemaphoreGive(file_mutex);
        return;
    }
    cJSON *item;
//------------------------------------------------
//        Print Json File
//------------------------------------------------
/*        char *my_json_string = cJSON_Print(root);
        ESP_LOGI(TAG, ">>>get_record_pulse_config<<<\n%s",my_json_string);  */     
//------------------------------------------------
        config->last_write_idx = cJSON_GetObjectItem(root, "last_write_idx")->valueint;
        config->last_read_idx = cJSON_GetObjectItem(root, "last_read_idx")->valueint;
        config->total = cJSON_GetObjectItem(root, "total")->valueint;
        config->last_pulse_count = cJSON_GetObjectItem(root, "last_pulse_count")->valueint;
        config->current_pulse_count = cJSON_GetObjectItem(root, "current_pulse_count")->valueint;
        cJSON_Delete(root);


    xSemaphoreGive(file_mutex);
}


//----------------------------------------------------------
//           Energy
//----------------------------------------------------------

/*void save_energy_index_control(struct energy_index_control *config)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "last_write_energy_idx", config->last_write_energy_idx);
    cJSON_AddNumberToObject(root, "last_read_energy_idx", config->last_read_energy_idx);
    cJSON_AddNumberToObject(root, "total_counter", config->total_counter);

    const char *energy_index = cJSON_PrintUnformatted(root);
    vTaskDelay(pdMS_TO_TICKS(20));
    xSemaphoreTake(file_mutex,portMAX_DELAY);
    FILE *f = fopen(ENERGY_INDEX_CONTROL, "w");
    if (f != NULL)
    {
        fprintf(f, energy_index);
        fclose(f);
    }
    else{
    	 printf ("*** save_record_energy_config --> File = Null ***\n");
    	 fclose(f);
         }
    free((void *)energy_index);
    cJSON_Delete(root);
    xSemaphoreGive(file_mutex);
}*/

/*void get_energy_index_control(struct energy_index_control *config)
{
    xSemaphoreTake(file_mutex,portMAX_DELAY);
    FILE *f = fopen(ENERGY_INDEX_CONTROL, "r");

    char buf[256];
    if (f == NULL)
     {
      printf ("### get_record_energy_config --> File = Null\n");
      xSemaphoreGive(file_mutex);
      return;
     
     }
    else
      {
    	vTaskDelay(pdMS_TO_TICKS(10));
        fseek(f, 0L, SEEK_END);
        long size = ftell(f);
        rewind(f);
      if(size>0)
        {
        fread(buf, 255, 1, f);
        buf[size] = '\0';
        fclose(f);
        cJSON *root = cJSON_Parse(buf);
        config->last_write_energy_idx = cJSON_GetObjectItem(root, "last_write_energy_idx")->valueint;
        config->last_read_energy_idx = cJSON_GetObjectItem(root, "last_read_energy_idx")->valueint;
        config->total_counter = cJSON_GetObjectItem(root, "total_counter")->valueint;
        cJSON_Delete(root);
       }
       else
          {
           fclose(f);
       	   printf ("### get_record_energy_config --> SIZE = %d ###\n",size);
           vTaskDelay(pdMS_TO_TICKS(100));
           }
     }

    xSemaphoreGive(file_mutex);
}
*/

//---------------------------------------------------------

void save_system_config(struct system_config *config)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "error_gsm_count", config->connection_gsm_network_error_count);
    cJSON_AddNumberToObject(root, "error_server_count", config->connection_server_error_count);
    cJSON_AddNumberToObject(root, "last_sent_data", config->last_data_sent);
    cJSON_AddNumberToObject(root, "last_sys_time", config->last_sys_time);
    cJSON_AddNumberToObject(root, "modem_enabled", config->modem_enabled ? 1 : 0);
    cJSON_AddNumberToObject(root, "led_enabled", config->led_enabled ? 1 : 0);

    char *system_config = cJSON_PrintUnformatted(root);
    
    if (xSemaphoreTake(file_mutex, portMAX_DELAY) == pdTRUE) {
    FILE *f = fopen(SYSTEM_CONFIG_FILE, "w");
    if (f != NULL)
    {
//        fputs(system_config, f);
        if (fputs(system_config, f) == EOF) {
                printf("*** save_system_config --> Error writing to file: %s ***\n", strerror(errno));
                fclose(f);
                xSemaphoreGive(file_mutex);
                free(system_config);
                cJSON_Delete(root);
                return;
            }
        fclose(f);
    }
    else{
    	 printf ("*** save_system_config --> File = Null ***\n");
         }
     xSemaphoreGive(file_mutex);    
    }
    free(system_config);
    cJSON_Delete(root);
}

void get_system_config(struct system_config *config)
{
    xSemaphoreTake(file_mutex,portMAX_DELAY);
    FILE *f = fopen(SYSTEM_CONFIG_FILE, "r");
    
 if (f == NULL)
   {
   	printf ("### get_system_config --> File = Null\n");
    xSemaphoreGive(file_mutex);
    return;
    }

	    vTaskDelay(pdMS_TO_TICKS(10));
        fseek(f, 0L, SEEK_END);
        long size = ftell(f);
        rewind(f);
        
  if (size <= 0) {
        // Arquivo vazio ou erro de tamanho
        fclose(f);
        printf("### get_system_config --> SIZE = %ld ###\n", size);
        vTaskDelay(pdMS_TO_TICKS(100));
        xSemaphoreGive(file_mutex);
        return;
    }

		char buf[256];
        size_t bytes_read = fread(buf, 1, (size_t) (size < (long)sizeof(buf) - 1 ? size : (long)sizeof(buf) - 1), f);
        buf[bytes_read] = '\0';
    // Fecha o arquivo agora que a leitura foi concluída
        fclose(f);
        
           // Parse do JSON
        cJSON *root = cJSON_Parse(buf);
        if (root == NULL) {
        printf("### get_device_config --> JSON Parse Error ###\n");
        xSemaphoreGive(file_mutex);
        return;
        }

    cJSON *item;

        config->connection_gsm_network_error_count = cJSON_GetObjectItem(root, "error_gsm_count")->valueint;
        config->connection_server_error_count = cJSON_GetObjectItem(root, "error_server_count")->valueint;
        config->last_data_sent = cJSON_GetObjectItem(root, "last_sent_data")->valueint;
        config->last_sys_time = cJSON_GetObjectItem(root, "last_sys_time")->valueint;
        config->modem_enabled = cJSON_GetObjectItem(root, "modem_enabled")->valueint == 1;
        config->led_enabled = cJSON_GetObjectItem(root, "led_enabled")->valueint == 1;
        
    cJSON_Delete(root);
    xSemaphoreGive(file_mutex);
}
//**************
void save_record_last_unit_time(struct record_last_unit_time *config)
{
    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "error_last_minute", config->last_minute);

    char *record_last_unit_time = cJSON_PrintUnformatted(root);
    
    if (xSemaphoreTake(file_mutex, portMAX_DELAY) == pdTRUE) {
    FILE *f = fopen(RECORD_LAST_UNIT_TIME_FILE, "w");
    if (f != NULL)
    {
//        fputs(record_last_unit_time, f);
         if (fputs(record_last_unit_time, f) == EOF) {
                printf("*** save_record_last_unit_time --> Error writing to file: %s ***\n", strerror(errno));
                fclose(f);
                xSemaphoreGive(file_mutex);
                free(record_last_unit_time);
                cJSON_Delete(root);
                return;
            }
        fclose(f);
    }
    else{
    	 printf ("*** save_record_last_unit_time --> File = Null ***\n");
         }
    xSemaphoreGive(file_mutex); 
    }
    free(record_last_unit_time);
    cJSON_Delete(root); 
}

void get_record_last_unit_time(struct record_last_unit_time *config)
{
    xSemaphoreTake(file_mutex,portMAX_DELAY);
    FILE *f = fopen(RECORD_LAST_UNIT_TIME_FILE, "r");
    
 if (f == NULL) {
        // Se não conseguir abrir, loga e retorna liberando o mutex
        printf("### get_record_last_unit_time --> File = Null ###\n");
        xSemaphoreGive(file_mutex);
        return;
    }
    	vTaskDelay(pdMS_TO_TICKS(10));
        fseek(f, 0L, SEEK_END);
        long size = ftell(f);
        rewind(f);
        
      if (size <= 0) {
        // Arquivo vazio ou erro de tamanho
        fclose(f);
        printf("### get_record_last_unit_time --> SIZE = %ld ###\n", size);
        vTaskDelay(pdMS_TO_TICKS(100));
        xSemaphoreGive(file_mutex);
        return;
    }
		char buf[64];
       size_t bytes_read = fread(buf, 1, (size_t) (size < (long)sizeof(buf) - 1 ? size : (long)sizeof(buf) - 1), f);
       buf[bytes_read] = '\0';
       // Fecha o arquivo agora que a leitura foi concluída
       fclose(f);
       
    // Parse do JSON
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        printf("### get_record_last_unit_time --> JSON Parse Error ###\n");
        xSemaphoreGive(file_mutex);
        return;
    }
    cJSON *item;

        config->last_minute = cJSON_GetObjectItem(root, "error_last_minute")->valueint;

    cJSON_Delete(root);
    xSemaphoreGive(file_mutex);
}

//**************

esp_err_t  save_self_monitoring_data(struct self_monitoring_data *config)
{
	if (config == NULL) {
        ESP_LOGE(TAG, "config is NULL");
        return ESP_ERR_INVALID_ARG;
        }
	
    cJSON *root = cJSON_CreateObject();
    
    if (!root) {
        ESP_LOGE(TAG, "cJSON_CreateObject failed");
        return ESP_ERR_NO_MEM;
       }

    cJSON_AddNumberToObject(root, "csq", config->csq);
    cJSON_AddNumberToObject(root, "power_source", config->power_source);
    cJSON_AddNumberToObject(root, "battery", config->battery);
    
    char *self_monitoring_data = cJSON_PrintUnformatted(root);
    if (!self_monitoring_data) {
        ESP_LOGE(TAG, "cJSON_PrintUnformatted failed");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
      }
     
     esp_err_t ret = ESP_OK;
    
    if (xSemaphoreTake(file_mutex, portMAX_DELAY) == pdTRUE) {
    FILE *f = fopen(SELF_MONITORING_DATA, "w");
    if (f != NULL) {
            if (fputs(self_monitoring_data, f) == EOF) {
                printf("*** save_self_monitoring_data --> Error writing to file: %s ***\n", strerror(errno));
                ret = ESP_FAIL;
            } else {
                fflush(f); // garante que dados vão para o backend do FS
                // littlefs não expõe fsync facilmente; fclose costuma flushar
            }
            fclose(f);
        } else {
            printf("*** save_self_monitoring_data --> fopen failed: %s ***\n", strerror(errno));
            ret = ESP_FAIL;
        }
        xSemaphoreGive(file_mutex);
    } else {
        printf("*** save_self_monitoring_data --> failed to take file_mutex ***\n");
        ret = ESP_ERR_TIMEOUT;
    }

    free(self_monitoring_data);
    cJSON_Delete(root);
    return ret;
       
}

esp_err_t load_self_monitoring_data(struct self_monitoring_data *config)
{
	if (config == NULL) {
        ESP_LOGE(TAG, "### get_self_monitoring_data --> config is NULL ###\n");
        return ESP_ERR_INVALID_ARG;
       }
    if (xSemaphoreTake(file_mutex, portMAX_DELAY) != pdTRUE) {
        printf("### get_self_monitoring_data --> failed to take file_mutex ###\n");
        return ESP_ERR_TIMEOUT;
       }
       
    FILE *f = fopen(SELF_MONITORING_DATA, "r");
    
if (f == NULL) {
        printf("### get_self_monitoring_data --> File = Null (%s) ###\n", strerror(errno));
        xSemaphoreGive(file_mutex);
        return ESP_FAIL;
    }

    // lê todo o conteúdo dinamicamente
    if (fseek(f, 0L, SEEK_END) != 0) {
        printf("### get_self_monitoring_data --> fseek failed ###\n");
        fclose(f);
        xSemaphoreGive(file_mutex);
        return ESP_FAIL;
    }
    long size = ftell(f);
    if (size <= 0) {
        printf("### get_self_monitoring_data --> SIZE = %ld ###\n", size);
        fclose(f);
        xSemaphoreGive(file_mutex);
        return ESP_FAIL;
    }
    rewind(f);

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        printf("### get_self_monitoring_data --> malloc failed ###\n");
        fclose(f);
        xSemaphoreGive(file_mutex);
        return ESP_ERR_NO_MEM;
    }

    size_t bytes_read = fread(buf, 1, (size_t)size, f);
    buf[bytes_read] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (root == NULL) {
        printf("### get_self_monitoring_data --> JSON Parse Error ###\n");
        xSemaphoreGive(file_mutex);
        return ESP_FAIL;
    }

    cJSON *item;

    item = cJSON_GetObjectItem(root, "csq");
    if (cJSON_IsNumber(item)) {
        config->csq = item->valueint;
    } else {
        printf("### get_self_monitoring_data --> csq missing or invalid ###\n");
    }

    item = cJSON_GetObjectItem(root, "power_source");
    if (cJSON_IsNumber(item)) {
        config->power_source = item->valueint;
    } else {
        printf("### get_self_monitoring_data --> power_source missing or invalid ###\n");
    }

    item = cJSON_GetObjectItem(root, "battery");
    if (cJSON_IsNumber(item)) {
        config->battery = item->valueint;
    } else {
        printf("### get_self_monitoring_data --> battery missing or invalid ###\n");
    }

    cJSON_Delete(root);
    xSemaphoreGive(file_mutex);
    return ESP_OK;
}

void save_pressure_data(struct pressure_data *config)
{
	 
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "pressure1", config->pressure1);
    cJSON_AddStringToObject(root, "pressure2", config->pressure2);

    char *pressure_dataset = cJSON_PrintUnformatted(root);
    
    if (xSemaphoreTake(file_mutex, portMAX_DELAY) == pdTRUE) {
    FILE *f = fopen(PRESSURE_DATA_SET, "w");
    if (f != NULL)
    {
//        fputs(pressure_dataset, f);
         if (fputs(pressure_dataset, f) == EOF) {
                printf("*** save_pressure_data --> Error writing to file: %s ***\n", strerror(errno));
                fclose(f);
                xSemaphoreGive(file_mutex);
                free(pressure_dataset);
                cJSON_Delete(root);
                return;
            }
        fclose(f);
    }
    else{
    	 printf ("*** pressure_data_set --> File = Null ***\n");
         }
     xSemaphoreGive(file_mutex);    
    }
    
    free((void *) pressure_dataset);
    cJSON_Delete(root);
}

void get_pressure_data(struct pressure_data *config)
{
    xSemaphoreTake(file_mutex,portMAX_DELAY);
    FILE *f = fopen(PRESSURE_DATA_SET, "r");
   
    if (f == NULL) {
        // Se não conseguir abrir, loga e retorna liberando o mutex
        printf("### get_pressure_data --> File = Null ###\n");
        xSemaphoreGive(file_mutex);
        return;
    }
	    vTaskDelay(pdMS_TO_TICKS(10));
        fseek(f, 0L, SEEK_END);
        long size = ftell(f);
        rewind(f);
        
    if (size <= 0) {
        // Arquivo vazio ou erro de tamanho
        fclose(f);
        printf("### get_pressure_data --> SIZE = %ld ###\n", size);
        vTaskDelay(pdMS_TO_TICKS(100));
        xSemaphoreGive(file_mutex);
        return;
    }
		char buf[256];
        size_t bytes_read = fread(buf, 1, (size_t) (size < (long)sizeof(buf) - 1 ? size : (long)sizeof(buf) - 1), f);
        buf[bytes_read] = '\0';
    // Fecha o arquivo agora que a leitura foi concluída
        fclose(f);
        
            // Parse do JSON
        cJSON *root = cJSON_Parse(buf);
        if (root == NULL) {
        printf("### get_pressure_data --> JSON Parse Error ###\n");
        xSemaphoreGive(file_mutex);
        return;
        }
        cJSON *item;

        strcpy(config->pressure1, cJSON_GetObjectItem(root, "pressure1")->valuestring);
        strcpy(config->pressure2, cJSON_GetObjectItem(root, "pressure2")->valuestring);
 
    cJSON_Delete(root);
 
    xSemaphoreGive(file_mutex);
}

//----------------------------------------------------------------

void save_energy_measured(struct energy_measured *config)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "V1", config->V1);
    cJSON_AddNumberToObject(root, "V2", config->V2);
    cJSON_AddStringToObject(root, "last_power_1", config->last_power_1);
    cJSON_AddStringToObject(root, "last_power_2", config->last_power_2);
 
    char *energy_measured = cJSON_PrintUnformatted(root);
    
    if (xSemaphoreTake(file_mutex, portMAX_DELAY) == pdTRUE) {
    FILE *f = fopen(ENERGY_MEASURED, "w");
    if (f != NULL)
    {
//      fputs(energy_measured, f);
         if (fputs(energy_measured, f) == EOF) {
                printf("*** save_energy_measured --> Error writing to file: %s ***\n", strerror(errno));
                fclose(f);
                xSemaphoreGive(file_mutex);
                free(energy_measured);
                cJSON_Delete(root);
                return;
            }
      fclose(f);
    }
    else{
    	 printf ("*** Energy Measured --> File = Null ***\n");
         }
       xSemaphoreGive(file_mutex);   
     }
    free((void *) energy_measured);
    cJSON_Delete(root);   
}

void get_energy_measured(struct energy_measured *config)
{
    xSemaphoreTake(file_mutex,portMAX_DELAY);
    FILE *f = fopen(ENERGY_MEASURED, "r");
    
 if (f == NULL)
   {
    printf ("### energy measured --> File = Null ###\n");
    xSemaphoreGive(file_mutex);
    return;
    }
	    vTaskDelay(pdMS_TO_TICKS(10));
        fseek(f, 0L, SEEK_END);
        long size = ftell(f);
        rewind(f);
        
        if (size <= 0) {
        // Arquivo vazio ou erro de tamanho
        fclose(f);
        printf("### get_energy_measured --> SIZE = %ld ###\n", size);
        vTaskDelay(pdMS_TO_TICKS(100));
        xSemaphoreGive(file_mutex);
        return;
        }
		char buf[256];
        size_t bytes_read = fread(buf, 1, (size_t) (size < (long)sizeof(buf) - 1 ? size : (long)sizeof(buf) - 1), f);
        buf[bytes_read] = '\0';
    // Fecha o arquivo agora que a leitura foi concluída
        fclose(f);
        
            // Parse do JSON
        cJSON *root = cJSON_Parse(buf);
        if (root == NULL) {
        printf("### get_energy_measured --> JSON Parse Error ###\n");
        xSemaphoreGive(file_mutex);
        return;
        }
        cJSON *item;
        config->V1 = cJSON_GetObjectItem(root, "V1")->valuedouble;
        config->V2 = cJSON_GetObjectItem(root, "V2")->valuedouble;

        strcpy(config->last_power_1, cJSON_GetObjectItem(root, "last_power_1")->valuestring);
        strcpy(config->last_power_2, cJSON_GetObjectItem(root, "last_power_2")->valuestring);
     
        cJSON_Delete(root);

        xSemaphoreGive(file_mutex);
}


//----------------------------------------------------------------
//	Verificação de dados do arquifo Little FS
//----------------------------------------------------------------

void listFilesInDir(void)
{
	  xSemaphoreTake(file_mutex,portMAX_DELAY);
    const char path[] = littlefs_base_path;

    DIR* dir = opendir(path);
    if (dir == NULL) {
        return;
    }

    while (true) {
        struct dirent* de = readdir(dir);
        if (!de) {
            break;
        }
        printf("Found file: %s\n", de->d_name);
    }

    closedir(dir);
    xSemaphoreGive(file_mutex);
}

//void test_littlefs_read_file_with_content(const char* filename, const char* expected_content)
void littlefs_read_file_with_content(const char* filename)
{
    FILE* f = fopen(filename, "r");
    TEST_ASSERT_NOT_NULL(f);
    if (f == NULL) {
    	printf("Failed to open file for reading\n");
        return;
    }
    char buf[32] = { 0 };
    char line[64];
    size_t read_length = 0;
    int cb = 0;
    do {
       fgets(line, sizeof(line), f);
       cb = fread(buf, 1, sizeof(buf), f);
       read_length += cb;
       printf("%s\n", line);
    } while(cb != 0);

    fclose(f);

}





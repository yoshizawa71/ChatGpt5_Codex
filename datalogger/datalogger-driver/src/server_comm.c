#include "datalogger_control.h"
#include <stdbool.h>
#include <string.h>
#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"

#include "esp_mac.h"
#include "esp_tls.h"
#include "esp_event.h"

#include "esp_http_client.h"

#include "datalogger_driver.h"
#include "pulse_meter.h"
#include "sdmmc_driver.h"

#define MAX_POINTS_TO_SEND 10
uint32_t cursor_position; 

static const char *TAG = "SERVER_COMM";

// Mutex para proteger o acesso a rec_index
static SemaphoreHandle_t recIndexMutex = NULL;

bool simulate_send_data_to_server(void)
{
    struct record_index_config rec_index = {0};
    uint32_t counter = 0;


    get_index_config(&rec_index);
printf(">>>>>>Total INDEX = %d\n",rec_index.total_idx );  
printf(">>>>>>Cursor Position = %d\n",rec_index.cursor_position ); 
printf(">>>>>>Last Read Index= %d\n",rec_index.last_read_idx );
printf(">>>>>>Last Write Index= %d\n",rec_index.last_write_idx); 
  
char server_payload[5000] = {0};
    
    esp_err_t err = json_data_payload(server_payload, sizeof(server_payload),rec_index, &counter, &cursor_position);
    if (err != ESP_OK) {
        printf("Erro ao montar o payload JSON: %d", err);
        return err;
    }
    printf("Cursor position depois de ser feito o payload====>> %d", cursor_position);

bool sucess = 1; //definir manualmente se é sucesso ou não.

    if(sucess)
    {
    	rec_index.last_read_idx = ((rec_index.last_read_idx + counter) & UNSPECIFIC_RECORD) % rec_index.total_idx; //Caso tenha sucesso no envio, o index � atualizado
        rec_index.cursor_position= cursor_position;

        save_index_config(&rec_index);
        printf("+++++SUCESSO++++\n");
        return true;
    }
    else
    {
		printf("-----Não Teve Sucesso----\n");
        return false;
    }
    
 }

void server_comm_init(void)
{
    if (recIndexMutex == NULL) {
        recIndexMutex = xSemaphoreCreateMutex();
        if (recIndexMutex == NULL) {
            ESP_LOGE(TAG, "Falha ao criar mutex para rec_index");
        }
    }
}

// Adaptação: monta payload customizado incluindo múltiplas leituras SD
// e todos os campos de configuração.
// Usa cJSON e mantém cursor/index para não reenviar dados já enviados.

#define MAX_POINTS_TO_SEND 10  // ou o que for adequado

esp_err_t json_data_payload(char *buf,
                               size_t bufSize,
                               struct record_index_config rec_index,
                               uint32_t *counter_out,
                               uint32_t *cursor_position)
{
    if (!buf || bufSize == 0 || !counter_out || !cursor_position) {
        return ESP_FAIL;
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_FAIL;
    }
    
    
cJSON_AddStringToObject(root, "id", get_device_id());

    
     cJSON_AddStringToObject(root, "Nome", get_name());
     cJSON_AddStringToObject(root, "Número Serial",get_serial_number());
     cJSON_AddStringToObject(root, "Telefone",get_phone());
     cJSON_AddNumberToObject(root, "CSQ",get_csq());
 //    cJSON_AddNumberToObject(root, "battery",get_battery());
     
    if (has_network_user_enabled()&&!has_network_http_enabled()) {
        cJSON_AddStringToObject(root, "Usuario", get_network_user());
    }
    if (has_network_pw_enabled()&&!has_network_http_enabled()) {
        cJSON_AddStringToObject(root, "Senha", get_network_pw());
    }
    if (has_network_token_enabled()&&!has_network_http_enabled()) {
        cJSON_AddStringToObject(root, "Token", get_network_token());
    }
    
    // 2) Preparar array de medições
    cJSON *meas_array = cJSON_CreateArray();
    if (!meas_array) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    cJSON_AddItemToObject(root, "measurements", meas_array);

    // 3) Calcular index inicial
    uint32_t index;
    if (rec_index.total_idx == 0) {
        index = 0;
    } else {
        index = (rec_index.last_read_idx + 1) % rec_index.total_idx;
    }
    *cursor_position = rec_index.cursor_position;

    // 4) Loop de leitura e montagem de cada registro
    while (*counter_out < MAX_POINTS_TO_SEND) {
        struct record_data_saved database;
        // read_record_sd atualiza cursor_position conforme rec_index
        if (read_record_sd(cursor_position, &database) != ESP_OK) {
            ESP_LOGW(TAG, "Falha ao ler registro SD no index %u", index);
            break;
        }
        cJSON *rec_obj = cJSON_CreateObject();
        if (!rec_obj) {
            ESP_LOGE(TAG, "Falha ao criar objeto de medição");
            break;
        }
        cJSON_AddStringToObject(rec_obj, "Data",  database.date);
        cJSON_AddStringToObject(rec_obj, "Hora",  database.time);
        cJSON_AddNumberToObject(rec_obj, "Canal", database.channel);
        cJSON_AddNumberToObject(rec_obj, "Dados", atof(database.data));
        cJSON_AddItemToArray(meas_array, rec_obj);

        (*counter_out)++;

        // Critério de parada: chegou no last_write_idx?
        if (rec_index.last_write_idx == UNSPECIFIC_RECORD) {
            if (index == rec_index.total_idx - 1) {
                break;
            }
        } else {
            if (index == rec_index.last_write_idx) {
                break;
            }
        }
        // garante que não haja divisão por zero
        if (rec_index.total_idx == 0) {
           break;
          }
        index = (index + 1) % rec_index.total_idx;
    }

    // 5) Campos finais e alarmes vazios
/*    cJSON *alarms = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "alarmes", alarms);*/

    // 6) Serializa em buffer
    char *p = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!p) {
        return ESP_FAIL;
    }
    size_t len = strlen(p);
    if (len + 1 > bufSize) {
        free(p);
        return ESP_FAIL;
    }
    memcpy(buf, p, len + 1);
    free(p);
    return ESP_OK;
}


/*esp_err_t json_data_payload(char* server_payload, size_t payload_size, struct record_index_config rec_index,uint32_t* counter_out,uint32_t *cursor_position)
{
	struct record_data_saved database = {0};
	uint32_t index;
	    
    if (!server_payload || payload_size == 0) {
        ESP_LOGE(TAG, "Parâmetros inválidos em json_data_payload");
        return ESP_FAIL;
    }
    
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Falha ao criar objeto cJSON");
  //      xSemaphoreGive(recIndexMutex);
        return ESP_FAIL;
    }

if (rec_index.total_idx == 0) {
	 index = 0;
	}
else{
     index = ((rec_index.last_read_idx + 1) & UNSPECIFIC_RECORD) % rec_index.total_idx;
    printf("------->INDEX  = %d\n", index);
	}
	
*cursor_position = rec_index.cursor_position;

    cJSON_AddStringToObject(root, "id", get_device_id());

    if (network_user_enabled()) {
        cJSON_AddStringToObject(root, "usuario", get_network_user());
    }
    if (network_pw_enabled()) {
        cJSON_AddStringToObject(root, "senha", get_network_pw());
    }
    if (network_key_enabled()) {
        cJSON_AddStringToObject(root, "chave", get_network_token());
    }

    cJSON *meas_array = cJSON_CreateArray();
    if (!meas_array) {
        ESP_LOGE(TAG, "Falha ao criar array cJSON");
        cJSON_Delete(root);
 //       xSemaphoreGive(recIndexMutex);
        return ESP_FAIL;
    } 

    while (*counter_out < MAX_POINTS_TO_SEND) {
        if (read_record_sd(cursor_position, &database) == ESP_OK) {
            cJSON *rec_obj = cJSON_CreateObject();
            if (!rec_obj) {
                ESP_LOGE(TAG, "Falha ao criar objeto de medição cJSON");
                cJSON_Delete(root);
                printf("TAG= server_comm, FALHA CRIAR OBJETO CJSON\n");
                return ESP_FAIL;
            }

            cJSON_AddStringToObject(rec_obj, "Data", database.date);
            cJSON_AddStringToObject(rec_obj, "Hora", database.time);
            cJSON_AddNumberToObject(rec_obj, "Canal", database.channel);
            cJSON_AddStringToObject(rec_obj, "Dados", database.data);

            cJSON_AddItemToArray(meas_array, rec_obj);

            (*counter_out) = (*counter_out) + 1;

            if (rec_index.last_write_idx == UNSPECIFIC_RECORD) {
                if (index == rec_index.total_idx - 1) {
                    ESP_LOGI(TAG, "Index 1 ====> %d", index);
                    break;
                }
            } else {
                if (index == rec_index.last_write_idx) {
                    ESP_LOGI(TAG, "Index 2 ====> %d", index);
                    break;
                }
            }
                    // garante que não haja divisão por zero
          if (rec_index.total_idx == 0) {
           break;
            }
            index = (index + 1) % rec_index.total_idx;
            printf("INDEX ---------> = %d\n", index);
            
        } else {
            ESP_LOGI(TAG, ">>Problema ao ler registro do SD<<");
            cJSON_Delete(root); // Libere o objeto cJSON antes de sair
  //          xSemaphoreGive(recIndexMutex); // Libere o mutex
            return ESP_FAIL; // Retorne imediatamente
        }
          
 
    }//end of while

    cJSON_AddItemToObject(root, "measurements", meas_array);

    char *my_json_string = cJSON_Print(root);
    ESP_LOGI(TAG, "\n>>>ROOT JSON<<<\n%s", my_json_string);
    free(my_json_string);

    char *pBuffer = cJSON_PrintUnformatted(root);
    if (pBuffer) {
        size_t len = strlen(pBuffer);
        if (len >= payload_size) {
            ESP_LOGE(TAG, "Payload excede o tamanho do buffer (%d >= %d)", len, payload_size);
            free(pBuffer);
            cJSON_Delete(root);
 //           xSemaphoreGive(recIndexMutex);
            return ESP_FAIL;
        }
        strcpy(server_payload, pBuffer);
        free(pBuffer);
    } else {
        ESP_LOGE(TAG, "Falha ao criar string JSON não formatada");
        cJSON_Delete(root);
        xSemaphoreGive(recIndexMutex);
        return ESP_FAIL;
    }

    cJSON_Delete(root);

//    xSemaphoreGive(recIndexMutex);
    return ESP_OK;
}*/


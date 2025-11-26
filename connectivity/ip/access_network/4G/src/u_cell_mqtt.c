/*
 * Copyright 2019-2024 u-blox
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/** @brief This example demonstrates how to use the common MQTT API
 * to talk to an MQTT broker on the public internet using a u-blox
 * module.
 *
 * The choice of module and the choice of platform on which this
 * code runs is made at build time, see the README.md for
 * instructions.
 */

// Bring in all of the ubxlib public header files
#include "esp_log.h"
#include "led_blink_control.h"
#include "ubxlib.h"

// Bring in the application settings
#include "u_cfg_app_platform_specific.h"

// For U_SHORT_RANGE_TEST_WIFI()
#include "u_short_range_test_selector.h"

#ifndef U_CFG_DISABLE_TEST_AUTOMATION
// This purely for internal u-blox testing
# include "u_cfg_test_platform_specific.h"
# include "u_wifi_test_cfg.h"
#endif

#include "sara_r422.h"
#include "datalogger_control.h"

#include "cJSON.h"

#include "sdmmc_driver.h"
#include "lte_payload_builder.h"

static const char *TAG = "MQTT_CELL";

/** The string to put at the start of all prints from this test.
 */
#define U_TEST_PREFIX "U_CELL_HTTP_TEST: "

/** Print a whole line, with terminator, prefixed for this test file.
 */
#define U_TEST_PRINT_LINE(format, ...) uPortLog(U_TEST_PREFIX format "\n", ##__VA_ARGS__)

#define U_CFG_TEST_CELL_MODULE_TYPE U_CELL_MODULE_TYPE_SARA_R422

#define UNSPECIFIC_RECORD 0x7FFFFFFFU

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// MQTT broker URL: there is no port number on the end of this URL,
// and hence, conventionally, it does not include TLS security.  You
// may make a secure [D]TLS connection on broker.emqx.io instead
// by editing this code to add [D]TLS security (see below) and
// changing MY_BROKER_NAME to have ":8883" on the end.
//#define MY_BROKER_NAME "mqtt://cogneti.ddns.net:9999"
//#define MY_BROKER "cogneti.ddns.net:9999"
// For u-blox internal testing only
/*#ifdef U_PORT_TEST_ASSERT
# define EXAMPLE_FINAL_STATE(x) U_PORT_TEST_ASSERT(x);
#else
# define EXAMPLE_FINAL_STATE(x)
#endif*/



/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */
//char payload[5000]={0} ;
//char *payload = NULL;
char confirm_topic[64];
bool mqtt_publish_delivery=false;
/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */
// Decide se uma publicação MQTT deve ser retentiva (retain=1) com base no tópico.
// Mantido totalmente local neste módulo para não afetar outros subsistemas.
//
// Estratégia atual:
//  - Tópicos que terminam com "/confirm" serão retentivos (ex.: ".../confirm").
//  - Todos os demais tópicos serão não retentivos.
//
// Isso garante:
//  - Pelo menos 1 tópico retentivo (confirmações/comandos).
//  - Pelo menos 1 tópico não retentivo (telemetria normal).
static bool mqtt_should_retain(const char *topic)
{
    if (topic == NULL) {
        return false;
    }

    const char *suffix_confirm = "/confirm";
    size_t topic_len   = strlen(topic);
    size_t suffix_len  = strlen(suffix_confirm);

    if (topic_len >= suffix_len) {
        const char *tail = topic + (topic_len - suffix_len);
        if (strcmp(tail, suffix_confirm) == 0) {
            // Ex.: "/topic/qos1/confirm" → retentivo
            return true;
        }
    }

    // Demais tópicos (ex.: "/topic/qos1", "/topic/qos1/receive") → não retentivos
    return false;
}


// Callback for unread message indications.
static void messageIndicationCallback(int32_t numUnread, void *pParam)
{
    bool *pMessagesAvailable = (bool *) pParam;

    // It is important to keep stack usage in this callback
    // to a minimum.  If you want to do more than set a flag
    // (e.g. you want to call into another ubxlib API) then send
    // an event to one of your own tasks, where you have allocated
    // sufficient stack, and do those things there.
    uPortLog("The broker says there are %d message(s) unread.\n", numUnread);
    *pMessagesAvailable = true;
}

// FunÃ§Ã£o para processar comandos recebidos via MQTT
// Função para processar comandos recebidos via MQTT
static void process_mqtt_command(const char *command_json, size_t length, uMqttClientContext_t *pContext)
{	
    cJSON *root = cJSON_ParseWithLength(command_json, length);
    if (root == NULL) {
        uPortLog("Failed to parse command JSON: %s\n", command_json);
        return;
    }

    cJSON *command = cJSON_GetObjectItem(root, "command");
    if (command == NULL || !cJSON_IsString(command)) {
        uPortLog("Invalid or missing 'command' field in JSON\n");
        cJSON_Delete(root);
        return;
    }

    const char *cmd_str = command->valuestring;
    uPortLog("Received command: %s\n", cmd_str);

    bool success = false;
    char confirmation_message[128] = {0};

    if (strcmp(cmd_str, "update_config") == 0) {
        cJSON *value = cJSON_GetObjectItem(root, "value");
        if (value != NULL) {
            success = true; // Assume sucesso inicialmente, ajusta se houver falhas

            // Atualizar APN, usuário e senha (se presentes)
            cJSON *apn = cJSON_GetObjectItem(value, "apn");
            if (apn != NULL && cJSON_IsString(apn)) {
                if (strlen(apn->valuestring) < 64) {
                    uPortLog("Updating APN to %s\n", apn->valuestring);
                    set_apn(apn->valuestring);

                } else {
                    uPortLog("APN too long: %s\n", apn->valuestring);
                    success = false;
                }
            }

            cJSON *user = cJSON_GetObjectItem(value, "user");
            if (user != NULL && cJSON_IsString(user)) {
                if (strlen(user->valuestring) < 32) {
                    uPortLog("Updating user to %s\n", user->valuestring);
                    set_lte_user(user->valuestring);

                } else {
                    uPortLog("User too long: %s\n", user->valuestring);
                    success = false;
                }
            }

            cJSON *password = cJSON_GetObjectItem(value, "password");
            if (password != NULL && cJSON_IsString(password)) {
                if (strlen(password->valuestring) < 32) {
                    uPortLog("Updating password to %s\n", password->valuestring);
                    set_lte_pw(password->valuestring);

                } else {
                    uPortLog("Password too long: %s\n", password->valuestring);
                    success = false;
                }
            }

            // Atualizar configurações HTTP (se presentes)
            cJSON *http = cJSON_GetObjectItem(value, "http");
            if (http != NULL) {
                cJSON *http_url = cJSON_GetObjectItem(http, "url");
                if (http_url != NULL && cJSON_IsString(http_url)) {
                    if (strlen(http_url->valuestring) < 128) {
                        uPortLog("Updating HTTP URL to %s\n", http_url->valuestring);
                        set_data_server_url(http_url->valuestring);

                        
                    } else {
                        uPortLog("HTTP URL too long: %s\n", http_url->valuestring);
                        success = false;
                    }
                }

                cJSON *http_port = cJSON_GetObjectItem(http, "port");
                if (http_port != NULL && cJSON_IsNumber(http_port)) {
                    if (http_port->valueint > 0 && http_port->valueint <= 65535) {
                        uPortLog("Updating HTTP port to %d\n", http_port->valueint);
                        set_data_server_port(http_port->valueint);

                    } else {
                        uPortLog("Invalid HTTP port: %d\n", http_port->valueint);
                        success = false;
                    }
                }

                cJSON *http_path = cJSON_GetObjectItem(http, "path");
                if (http_path != NULL && cJSON_IsString(http_path)) {
                    if (strlen(http_path->valuestring) < 64) {
                        uPortLog("Updating HTTP path to %s\n", http_path->valuestring);
                        set_data_server_path(http_path->valuestring);

                    } else {
                        uPortLog("HTTP path too long: %s\n", http_path->valuestring);
                        success = false;
                    }
                }
            }

            // Atualizar configurações MQTT (se presentes)
            cJSON *mqtt = cJSON_GetObjectItem(value, "mqtt");
            if (mqtt != NULL) {
                cJSON *mqtt_url = cJSON_GetObjectItem(mqtt, "url");
                if (mqtt_url != NULL && cJSON_IsString(mqtt_url)) {
                    if (strlen(mqtt_url->valuestring) < 128) {
                        uPortLog("Updating MQTT URL to %s\n", mqtt_url->valuestring);
                        set_mqtt_url(mqtt_url->valuestring);

                        
                    } else {
                        uPortLog("MQTT URL too long: %s\n", mqtt_url->valuestring);
                        success = false;
                    }
                }

                cJSON *mqtt_port = cJSON_GetObjectItem(mqtt, "port");
                if (mqtt_port != NULL && cJSON_IsNumber(mqtt_port)) {
                    if (mqtt_port->valueint > 0 && mqtt_port->valueint <= 65535) {
                        uPortLog("Updating MQTT port to %d\n", mqtt_port->valueint);
                        set_mqtt_port(mqtt_port->valueint);

                    } else {
                        uPortLog("Invalid MQTT port: %d\n", mqtt_port->valueint);
                        success = false;
                    }
                }

                cJSON *mqtt_topic = cJSON_GetObjectItem(mqtt, "topic");
                if (mqtt_topic != NULL && cJSON_IsString(mqtt_topic)) {
                    if (strlen(mqtt_topic->valuestring) < 64) {
                        uPortLog("Updating MQTT topic to %s\n", mqtt_topic->valuestring);
                        set_mqtt_topic(mqtt_topic->valuestring);

                    } else {
                        uPortLog("MQTT topic too long: %s\n", mqtt_topic->valuestring);
                        success = false;
                    }
                }
            }

            // Atualizar frequência de envio de dados (se presente)
            cJSON *data_send_frequency = cJSON_GetObjectItem(value, "data_send_frequency");
            if (data_send_frequency != NULL && cJSON_IsNumber(data_send_frequency)) {
                if (data_send_frequency->valueint >= 1) {
                    uPortLog("Updating data send frequency to %d minutes\n", data_send_frequency->valueint);
                    set_send_period(data_send_frequency->valueint);

                } else {
                    uPortLog("Data send frequency too low: %d minutes\n", data_send_frequency->valueint);
                    success = false;
                }
            }

            // Atualizar tempo de deep sleep (se presente)
            cJSON *deep_sleep_time = cJSON_GetObjectItem(value, "deep_sleep_time");
            if (deep_sleep_time != NULL && cJSON_IsNumber(deep_sleep_time)) {
                if (deep_sleep_time->valueint >= 1) {
                    uPortLog("Updating deep sleep time to %d minutes\n", deep_sleep_time->valueint);
                    set_deep_sleep_period(deep_sleep_time->valueint);

                } else {
                    uPortLog("Deep sleep time too low: %d minutes\n", deep_sleep_time->valueint);
                    success = false;
                }
            }

            // Salvar todas as configuraÃ§Ãµes
            save_config();
            printf("###Configurações Salvas###\n");
            // Processar parâmetros dinâmicos (novos parâmetros adicionados no futuro)
/*            cJSON *param;
            for (param = value->child; param != NULL; param = param->next) {
                const char *param_name = param->string;
                // Ignora parâmetros já processados
                if (strcmp(param_name, "apn") == 0 ||
                    strcmp(param_name, "user") == 0 ||
                    strcmp(param_name, "password") == 0 ||
                    strcmp(param_name, "http") == 0 ||
                    strcmp(param_name, "mqtt") == 0 ||
                    strcmp(param_name, "data_send_frequency") == 0 ||
                    strcmp(param_name, "deep_sleep_time") == 0) {
                    continue;
                }*/

                // Processa novos parâmetros
/*                if (cJSON_IsString(param)) {
                    uPortLog("Processing dynamic parameter: %s = %s\n", param_name, param->valuestring);
                    // Exemplo: salvar o parâmetro no NVS
                    char nvs_key[32];
                    snprintf(nvs_key, sizeof(nvs_key), "dyn_%s", param_name);
                    nvs_handle_t handle;
                    if (nvs_open("datalogger", NVS_READWRITE, &handle) == ESP_OK) {
                        nvs_set_str(handle, nvs_key, param->valuestring);
                        nvs_commit(handle);
                        nvs_close(handle);
                    } else {
                        uPortLog("Failed to save dynamic parameter %s\n", param_name);
                        success = false;
                    }
                } else if (cJSON_IsNumber(param)) {
                    uPortLog("Processing dynamic parameter: %s = %d\n", param_name, param->valueint);
                    char nvs_key[32];
                    snprintf(nvs_key, sizeof(nvs_key), "dyn_%s", param_name);
                    nvs_handle_t handle;
                    if (nvs_open("datalogger", NVS_READWRITE, &handle) == ESP_OK) {
                        nvs_set_i32(handle, nvs_key, param->valueint);
                        nvs_commit(handle);
                        nvs_close(handle);
                    } else {
                        uPortLog("Failed to save dynamic parameter %s\n", param_name);
                        success = false;
                    }
                } else {
                    uPortLog("Unsupported type for dynamic parameter: %s\n", param_name);
                    success = false;
                }
            }*/
        }
    } else if (strcmp(cmd_str, "reboot") == 0) {
        uPortLog("Reboot command received, restarting device...\n");
        success = true;
        // Aqui você pode chamar uma função para reiniciar o dispositivo
        // Por exemplo: esp_restart();
    } else if (strcmp(cmd_str, "set_parameter") == 0) {
        cJSON *value = cJSON_GetObjectItem(root, "value");
        if (value != NULL) {
            cJSON *param = cJSON_GetObjectItem(value, "param");
            cJSON *param_value = cJSON_GetObjectItem(value, "param_value");
            if (param != NULL && cJSON_IsString(param) && param_value != NULL) {
                uPortLog("Setting parameter %s to %s\n", param->valuestring, cJSON_Print(param_value));
                // Aqui você pode chamar uma função para atualizar um parâmetro
                // Exemplo: set_device_parameter(param->valuestring, param_value);
                success = true;
            }
        }
    } else {
        uPortLog("Unknown command: %s\n", cmd_str);
    }

   // Enviar mensagem de confirmaÃ§Ã£o para o tÃ³pico de confirmaÃ§Ã£o
/*    if (pContext != NULL) {
        snprintf(confirmation_message, sizeof(confirmation_message),
                 "{\"status\": \"%s\", \"command\": \"%s\"}",
                 success ? "success" : "failed", cmd_str);
        uPortLog("Publishing confirmation: %s to topic %s (QoS 1 fixo)\n",
                 confirmation_message, confirm_topic);
        uMqttClientPublish(pContext, confirm_topic, confirmation_message, strlen(confirmation_message),
                           U_MQTT_QOS_AT_LEAST_ONCE, false);

    }*/
    
    if (pContext != NULL) {
    snprintf(confirmation_message, sizeof(confirmation_message),
             "{\"status\": \"%s\", \"command\": \"%s\"}",
             success ? "success" : "failed", cmd_str);

    // Para confirmações, queremos que o último estado fique disponível
    // para quem se inscrever depois: usamos retain=true para tópicos
    // que terminam com "/confirm".
    bool retain_flag_confirm = mqtt_should_retain(confirm_topic);

    uPortLog("Publishing confirmation: %s to topic %s (QoS 1 fixo, retain=%d)\n",
             confirmation_message, confirm_topic, retain_flag_confirm ? 1 : 0);

    uMqttClientPublish(pContext,
                       confirm_topic,
                       confirmation_message,
                       strlen(confirmation_message),
                       U_MQTT_QOS_AT_LEAST_ONCE,
                       retain_flag_confirm);
}


    cJSON_Delete(root);
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: THE EXAMPLE
 * -------------------------------------------------------------- */

//void cellMqttClient(uDeviceHandle_t devHandle) {
bool ucell_MqttClient_connection (uDeviceHandle_t devHandle){
	 // --- mede quanta stack a task de URC/evento ainda tem de sobra ---
    TaskHandle_t eventTaskHandle = xTaskGetHandle("eventTask");  // ou o nome real da URC-task
    if (eventTaskHandle != NULL) {
        UBaseType_t highWater = uxTaskGetStackHighWaterMark(eventTaskHandle);
        size_t bytesFree = highWater * sizeof(StackType_t);
        printf("DEBUG: eventTask tem %u bytes livres no pior caso\n",
               (unsigned)bytesFree);
    } else {
        printf(">>>>>DEBUG: não achei a task de URC pelo nome!\n");
    }
	
    uMqttClientContext_t *pContext = NULL;
    uMqttClientConnection_t connection = U_MQTT_CLIENT_CONNECTION_DEFAULT;
    uSecurityTlsSettings_t tlsSettings = U_SECURITY_TLS_SETTINGS_DEFAULT;

    struct record_index_config rec_mqtt_index = {0};
    const size_t MQTT_PAYLOAD_SIZE = 5000;
    char *mqtt_payload = malloc(MQTT_PAYLOAD_SIZE * sizeof(char));
    if (mqtt_payload == NULL) {
        printf("Falha ao alocar memória para mqtt_payload\n");
        return mqtt_publish_delivery=false;  
    }

    uint32_t counter = 0;
    uint32_t cursor_position = 0;
    char buffer[512];
    size_t bufferSize;
    char MY_BROKER[64];
    volatile bool messagesAvailable = false;
    uTimeoutStart_t timeoutStart;
    int32_t returnCode = 0;
    bool success = false;

    // Variável para evitar loops de comandos repetidos
    static char lastCommand[32] = "";
    static uint32_t lastCommandTime = 0;
    const uint32_t COMMAND_COOLDOWN_MS = 5000; // 5 segundos de cooldown

    char* mqtt_url = get_mqtt_url();
    printf(">>>>>>URL MQTT = %s\n", mqtt_url);
    uint16_t mqtt_port = get_mqtt_port();
    printf(">>>>>> Server Port = %d\n", mqtt_port);
    char* topic = get_mqtt_topic();
    printf(">>>>>> Topic = %s\n", topic);
        uint8_t qos_cfg = get_mqtt_qos();
    if (qos_cfg > 2) {
        qos_cfg = 2;
    }
    uMqttQos_t qos_enum = U_MQTT_QOS_AT_MOST_ONCE;
    if (qos_cfg == 1) {
        qos_enum = U_MQTT_QOS_AT_LEAST_ONCE;
    } else if (qos_cfg == 2) {
        qos_enum = U_MQTT_QOS_EXACTLY_ONCE;
    }
     
    char* usuario = get_network_user();
    char* senha = get_network_pw();
	
    // Derivar tópicos: um para envio (publish), outro para recebimento (subscribe) e outro para confirmação
    char send_topic[64];
    char receive_topic[64];
    
    snprintf(send_topic, sizeof(send_topic), "%s", topic); // Ex.: "/topic/qos1"
    snprintf(receive_topic, sizeof(receive_topic), "%s/receive", topic); // Ex.: "/topic/qos1/receive"
    snprintf(confirm_topic, sizeof(confirm_topic), "%s/confirm", topic); // Ex.: "/topic/qos1/confirm"

    snprintf(MY_BROKER, sizeof(MY_BROKER), "%s:%d", mqtt_url, mqtt_port);

    if (mqtt_payload == NULL) {
        printf("Memory allocation failed\n", stderr);
        free(mqtt_payload);
        return mqtt_publish_delivery=false; ;
    }

    get_index_config(&rec_mqtt_index);
    
//    esp_err_t err = json_data_payload(mqtt_payload, MQTT_PAYLOAD_SIZE, rec_mqtt_index, &counter, &cursor_position);
       esp_err_t err = lte_json_data_payload(mqtt_payload,
                         MQTT_PAYLOAD_SIZE,
                         rec_mqtt_index,
                         &counter,
                         &cursor_position);
    if (err != ESP_OK) {
        uPortLog("Erro ao montar o payload JSON: %d\n", err);
        free(mqtt_payload);
        return mqtt_publish_delivery=false; ;
    }   
        
    ESP_LOGI(TAG, "MQTT Payload JSON: %s", mqtt_payload);  

    pContext = pUMqttClientOpen(devHandle, NULL);
    if (pContext != NULL) {
        connection.pBrokerNameStr = MY_BROKER;
        
        if(has_network_user_enabled()){
           connection.pUserNameStr = usuario;
          }
        if(has_network_pw_enabled()){
        connection.pPasswordStr = senha;
        }

        uPortLog("Connecting to MQTT broker \"%s\"with user \"%s\"...\n", MY_BROKER, connection.pBrokerNameStr,
                 connection.pUserNameStr);

        if (uMqttClientConnect(pContext, &connection) == 0) {
						
            uMqttClientSetMessageCallback(pContext, messageIndicationCallback, (void *) &messagesAvailable);

            uPortLog("Subscribing to topic \"%s\" (QoS %u)...\n", receive_topic, qos_cfg);
            if (uMqttClientSubscribe(pContext, receive_topic, qos_enum) >= 0) {
                uPortLog("Publishing \"%s\" to topic \"%s\" (QoS %u)...\n", mqtt_payload, send_topic, qos_cfg);
  //-------------------------------------------------------------------------------                
         /*       timeoutStart = uTimeoutStart();
                if (uMqttClientPublish(pContext, send_topic, mqtt_payload, strlen(mqtt_payload),
                                       qos_enum, false) == 0) {*/
// Decide retenção para o tópico de envio (telemetria):
// neste momento, queremos que o tópico principal de dados NÃO seja retentivo,
// então mqtt_should_retain(send_topic) retornará false (pois não termina com "/confirm").
bool retain_flag_send = mqtt_should_retain(send_topic);

                 timeoutStart = uTimeoutStart();
                 if (uMqttClientPublish(pContext,
                       send_topic,
                       mqtt_payload,
                       strlen(mqtt_payload),
                       qos_enum,
                       retain_flag_send) == 0) {                       
                    success = true;
                    uPortLog("Message successfully published on topic \"%s\".\n", send_topic);
                    
                    size_t payload_len = strlen(mqtt_payload);
                    ESP_LOGI(TAG, "MQTT Payload (%u bytes): %s",(unsigned)payload_len, mqtt_payload);
                    
                    uPortLog("Checking for pending MQTT commands (retain/confirm)...\n");
                    
                    // Processar mensagens recebidas
                    while ((uMqttClientGetUnread(pContext) > 0) && (returnCode == 0)) {
                        bufferSize = sizeof(buffer);
                        returnCode = uMqttClientMessageRead(pContext, receive_topic, sizeof(receive_topic),
                                                            buffer, &bufferSize, NULL);
                        if (returnCode == 0) {
                            uPortLog("New message in topic \"%s\" is %d character(s): \"%.*s\".\n",
                                     receive_topic, bufferSize, bufferSize, buffer);

                            // Verificar se o comando é repetido dentro do cooldown
                            char command[32] = "";
                            // Extrair o comando do JSON (simplificado, você pode usar uma biblioteca JSON como cJSON)
                            if (strstr(buffer, "\"command\":\"update_config\"")) {
                                strncpy(command, "update_config", sizeof(command) - 1);
                            }

                            uint32_t currentTime = uPortGetTickTimeMs();
                            if (strcmp(command, lastCommand) == 0 && 
                                (currentTime - lastCommandTime) < COMMAND_COOLDOWN_MS) {
                                uPortLog("Ignoring repeated command \"%s\" within cooldown period.\n", command);
                                continue;
                            }

                            // Atualizar o último comando processado
                            strncpy(lastCommand, command, sizeof(lastCommand) - 1);
                            lastCommandTime = currentTime;

                            // Processar o comando
                            process_mqtt_command(buffer, bufferSize, pContext);

                           /* // Enviar confirmação para o tópico de confirmação
                            const char *confirmation = "{\"status\": \"success\", \"command\": \"update_config\"}";
                            uPortLog("Publishing confirmation: %s to topic %s\n", confirmation, confirm_topic);
                            if (uMqttClientPublish(pContext, confirm_topic, confirmation, strlen(confirmation),
                                                   U_MQTT_QOS_AT_LEAST_ONCE, false) != 0) {
                                uPortLog("Failed to publish confirmation message!\n");
                            }*/
                        }
                    }
                } else {
                    uPortLog("Unable to publish our message \"%s\"!\n", mqtt_payload);
                   
                }
            } else {
                uPortLog("Unable to subscribe to topic \"%s\"!\n", receive_topic);
            }

            if (uMqttClientDisconnect(pContext) == 0) {
                uPortLog("MQTT disconnected successfully.\n");
            } else {
                uPortLog("Failed to disconnect MQTT, proceeding with caution...\n");
            }
            uPortTaskBlock(500);
        } else {
            uPortLog("Unable to connect to MQTT broker \"%s\"!\n", MY_BROKER);
        }

        if (pContext != NULL) {
            uMqttClientClose(pContext);
            pContext = NULL;
        }
    } else {
        uPortLog("Unable to create MQTT instance!\n");
    }

    if (success) {
	    time_t now;
        time(&now);
        set_last_data_sent(now);
        
        if (rec_mqtt_index.total_idx == 0) {
            printf("Erro: total_idx é zero, evitando divisão por zero\n");
        } else {
            rec_mqtt_index.last_read_idx = ((rec_mqtt_index.last_read_idx + counter) & UNSPECIFIC_RECORD) % rec_mqtt_index.total_idx;
        }
        rec_mqtt_index.cursor_position = cursor_position;

        save_index_config(&rec_mqtt_index);
        printf("++++MQTT SUCESSO++++\n");
        mqtt_publish_delivery=true;
    } else {
        printf("-----Não Teve Sucesso----\n");
        mqtt_publish_delivery=false;
    }
    
    free(mqtt_payload);
    uPortLog("MQTT Done.\n");

    (void) tlsSettings;
    return mqtt_publish_delivery;
}

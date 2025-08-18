
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/apps/sntp.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "comm_wifi.h"
#include "datalogger_control.h"
#include "cJSON.h"
#include "sdmmc_driver.h"

// Ubidots Token e Device Label
char token[] = "BBUS-9fhqdQ7RIzySbihcaLx3NSDhfa9Ntv"; // Seu token
#define DEVICE_LABEL "Smart_data" // Substitua pelo seu device label no Ubidots
//#define VARIABLE_LABEL "volume"
#define MQTT_TOPIC "/v1.6/devices/" DEVICE_LABEL


static const char *TAG = "wifi_mqtt";
static esp_mqtt_client_handle_t mqtt_client = NULL;
static volatile bool publish_confirmed = false; // Sinal para parar o cliente

static void log_error_if_nonzero(const char *message, int error_code) {
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

// Função para inicializar SNTP e obter data/hora
static void initialize_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP...");

    // Verifica se o SNTP já está rodando
    time_t now = 0;
    struct tm timeinfo = { 0 };
    time(&now);
    localtime_r(&now, &timeinfo);

    // Se o ano for inválido (antes de 2016), assume que o SNTP ainda não foi inicializado
    if (timeinfo.tm_year < (2016 - 1900)) {
        // Configura apenas se não estiver inicializado
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, "pool.ntp.org"); // Linha corrigida
        sntp_init();

        // Aguarda sincronização
        int retry = 0;
        const int retry_count = 10;
        while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
            ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            time(&now);
            localtime_r(&now, &timeinfo);
        }
        if (retry < retry_count) {
            ESP_LOGI(TAG, "SNTP synchronized successfully");
        } else {
            ESP_LOGE(TAG, "SNTP synchronization failed");
        }
    } else {
        ESP_LOGI(TAG, "SNTP already initialized by system, skipping reconfiguration");
    }
}

// Função para criar mensagem JSON
static char* create_json_message(int value) {
    cJSON *root = cJSON_CreateObject();

    uint64_t timestamp = get_timestamp_ms();
    ESP_LOGI(TAG, "Timestamp value: %" PRIu64, timestamp); // Log para verificar timestamp

    // Adiciona as variáveis diretamente como chaves no objeto raiz
    cJSON_AddNumberToObject(root, "volume", value); // Sem "value", apenas o número
    cJSON_AddNumberToObject(root, "timestamp", timestamp); // Timestamp global

    // Se precisar adicionar mais variáveis no futuro, pode incluir aqui
    // Exemplo: cJSON_AddNumberToObject(root, "temperature", 20);

    char *json_string = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG, "JSON created: %s", json_string); // Log para verificar o JSON
    cJSON_Delete(root);
    return json_string;
}

// Função para publicar mensagens dinâmicas
static void publish_mqtt_message(void) {
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT client not initialized, cannot publish");
        return;
    }

    // Exemplo: valor inteiro a ser enviado
    int value = 100; // Substitua por seu valor real
    char *json_message = create_json_message(value);
    ESP_LOGI(TAG, "Publishing JSON: %s", json_message);

    // Publica no tópico do Ubidots
    int msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC, json_message, 0, 1, 0);
    if (msg_id >= 0) {
        ESP_LOGI(TAG, "Message sent, msg_id=%d", msg_id);
    } else {
        ESP_LOGE(TAG, "Failed to send message");
    }

    // Libera memória
    free(json_message);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        publish_mqtt_message(); // Publica ao conectar
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        publish_confirmed = true; // Sinaliza que a publicação foi confirmada
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void start_mqtt_client(void) {
    wifi_ap_record_t ap_info;
    esp_err_t sta_status = esp_wifi_sta_get_ap_info(&ap_info);
    printf("STA STATUS=====>%d\n", sta_status);

    if (sta_status != ESP_OK) {
        ESP_LOGW(TAG, "STA not connected to router (status: %d), MQTT will not start", sta_status);
        return;
    }

    ESP_LOGI(TAG, "STA connected to SSID: %s", ap_info.ssid);

    char BROKER_URL[128];
    char* mqtt_url = get_mqtt_url();
    uint16_t mqtt_port = get_mqtt_port();

    snprintf(BROKER_URL, sizeof(BROKER_URL), "mqtt://%s:%d", mqtt_url, mqtt_port);
    ESP_LOGI(TAG, "Broker URL: %s", BROKER_URL);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = BROKER_URL,
            },
        },
        .network = {
            .reconnect_timeout_ms = 5000,
        },
        .session = {
            .keepalive = 30,
        },
        .credentials = {
            .username = token,
        },
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        ESP_LOGE(TAG, "Free heap: %" PRIu32 " bytes", esp_get_free_heap_size());
        ESP_LOGE(TAG, "Last error code: %s", esp_err_to_name(errno));
        return;
    }

    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        return;
    } else {
        ESP_LOGI(TAG, "MQTT client started successfully");
    }

    // Aguarda a publicação ser confirmada ou timeout
    int timeout = 10000; // 10 segundos
    while (!publish_confirmed && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100)); // Espera 100ms por iteração
        timeout -= 100;
    }

    if (publish_confirmed) {
        ESP_LOGI(TAG, "Publication confirmed, stopping MQTT client");
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        ESP_LOGI(TAG, "MQTT client stopped and destroyed");
    } else {
        ESP_LOGE(TAG, "Publication not confirmed within timeout, forcing stop");
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }

    publish_confirmed = false; // Reseta para o próximo envio
}

void init_wifi_mqtt(void) {
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("wifi_mqtt", ESP_LOG_VERBOSE);

    // Inicializar SNTP antes do MQTT
    initialize_sntp();

    start_mqtt_client(); // Chama para cada envio
}
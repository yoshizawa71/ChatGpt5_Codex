/*
 * mqtt_client_esp.h
 *
 *  Created on: 15 de set. de 2025
 *      Author: geopo
 */

#ifndef CONNECTIVITY_IP_MESSAGING_MQTT_WIFI_INCLUDE_MQTT_CLIENT_ESP_H_
#define CONNECTIVITY_IP_MESSAGING_MQTT_WIFI_INCLUDE_MQTT_CLIENT_ESP_H_

#pragma once
#include <stdbool.h>
#include "esp_err.h"

// Handle opaco do cliente MQTT (esp-mqtt) para Wi-Fi
typedef void* mqtt_esp_handle_t;

typedef struct {
    const char *host;          // Ex: "broker.exemplo.com"
    int         port;          // 1883 (sem TLS) / 8883 (TLS)
    bool        use_tls;       // true se for TLS (recomendado quando port=8883)
    const char *ca_cert_pem;   // PEM da CA (NULL => não usa TLS)
    const char *client_id;     // se NULL => usaremos "esp32"
    const char *username;      // pode ser token (Ubidots) ou usuário comum
    const char *password;      // "" quando token
    int         keepalive;     // default 60
    bool        clean_session; // default true
} mqtt_conn_cfg_t;

// Cria e inicia o cliente (faz esp_mqtt_client_start e aguarda CONNECTED)
mqtt_esp_handle_t mqtt_client_esp_create_and_connect(const mqtt_conn_cfg_t *cfg, int timeout_ms);

// Publica e, se QoS=1, bloqueia até PUBACK (ou timeout_ms)
// Retorna ESP_OK em sucesso; ESP_ERR_TIMEOUT se não houve confirmação em QoS1.
esp_err_t mqtt_client_esp_publish(mqtt_esp_handle_t h,
                                  const char *topic,
                                  const char *payload,
                                  int qos,
                                  bool retain,
                                  int timeout_ms,
                                  int *out_msg_id);

// Para e destrói o cliente com segurança
void mqtt_client_esp_stop_and_destroy(mqtt_esp_handle_t h);


#endif /* CONNECTIVITY_IP_MESSAGING_MQTT_WIFI_INCLUDE_MQTT_CLIENT_ESP_H_ */

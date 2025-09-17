/*
 * mqtt_publisher.h
 *  Created on: 15 de set. de 2025
 *      Author: geopo
 */
#ifndef CONNECTIVITY_IP_MESSAGING_MQTT_WIFI_INCLUDE_MQTT_PUBLISHER_H_
#define CONNECTIVITY_IP_MESSAGING_MQTT_WIFI_INCLUDE_MQTT_PUBLISHER_H_
#pragma once

#include "esp_err.h"

// Inicialização do publicador (chame UMA vez no boot do Wi-Fi/MQTT)
void mqtt_publisher_init(void);

// Publica UM pacote agora (monta payload, conecta se preciso e publica)
esp_err_t mqtt_wifi_publish_now(void);

#endif /* CONNECTIVITY_IP_MESSAGING_MQTT_WIFI_INCLUDE_MQTT_PUBLISHER_H_ */

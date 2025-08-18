/*
 * wifi_softap_sta.h
 *
 *  Created on: 15 de ago. de 2025
 *      Author: geopo
 */

#ifndef COMMUNICATIONS_ACCESS_NETWORK_WIFI_INCLUDE_WIFI_SOFTAP_STA_H_
#define COMMUNICATIONS_ACCESS_NETWORK_WIFI_INCLUDE_WIFI_SOFTAP_STA_H_

#include "esp_err.h"
#include <stdbool.h>
#include "esp_wifi.h"

#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
esp_err_t start_wifi_ap_sta(void);
void      stop_wifi_ap_sta(void);
void ap_restart_cb(void* arg);

extern volatile bool sta_connected; // Estado atual da conexão STA
extern volatile bool sta_intentional_disconnect; // Flag de desconexão intencional

// Define o modo e ajusta o power-save apropriadamente.
// - APSTA  -> WIFI_PS_NONE (recomendado p/ estabilidade do AP)
// - STA    -> (opcional) manter NONE ou usar MIN_MODEM se quiser economizar
// - AP     -> NONE
esp_err_t wifi_set_mode_with_ps(wifi_mode_t mode);

#endif /* COMMUNICATIONS_ACCESS_NETWORK_WIFI_INCLUDE_WIFI_SOFTAP_STA_H_ */

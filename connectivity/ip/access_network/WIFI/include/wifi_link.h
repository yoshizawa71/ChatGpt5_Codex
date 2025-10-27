/*
 * wifi_link.h
 *
 *  Created on: 7 de out. de 2025
 *      Author: geopo
 */

#ifndef CONNECTIVITY_IP_ACCESS_NETWORK_WIFI_INCLUDE_WIFI_LINK_H_
#define CONNECTIVITY_IP_ACCESS_NETWORK_WIFI_INCLUDE_WIFI_LINK_H_

#pragma once
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Sobe Wi-Fi em STA(-only) e espera por IP até timeout_ms.
// Se force_sta_only = true, garante que o AP fique OFF (útil para headless).
esp_err_t wifi_link_ensure_ready_sta(int timeout_ms, bool force_sta_only);

// Opcional: retorna true/false se já há IP no STA.
bool wifi_link_has_ip(void);

#ifdef __cplusplus
}
#endif




#endif /* CONNECTIVITY_IP_ACCESS_NETWORK_WIFI_INCLUDE_WIFI_LINK_H_ */

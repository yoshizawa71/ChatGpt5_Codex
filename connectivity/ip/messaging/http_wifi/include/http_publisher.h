/*
 * http_publisher.h
 *
 *  Created on: 7 de out. de 2025
 *      Author: geopo
 */

#ifndef CONNECTIVITY_IP_MESSAGING_HTTP_WIFI_INCLUDE_HTTP_PUBLISHER_H_
#define CONNECTIVITY_IP_MESSAGING_HTTP_WIFI_INCLUDE_HTTP_PUBLISHER_H_

#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Inicializa jitter/diagnósticos (opcional — igual ao mqtt)
void http_publisher_init(void);

// Publica UM pacote agora via HTTP(S) usando a STA conectada.
// Monta o payload a partir do SD (mesma diretiva do MQTT) e, se OK, avança índices.
esp_err_t http_wifi_publish_now(void);

// ====== Getters fracos (override pelo seu front/NVS) ======
const char *get_http_url(void);      // ex.: "https://api.exemplo.com/ingest"
const char *get_http_ca_pem(void);   // PEM opcional; se NULL, usa bundle (se habilitado)
int         http_payload_is_ubidots(void); // 1 se quiser reusar builder Ubidots
int         http_payload_is_weg(void);     // 1 se quiser reusar builder WEG/Losant

#ifdef __cplusplus
}
#endif




#endif /* CONNECTIVITY_IP_MESSAGING_HTTP_WIFI_INCLUDE_HTTP_PUBLISHER_H_ */

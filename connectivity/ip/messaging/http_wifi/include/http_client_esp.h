/*
 * http_client_esp.h
 *
 *  Created on: 7 de out. de 2025
 *      Author: geopo
 */

#ifndef CONNECTIVITY_IP_MESSAGING_HTTP_WIFI_INCLUDE_HTTP_CLIENT_ESP_H_
#define CONNECTIVITY_IP_MESSAGING_HTTP_WIFI_INCLUDE_HTTP_CLIENT_ESP_H_

#pragma once
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *url;         // ex.: "https://api.seuservidor.com/ingest"
    const char *ca_cert_pem; // PEM da CA (NULL => usa bundle se habilitado; em http puro, ignore)
    int         timeout_ms;  // default 10000
    // headers opcionais
    const char *auth_bearer; // ex.: "meu_token" -> envia "Authorization: Bearer <token>"
    const char *basic_user;  // se definidos, envia Authorization: Basic
    const char *basic_pass;
    const char *content_type; // default "application/json"
} http_conn_cfg_t;

/** Envia JSON via HTTP(S) POST e retorna ESP_OK se status 2xx.
 *  Se out_status != NULL, retorna o status HTTP. */
esp_err_t http_client_esp_post_json(const http_conn_cfg_t *cfg,
                                    const char *json,
                                    int *out_status);

#ifdef __cplusplus
}
#endif




#endif /* CONNECTIVITY_IP_MESSAGING_HTTP_WIFI_INCLUDE_HTTP_CLIENT_ESP_H_ */

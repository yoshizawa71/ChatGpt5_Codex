/*
 * http_wifi_cfg.h
 *
 *  Created on: 7 de out. de 2025
 *      Author: geopo
 */

#ifndef CONNECTIVITY_IP_MESSAGING_HTTP_WIFI_INCLUDE_HTTP_WIFI_CFG_H_
#define CONNECTIVITY_IP_MESSAGING_HTTP_WIFI_INCLUDE_HTTP_WIFI_CFG_H_

// connectivity/ip/messaging/http_wifi/include/http_wifi_cfg.h
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Este include deve expor os protótipos que você já tem para
// get_data_server_url/port/path e auth (user/token/pw).
// Use o mesmo header que os outros módulos usam para acessar config_control.*
// Se preferir, troque por "config_control.h".
#include "datalogger_control.h"

// ---- Wrappers HTTP (nomes intuitivos) ----
static inline const char *http_cfg_host(void)             { return get_data_server_url(); }
static inline uint16_t    http_cfg_port(void)             { return get_data_server_port(); }
static inline const char *http_cfg_path(void)             { const char *p = get_data_server_path(); return p ? p : ""; }

// Se um dia você armazenar o PEM no NVS/front, troque aqui.
// Por ora, sem TLS (porta 8899), então NULL.
static inline const char *http_cfg_ca_pem(void)           { return NULL; }

// ---- Auth (reaproveita flags e getters existentes) ----
static inline bool        http_cfg_has_bearer(void)       { return has_network_token_enabled(); }
static inline const char *http_cfg_bearer(void)           { return get_network_token(); }

static inline bool        http_cfg_has_basic(void)        { return has_network_user_enabled(); }
static inline const char *http_cfg_basic_user(void)       { return get_network_user(); }
static inline const char *http_cfg_basic_pass(void)       { return has_network_pw_enabled() ? get_network_pw() : NULL; }


#endif /* CONNECTIVITY_IP_MESSAGING_HTTP_WIFI_INCLUDE_HTTP_WIFI_CFG_H_ */

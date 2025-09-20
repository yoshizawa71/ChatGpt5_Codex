/*
 * tcp_log_server.c
 *
 *  Created on: 14 de ago. de 2025
 *      Author: geopo
 */

#pragma once
#include <stdarg.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Servidor TCP para logs
esp_err_t start_tcp_log_server_task(uint16_t port);
void      stop_tcp_log_server_task(void);
int       tcp_log_vprintf(const char *fmt, va_list args);
bool      tcp_log_client_connected(void);

// Estado do servidor/cliente
bool tcp_log_is_listening(void);
bool tcp_log_has_client(void);

// Checagem ativa de “porta ocupada” (alguém já fez bind na 3333)
bool tcp_log_port_in_use(int port);

// ---- NOVO: Ring buffer + endpoint HTTP ----
// Registra o endpoint GET /logs no seu httpd existente.
typedef void* httpd_handle_t; // forward se o projeto já define, ok manter aqui
esp_err_t tcp_log_register_http_endpoint(void *httpd_handle, const char *path);

// Instala um "tee": log vai para TCP e para o destino anterior (UART ou none).
/*void tcp_log_install_tee(void);
// Remove o tee e restaura o destino anterior.
void tcp_log_uninstall_tee(void);*/

void      tcp_log_force_close_client(void);


#ifdef __cplusplus
}
#endif

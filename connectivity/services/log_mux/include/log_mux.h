/*
 * log_mux.h
 *
 *  Created on: 27 de ago. de 2025
 *      Author: geopo
 */

#ifndef SYSTEM_INCLUDE_LOG_MUX_H_
#define SYSTEM_INCLUDE_LOG_MUX_H_

#pragma once
#include <stdarg.h>
#include "driver/uart.h"

// Writer compatível com esp_log_set_vprintf()
typedef int (*vprintf_like_t)(const char *fmt, va_list ap);

// Inicia o roteador (passe o writer do console TCP)
void logmux_init(vprintf_like_t tcp_writer);

// Liga/desliga cada "sink"
void logmux_enable_uart(bool on);   // UART0 (serial/TeraTerm)
void logmux_enable_tcp(bool on);    // Console TCP

// Notifica quando o RS-485 está ocupando um UART
void logmux_notify_rs485_active(uart_port_t uart, bool active);

// (Opcional) restaurar vprintf original do IDF
void logmux_restore(void);
void logmux_set_tcp_writer(vprintf_like_t tcp_writer);
void logmux_uart_mute(bool on);   // mute temporário da UART0 (por transação




#endif /* SYSTEM_INCLUDE_LOG_MUX_H_ */

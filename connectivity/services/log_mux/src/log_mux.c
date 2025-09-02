/*
 * log_mux.c
 *
 *  Created on: 27 de ago. de 2025
 *      Author: geopo
 */

#include "log_mux.h"
#include "esp_log.h"
#include <stdarg.h>
#include <stdbool.h>

static vprintf_like_t s_prev_vprintf = NULL; // UART0 padrão do IDF
static vprintf_like_t s_tcp_writer   = NULL; // seu writer TCP
static bool s_sink_uart = true;              // duplicação ON
static bool s_sink_tcp  = true;              // duplicação ON
static bool s_rs485_on_uart0 = false;        // RS-485 ativo no UART0?
static int  s_uart_mute_depth = 0;           // mute temporário (aninhável)

static __attribute__((noinline)) int call_raw(vprintf_like_t fn, const char *fmt, va_list ap) {
    return fn ? fn(fmt, ap) : 0;
}

static inline bool uart_muted_now(void) {
    // Mudo se: RS-485 ativo no UART0, OU mute temporário >0, OU sink UART desabilitado
    return s_rs485_on_uart0 || (s_uart_mute_depth > 0) || !s_sink_uart;
}

static int logmux_vprintf(const char *fmt, va_list ap)
{
    int ret = 0;

    if (s_sink_tcp && s_tcp_writer) {
        va_list ap_tcp; va_copy(ap_tcp, ap);
        ret = call_raw(s_tcp_writer, fmt, ap_tcp);
        va_end(ap_tcp);
    }

    if (!uart_muted_now() && s_prev_vprintf) {
        va_list ap_uart; va_copy(ap_uart, ap);
        ret = call_raw(s_prev_vprintf, fmt, ap_uart);
        va_end(ap_uart);
    }

    return ret;
}

void logmux_init(vprintf_like_t tcp_writer)
{
    if (tcp_writer) s_tcp_writer = tcp_writer;
    if (!s_prev_vprintf) {
        s_prev_vprintf = esp_log_set_vprintf(logmux_vprintf);
    }
}

void logmux_set_tcp_writer(vprintf_like_t tcp_writer) { s_tcp_writer = tcp_writer; }
void logmux_enable_uart(bool on) { s_sink_uart = on; }
void logmux_enable_tcp(bool on)  { s_sink_tcp  = on; }

void logmux_notify_rs485_active(uart_port_t uart, bool active)
{
    if (uart == UART_NUM_0) s_rs485_on_uart0 = active;
}

// >>> NOVA: mute temporário por transação (aninhável)
void logmux_uart_mute(bool on)
{
    if (on) {
        ++s_uart_mute_depth;
    } else if (s_uart_mute_depth > 0) {
        --s_uart_mute_depth;
    }
}

void logmux_restore(void)
{
    if (s_prev_vprintf) {
        esp_log_set_vprintf(s_prev_vprintf);
        s_prev_vprintf = NULL;
    }
    s_tcp_writer = NULL;
    s_sink_uart = true;
    s_sink_tcp  = true;
    s_rs485_on_uart0 = false;
    s_uart_mute_depth = 0;
}


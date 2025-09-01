/*
 * modbus.h
 *
 *  Created on: 25 de mar. de 2025
 *      Author: geopo
 */

#include "esp_err.h"


#ifndef MAIN_INCLUDE_MODBUS_H_
#define MAIN_INCLUDE_MODBUS_H_



esp_err_t start_tcp_log_server(void);
int tcp_log_vprintf(const char *fmt, va_list ap);
#endif /* MAIN_INCLUDE_MODBUS_H_ */

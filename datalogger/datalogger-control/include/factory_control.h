/*
 * factory_control.h
 *
 *  Created on: 14 de set. de 2025
 *      Author: geopo
 */

#ifndef DATALOGGER_DATALOGGER_CONTROL_INCLUDE_FACTORY_CONTROL_H_
#define DATALOGGER_DATALOGGER_CONTROL_INCLUDE_FACTORY_CONTROL_H_


#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tcp_log_server.h"

//void update_last_interaction(void);
void update_last_interaction_real(void);  // implementação real (sem log)
TickType_t get_factory_routine_last_interaction(void);

// ======= NOVAS (rastreamento) =======
void update_last_interaction_tracked(const char *file, int line, const char *func);
int64_t get_factory_routine_last_interaction_us(void); // se ainda não tiver
void factory_dump_last_interaction_origin(void);       // loga o último caller

// Intercepta TODAS as chamadas no código:
#define update_last_interaction() update_last_interaction_tracked(__FILE__, __LINE__, __func__)

void wifi_portal_on_ap_start(void); 
// Função separada para parar o servidor HTTP (chamada em outro contexto se necessário)
void stop_http_server(httpd_handle_t server);

void Factory_Config_Task(void* pvParameters);

void init_factory_task(void);

void deinit_factory_task(void);




#endif /* DATALOGGER_DATALOGGER_CONTROL_INCLUDE_FACTORY_CONTROL_H_ */

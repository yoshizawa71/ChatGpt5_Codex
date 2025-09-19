/*
 * sleep_preparation.h
 *
 *  Created on: 18 de set. de 2025
 *      Author: geopo
 */

#ifndef DATALOGGER_DATALOGGER_CONTROL_INCLUDE_SLEEP_PREPARATION_H_
#define DATALOGGER_DATALOGGER_CONTROL_INCLUDE_SLEEP_PREPARATION_H_

#pragma once
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Retorna true se o STA está conectado a um AP. */
bool sta_is_connected(void);

/** Epílogo comum antes de dormir / sair do modo ativo.
 *
 * O que esta função SEMPRE faz (se existir no seu projeto):
 *  - Coloca o RS-485 em idle (DE/RE=0) e drena a UART do RS-485.
 *  - (Opcional) De-inicializa o Modbus master.
 *  - Desabilita o console TCP e devolve logs à UART.
 *  - Pequenos "delays" para drenar buffers.
 *
 * O que ELA PODE fazer, conforme o parâmetro:
 *  - Se maybe_stop_wifi == true: tenta parar o Wi-Fi (AP/STA), quando presente.
 *  - Se maybe_stop_wifi == false: não mexe no Wi-Fi.
 *
 * Observação importante:
 *  - Parar o servidor HTTP (factory server) **NÃO** é responsabilidade desta função,
 *    porque o servidor só existe no caminho do EXIT. Faça isso no shutdown_task().
 */
void sleep_prepare(bool maybe_stop_wifi);

#ifdef __cplusplus
}
#endif




#endif /* DATALOGGER_DATALOGGER_CONTROL_INCLUDE_SLEEP_PREPARATION_H_ */

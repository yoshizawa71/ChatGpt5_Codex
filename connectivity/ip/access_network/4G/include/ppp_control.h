/*
 * ppp_control.h
 *
 * Camada de controle superior do modo PPP LTE usando SARA-R4/R41/R412/R422.
 * Implementa:
 *  - Fila de comandos
 *  - Máquina de estados
 *  - Task dedicada
 *  - Eventos públicos (ESP-IDF EventLoop)
 *
 *  Autor: ChatGPT (Enterprise Edition)
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
//  EVENTOS PPP CONTROL (usados para app_main ou qualquer módulo interessado)
// -----------------------------------------------------------------------------
ESP_EVENT_DECLARE_BASE(PPP_CTRL_EVENT);

// Tipos de eventos publicados no EventLoop
typedef enum {
    PPP_CTRL_EVENT_STARTING = 1,     // ppp_control recebeu comando START
    PPP_CTRL_EVENT_CONNECTED,        // PPP entrou no estado CONNECTED (IP OK)
    PPP_CTRL_EVENT_STOPPING,         // Recebeu comando STOP
    PPP_CTRL_EVENT_STOPPED,          // PPP caiu / desconectado / desligado
    PPP_CTRL_EVENT_FAILED            // Falha ao conectar ou link caiu
} ppp_ctrl_event_t;

// -----------------------------------------------------------------------------
//  ESTADO PPP CONTROL (estado interno da task, mas pode ser consultado)
// -----------------------------------------------------------------------------
typedef enum {
    PPPC_STATE_DISCONNECTED = 0,
    PPPC_STATE_STARTING,
    PPPC_STATE_CONNECTED,
    PPPC_STATE_STOPPING,
    PPPC_STATE_FAILED
} ppp_ctrl_state_t;

// -----------------------------------------------------------------------------
//  API PÚBLICA
// -----------------------------------------------------------------------------

/**
 * @brief Inicializa o módulo PPP Control.
 *
 * Cria fila + task e deixa o sistema pronto para comandos START/STOP.
 */
esp_err_t ppp_control_init(void);

/**
 * @brief Solicita início do PPP LTE (assíncrono).
 *
 * Apenas envia comando para a fila. A task interna executará a lógica.
 */
esp_err_t ppp_control_start(void);

/**
 * @brief Solicita parada completa do PPP LTE (assíncrono).
 */
esp_err_t ppp_control_stop(void);

/**
 * @brief Encerra COMPLETAMENTE o PPP Control (destrói a task).
 *
 * Muito útil para desligar tudo antes de deep sleep total.
 */
esp_err_t ppp_control_shutdown(void);

/**
 * @brief Retorna o estado interno atual.
 */
ppp_ctrl_state_t ppp_control_get_state(void);

/**
 * @brief Indica se o PPP está conectado (estado CONNECTED).
 */
bool ppp_control_is_connected(void);

/**
 * @brief Configura se o PPP Control deve tentar reconectar automaticamente.
 *
 * Nesta versão de teste, é apenas guardado internamente (não há lógica de
 * reconexão implementada ainda). Mantido para compatibilidade com o teste.
 */
void ppp_control_set_auto_reconnect(bool enable);

/**
 * @brief Define parâmetros de retentativa (máximo de tentativas e atraso).
 *
 * Também apenas armazenado internamente nessa primeira versão.
 */
void ppp_control_set_retry_params(int max_retries, int retry_delay_ms);

/**
 * @brief Obtém o esp_netif associado ao PPP.
 *
 * Pode ser NULL caso a versão da ubxlib não exponha esse recurso.
 */
esp_netif_t *ppp_control_get_netif(void);

#ifdef __cplusplus
}
#endif

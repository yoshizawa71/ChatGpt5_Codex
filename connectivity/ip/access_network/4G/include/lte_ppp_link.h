/*
 * lte_ppp_link.h
 *
 * Camada de abstração para o link PPP via LTE (SARA),
 * usando ubxlib e ESP-IDF, sem interferir na lógica Wi-Fi.
 *
 * Este módulo:
 *  - NÃO inicializa uPort/uDevice nem abre o device;
 *    apenas usa um uDeviceHandle_t fornecido pelo chamador.
 *  - Lê APN/usuário/senha através de get_apn(), get_lte_user(), get_lte_pw()
 *    (definidos em datalogger_control.h).
 *  - Sobe e derruba a interface PPP via uNetworkInterfaceUp()/Down().
 *
 * Pensado para encaixar depois em um "network manager" central.
 */

#ifndef LTE_PPP_LINK_H
#define LTE_PPP_LINK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "esp_err.h"
#include "esp_netif.h"   // <-- importante para ter esp_netif_t

// ubxlib: traz uDeviceHandle_t, uNetworkCfgCell_t, etc.
#include "ubxlib.h"

/**
 * @brief Estados de alto nível do link PPP LTE.
 */
typedef enum {
    LTE_PPP_STATE_IDLE = 0,      ///< Ainda não inicializado / desconectado
    LTE_PPP_STATE_NET_BRINGUP,   ///< uNetworkInterfaceUp() em andamento
    LTE_PPP_STATE_CONNECTED,     ///< PPP ativo e rede celular conectada
    LTE_PPP_STATE_ERROR          ///< Erro na última tentativa
} lte_ppp_state_t;

/**
 * @brief Callback para notificação de mudança de estado.
 *
 * Útil para um futuro "network manager".
 */
typedef void (*lte_ppp_event_cb_t)(lte_ppp_state_t new_state);

/**
 * @brief Inicializa o módulo LTE PPP.
 *
 * Não sobe rede ainda, apenas zera estados internos.
 * Não chama uPortInit()/uDeviceInit().
 *
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t lte_ppp_init(void);

/**
 * @brief Define o handle do device LTE (SARA) já aberto.
 *
 * O chamador é responsável por:
 *  - chamar uPortInit();
 *  - chamar uDeviceInit();
 *  - abrir o device com uDeviceOpen() e passar o handle aqui.
 *
 * @param devHandle Handle retornado por uDeviceOpen().
 */
void lte_ppp_set_device_handle(uDeviceHandle_t devHandle);

/**
 * @brief Registra um callback para eventos de estado do PPP.
 *
 * @param cb Ponteiro para callback, ou NULL para remover.
 */
void lte_ppp_register_callback(lte_ppp_event_cb_t cb);

/**
 * @brief Sobe o link PPP LTE via ubxlib (uNetworkInterfaceUp).
 *
 * Fluxo esperado:
 *  - SARA já ligado (hardware/energia ok);
 *  - uPortInit()/uDeviceInit() já chamados;
 *  - uDeviceOpen() já feito e handle setado em lte_ppp_set_device_handle();
 *  - APN/USER/PASS disponíveis via get_apn()/get_lte_user()/get_lte_pw().
 *
 * Esta função é síncrona: bloqueia até timeoutSeconds ou sucesso/erro.
 *
 * @return ESP_OK se a rede foi conectada; ESP_FAIL em caso de erro.
 */
esp_err_t lte_ppp_start(void);

/**
 * @brief Derruba o link PPP LTE via ubxlib (uNetworkInterfaceDown).
 *
 * Não fecha o device (uDeviceClose), apenas derruba a interface de rede.
 */
void lte_ppp_stop(void);

/**
 * @brief Verifica se o PPP LTE está conectado.
 *
 * @return true se estado == LTE_PPP_STATE_CONNECTED.
 */
bool lte_ppp_is_connected(void);

/**
 * @brief Retorna o último estado conhecido do PPP LTE.
 */
lte_ppp_state_t lte_ppp_get_state(void);

/**
 * @brief Obtém o esp_netif associado ao PPP LTE (se disponível).
 *
 * @return ponteiro para esp_netif_t ou NULL se indisponível.
 */
esp_netif_t *lte_ppp_get_netif(void);

#ifdef __cplusplus
}
#endif

#endif /* LTE_PPP_LINK_H */

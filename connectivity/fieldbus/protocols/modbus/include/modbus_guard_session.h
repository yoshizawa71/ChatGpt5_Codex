/*
 * modbus_guard_session.h
 *
 *  Created on: 17 de set. de 2025
 *      Author: geopo
 */

#ifndef CONNECTIVITY_FIELDBUS_PROTOCOLS_MODBUS_SRC_MODBUS_GUARD_SESSION_H_
#define CONNECTIVITY_FIELDBUS_PROTOCOLS_MODBUS_SRC_MODBUS_GUARD_SESSION_H_

#pragma once
/**
 * Modbus Session Guard — abre/fecha uma "janela" curta e exclusiva para o Modbus.
 *
 * Objetivos:
 *  - Exclusividade entre tasks (mutex + refcount).
 *  - Opcionalmente silenciar logs durante a sessão (reduz colisão com UART0).
 *  - Executar callbacks de preparo/flush/idle do RS-485.
 *  - Usar modbus_master_init()/deinit() por sessão, ou "init_once" (configurável).
 *
 * Uso típico:
 *   // 1) Em algum init do sistema (uma vez):
 *   mb_session_config_t cfg;
 *   mb_session_config_defaults(&cfg);
 *   cfg.prepare_hw  = rs485_hw_prepare_for_modbus; // opcional
 *   cfg.flush_hw    = rs485_hw_flush;              // opcional
 *   cfg.idle_hw     = rs485_hw_set_idle_rx;        // opcional
 *   cfg.init_once   = false;                       // true => mantém master ligado
 *   cfg.silence_level = ESP_LOG_WARN;              // reduz ruído durante a sessão
 *   mb_session_setup(&cfg);
 *
 *   // 2) Ao ler (ex.: 1x/min):
 *   if (mb_session_begin(pdMS_TO_TICKS(500)) == ESP_OK) {
 *       // ... leituras Modbus rápidas ...
 *       mb_session_end();
 *   }
 *
 *   // Alternativa "estilo RAII":
 *   MB_SESSION_WITH(pdMS_TO_TICKS(500)) {
 *       // ... leituras Modbus rápidas ...
 *   }
 */

#include <stdbool.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

/* ---- Assinaturas padrão (usadas se você não fornecer callbacks) ----
 * Se seu projeto já tem essas funções, basta linkar normalmente.
 * Se não tiver, deixe os ponteiros em NULL e o guard não chamará nada.
 */
 
 // ===== Feature flags (podem vir do menuconfig ou do CMake) =====


 
#ifdef __cplusplus
extern "C" {
#endif


#if !CONFIG_MODBUS_ENABLE

typedef struct {
    esp_err_t (*prepare_hw)(void);
    void      (*flush_hw)(void);
    esp_err_t (*idle_hw)(void);
    esp_err_t (*modbus_init)(void);
    esp_err_t (*modbus_deinit)(void);
    bool init_once;
    esp_log_level_t silence_level;
} mb_session_config_t;

static inline void mb_session_config_defaults(mb_session_config_t *cfg) { if (cfg) { cfg->prepare_hw=NULL; cfg->flush_hw=NULL; cfg->idle_hw=NULL; cfg->modbus_init=NULL; cfg->modbus_deinit=NULL; cfg->init_once=false; cfg->silence_level=ESP_LOG_WARN; } }
static inline esp_err_t mb_session_setup(const mb_session_config_t *cfg) { (void)cfg; return ESP_OK; }
static inline esp_err_t mb_session_begin(TickType_t max_wait_ticks) { (void)max_wait_ticks; return ESP_ERR_NOT_SUPPORTED; }
static inline void      mb_session_end(void) {}
static inline bool      mb_session_is_active(void) { return false; }
#define MB_SESSION_WITH(wait_ticks) for (int _mb_once = 0; _mb_once; _mb_once = 0)

#else   // CONFIG_MODBUS_ENABLE == 1

// Do seu stack Modbus (se existir)
esp_err_t modbus_master_init(void);
esp_err_t modbus_master_deinit(void);

/* ---- Configuração do guard ---- */
typedef struct {
    // Callbacks de hardware RS-485 (opcionais)
    esp_err_t (*prepare_hw)(void);   // configurar UART/RS-485 p/ Modbus
    void      (*flush_hw)(void);     // drenar buffers RX/TX
    esp_err_t (*idle_hw)(void);      // colocar DE/RE em idle (driver off)
    // Callbacks Modbus (opcionais; se NULL, usam modbus_master_init/deinit)
    esp_err_t (*modbus_init)(void);
    esp_err_t (*modbus_deinit)(void);
    // Política de ciclo de vida do master:
    //  false => init/deinit a cada sessão;
    //  true  => init uma vez na 1ª sessão e mantém (não chama deinit no end).
    bool init_once;
    // Nível de log a aplicar durante a sessão (para reduzir ruído na UART0).
    // Use ESP_LOG_NONE/ERROR/WARN/INFO...  (padrão: ESP_LOG_WARN)
    esp_log_level_t silence_level;
} mb_session_config_t;

/* Preenche 'cfg' com valores padrão seguros. */
void mb_session_config_defaults(mb_session_config_t *cfg);
esp_err_t mb_session_setup(const mb_session_config_t *cfg);

#if CONFIG_MODBUS_GUARD_ENABLE
/* Inicia uma sessão exclusiva. Retorna ESP_OK ao conseguir o mutex. */
esp_err_t mb_session_begin(TickType_t max_wait_ticks);

/* Encerra a sessão (sempre chame, mesmo em erro dentro da sessão). */
void mb_session_end(void);

/* Estado atual (true se alguma sessão está ativa). */
bool mb_session_is_active(void);

#define MB_SESSION_WITH(wait_ticks) \
    for (int _mb_once = (mb_session_begin((wait_ticks)) == ESP_OK); _mb_once; (mb_session_end(), _mb_once = 0))

// --------- Guard DESATIVADO (init/deinit direto por sessão) ----------
#else  // CONFIG_MODBUS_GUARD_ENABLE == 0
static inline esp_err_t mb_session_begin(TickType_t max_wait_ticks)
{
    (void)max_wait_ticks;
    return modbus_master_init();
}

static inline void mb_session_end(void)
{
    (void)modbus_master_deinit();
}

static inline bool mb_session_is_active(void) { return false; }

#define MB_SESSION_WITH(wait_ticks) \
    for (int _mb_once = (mb_session_begin((wait_ticks)) == ESP_OK); _mb_once; (mb_session_end(), _mb_once = 0))

#endif // CONFIG_MODBUS_GUARD_ENABLE

#endif // CONFIG_MODBUS_ENABLE

#ifdef __cplusplus
}
#endif




#endif /* CONNECTIVITY_FIELDBUS_PROTOCOLS_MODBUS_SRC_MODBUS_GUARD_SESSION_H_ */

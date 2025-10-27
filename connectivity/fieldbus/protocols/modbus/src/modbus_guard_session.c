/*
 * modbus_guard_session.c
 *
 *  Created on: 17 de set. de 2025
 *      Author: geopo
 */
 
 /*
 
 Abre uma janelinha exclusiva (mutex + refcount) para fazer leituras rápidas, 
 silencia logs durante a sessão (evita colisão com UART0), executa callbacks de 
 preparo/flush/idle do RS-485 e pode inicializar/deinicializar o master a cada
 sessão ou manter o master ligado (policy init_once).
 
 */
// #include "sdkconfig.h"
#include "modbus_guard_session.h"
#if CONFIG_MODBUS_SERIAL_ENABLE && CONFIG_MODBUS_GUARD_ENABLE


#include <string.h>
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "MB/SESS";

/* ---- Estado interno ---- */
static mb_session_config_t s_cfg;
static bool                s_cfg_set = false;

static SemaphoreHandle_t   s_mutex   = NULL;
static int                 s_refcnt  = 0;
static bool                s_modbus_initialized = false;

#ifndef CONFIG_LOG_DEFAULT_LEVEL
// fallback: se não existir a macro do projeto
#define CONFIG_LOG_DEFAULT_LEVEL ESP_LOG_INFO
#endif

static esp_log_level_t     s_prev_level = CONFIG_LOG_DEFAULT_LEVEL;

/* ---- Helpers ---- */
static inline void ensure_mutex(void) {
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
}

void mb_session_config_defaults(mb_session_config_t *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->init_once     = false;          // por sessão: init/deinit a cada begin/end
    cfg->silence_level = ESP_LOG_WARN;   // reduz ruído durante a sessão
    // demais callbacks ficam NULL (usarão defaults quando aplicável)
}

esp_err_t mb_session_setup(const mb_session_config_t *cfg)
{
    ensure_mutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    if (cfg) {
        s_cfg = *cfg;
    } else {
        mb_session_config_defaults(&s_cfg);
    }
    s_cfg_set = true;

    // nível de log "anterior" (para restauração)
    s_prev_level = CONFIG_LOG_DEFAULT_LEVEL;
    return ESP_OK;
}

static inline esp_err_t call_or_default_init(void)
{
    if (s_cfg.modbus_init)  return s_cfg.modbus_init();
    // default: usar a função global se existir
    return modbus_master_init();
}

static inline esp_err_t call_or_default_deinit(void)
{
    if (s_cfg.modbus_deinit) return s_cfg.modbus_deinit();
    // default: usar a função global se existir
    return modbus_master_deinit();
}

esp_err_t mb_session_begin(TickType_t max_wait_ticks)
{
    if (!s_cfg_set) {
        // Config padrão, se esqueceram de chamar setup()
        mb_session_config_defaults(&s_cfg);
        s_cfg_set = true;
    }
    ensure_mutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    if (xSemaphoreTake(s_mutex, max_wait_ticks) != pdTRUE) {
        ESP_LOGW(TAG, "timeout aguardando exclusividade do Modbus");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = ESP_OK;

    if (s_refcnt == 0) {
        // 1) Silencia logs durante a janela Modbus (reduz colisão com UART0)
        esp_log_level_set("*", s_cfg.silence_level);

        // 2) Sobe o Modbus master conforme a política (ANTES do prepare_hw)
        bool did_fresh_init = false;
        if (s_cfg.init_once) {
            if (!s_modbus_initialized) {
                err = call_or_default_init();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "modbus_init falhou: %s", esp_err_to_name(err));
                    esp_log_level_set("*", s_prev_level);
                    xSemaphoreGive(s_mutex);
                    return err;
                }
                s_modbus_initialized = true;
                did_fresh_init = true;
            }
        } else {
            err = call_or_default_init();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "modbus_init falhou: %s", esp_err_to_name(err));
                esp_log_level_set("*", s_prev_level);
                xSemaphoreGive(s_mutex);
                return err;
            }
            did_fresh_init = true;
        }

        // 3) Agora sim, preparar o HW RS-485 (usa driver já instalado)
        if (s_cfg.prepare_hw) {
            err = s_cfg.prepare_hw();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "prepare_hw falhou: %s", esp_err_to_name(err));
                // tenta deixar o transceiver em estado seguro
                if (s_cfg.idle_hw) (void)s_cfg.idle_hw();

                // se nós inicializamos agora, desfaz (evita "master" pendurado)
                if (did_fresh_init) {
                    (void)call_or_default_deinit();
                    if (s_cfg.init_once) s_modbus_initialized = false;
                }

                esp_log_level_set("*", s_prev_level);
                xSemaphoreGive(s_mutex);
                return err;
            }
        }
    }

    s_refcnt++;
    // mutex permanece tomado até mb_session_end()
    return ESP_OK;
}


void mb_session_end(void)
{
    if (!s_mutex) return;

    if (s_refcnt <= 0) {
        // uso incorreto (end sem begin)
        ESP_LOGW(TAG, "mb_session_end() sem begin correspondente");
        xSemaphoreGive(s_mutex);
        return;
    }

    s_refcnt--;

    if (s_refcnt == 0) {
        // 1) Drenar buffers e colocar transceiver em estado seguro
        if (s_cfg.flush_hw) s_cfg.flush_hw();
        if (s_cfg.idle_hw)  (void)s_cfg.idle_hw();

        // 2) Política de deinit
        if (!s_cfg.init_once) {
            (void)call_or_default_deinit();
        }

        // 3) Restaurar nível de log
        esp_log_level_set("*", s_prev_level);
    }

    xSemaphoreGive(s_mutex);
}

bool mb_session_is_active(void)
{
    return (s_refcnt > 0);
}

#endif

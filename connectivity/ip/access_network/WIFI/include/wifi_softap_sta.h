// wifi_softap_sta.h
// Interface pública para controle robusto AP+STA no ESP-IDF.
// - start_wifi_ap_sta(): inicializa netifs, Wi-Fi e sobe em modo AP+STA
// - stop_wifi_ap_sta():  desliga tudo (use só para “parar geral”)
// - wifi_ap_force_enable():        religa o AP imediatamente (AP+STA)
// - wifi_ap_force_disable():       desliga o AP mantendo STA
// - wifi_ap_is_running():          consulta se o SoftAP está ativo
//
// Integração com Factory Control / Low Power:
// * Se STA NÃO estiver ativo (has_activate_sta() == false), seu FC pode ir
//   para deep sleep normalmente. Este driver não chama deep sleep.
// * Se STA estiver ativo e você precisar “devolver a internet” ao usuário
//   O AP volta sozinho após o período ou via wifi_ap_force_enable().
//
// Observação: este header NÃO expõe helpers internos como get_ssid_*() ou
// has_activate_sta(); eles pertencem ao seu projeto.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

//bool     wifi_ap_single_shot_done(void);
// Utilitário: AP está ativo? (modo contém AP)
bool     wifi_ap_is_running(void);
/**
 * @brief Inicia Wi-Fi em modo AP+STA.
 *
 * Cria as interfaces padrão (AP e STA), registra handlers de evento,
 * configura o SoftAP com SSID/senha obtidos do seu storage (get_ssid_ap(),
 * get_password_ap()) e inicia o Wi-Fi. O STA tentará conectar caso
 * has_activate_sta() retorne true e as credenciais existam.
 *
 * Importante:
 *  - Alimentação por fonte: o driver configura WIFI_PS_NONE (sem power-save).
 *  - Se quiser economia em bateria, mude para WIFI_PS_MIN_MODEM no seu fluxo.
 *
 * @return ESP_OK em sucesso; erro do IDF caso falhe.
 */
esp_err_t start_wifi_ap_sta(void);

/**
 * @brief Para completamente o Wi-Fi (modo NULL + deinit das pilhas).
 *
 * Use APENAS quando quiser desligar tudo (ex.: desligar dispositivo,
 * trocar de modo radical, etc.). Para o caso de “Exit/timeout do front”,
 * NÃO use esta função — prefira wifi_ap_suspend_temporarily().
 */
void stop_wifi_ap_sta(void);

/**
 * @brief Suspende o SoftAP temporariamente, mantendo o STA ativo.
 *
 * Troca o modo para WIFI_MODE_STA (sem deinit), agenda um timer one-shot
 * para religar o AP após `seconds`. Durante a suspensão, eventos que
 * pararem o AP não irão religá-lo até o fim da janela.
 *
 * Exemplo de uso no “Exit” do front: wifi_ap_suspend_temporarily(90);
 *
 * @param seconds  Quantos segundos o SoftAP ficará suspenso (se 0, usa default).
 */
void wifi_ap_suspend_temporarily(uint32_t seconds);

/**
 * @brief Religa o SoftAP imediatamente (volta para AP+STA).
 *
 * Útil para um botão “Reativar Portal Agora” no front ou para cenários
 * em que você deseja encerrar a janela de suspensão antes do tempo.
 * Idempotente: se já estiver em AP+STA, apenas garante o START.
 */
void wifi_ap_force_enable(void);

/**
 * @brief Desliga o SoftAP mantendo o STA.
 *
 * Coloca o Wi-Fi em WIFI_MODE_STA, sem timers. Útil se você quiser manter
 * o portal desligado por tempo indeterminado enquanto o STA opera.
 */
void wifi_ap_force_disable(void);

/**
 * @brief Indica se o SoftAP está ativo (modo AP ou AP+STA).
 *
 * @return true se o AP está rodando; false caso contrário.
 */
bool wifi_ap_is_running(void);

void wifi_sta_mark_intentional_disconnect(bool enable);

uint32_t wifi_ap_get_sta_count(void);

bool     wifi_ap_is_suspended(void);        
uint32_t wifi_ap_seconds_to_resume(void);  

void wifi_diag_install(void);

void     wifi_ap_single_shot_suspend(uint32_t seconds);

#ifdef __cplusplus
}
#endif

/*
 * u_cell_power_strategy.c
 *
 *  Created on: 9 de nov. de 2025
 *      Author: geopo
 */

// u_cell_power_strategy.c
// helpers de energia para SARA-R4/R422 usando ubxlib
// copia e cola na pasta 4G junto com o .h

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"

#include "u_port.h"
#include "u_cell_power_strategy.h"
#include "u_at_client.h"
#include "u_error_common.h"
#include "u_cell_module_type.h"
#include "u_cell.h"
#include "u_cell_net.h"
#include "u_cell_pwr.h"
#include "u_port_os.h"
#include "u_cfg_app_platform_specific.h"
#include "u_cell_cfg.h"

#ifdef U_CFG_OVERRIDE
# include "u_cfg_override.h"
#endif

#define TAG "LTE_PWR"

// usamos o mesmo modo de UART power saving do SARA p/ DTR
#define LTE_PWR_USE_DTR_UPSV_MODE  3

// GPIO lógico do SARA usado para RI.
// Para SARA-R4/R422 o pin 7 é o RING output dedicado.
// Aqui usamos o índice lógico 7, que é o que vai no +UGPIOC=idx,func.
#define LTE_PWR_RI_GPIO_INDEX      7

// ---------------------------------------------------------------------
// Helpers AT simples
// ---------------------------------------------------------------------
static int at_send_ok(uAtClientHandle_t at, const char *cmd)
{
    uAtClientLock(at);
    uAtClientCommandStart(at, cmd);
    uAtClientCommandStopReadResponse(at);
    return uAtClientUnlock(at);
}

static int at_query_int(uAtClientHandle_t at, const char *cmd,
                        const char *prefix, int32_t *out)
{
    if (out) {
        *out = -1;
    }
    uAtClientLock(at);
    uAtClientCommandStart(at, cmd);
    uAtClientCommandStop(at);

    int err = 0;
    int32_t val = -1;

    if (prefix) {
        if (uAtClientResponseStart(at, prefix) == 0) {
            val = uAtClientReadInt(at);
            uAtClientResponseStop(at);
        } else {
            err = (int)U_ERROR_COMMON_PLATFORM;
        }
    } else {
        if (uAtClientResponseStart(at, NULL) == 0) {
            val = uAtClientReadInt(at);
            uAtClientResponseStop(at);
        } else {
            err = (int)U_ERROR_COMMON_PLATFORM;
        }
    }

    int unlock = uAtClientUnlock(at);
    if (err == 0) {
        err = unlock;
    }
    if (err == 0 && out) {
        *out = val;
    }
    return err;
}

// ---------------------------------------------------------------------
// Helper genérico para +UGPIOC (configuração de GPIO lógico do modem)
// ---------------------------------------------------------------------
static int32_t lte_power_set_ugpioc(uDeviceHandle_t devHandle,
                                    int gpioIndex,
                                    int function,
                                    bool saveToNvm)
{
    if (gpioIndex < 0 || function < 0) {
        return (int32_t)U_ERROR_COMMON_INVALID_PARAMETER;
    }

    uAtClientHandle_t at = NULL;
    if (uCellAtClientHandleGet(devHandle, &at) != 0 || (at == NULL)) {
        return (int32_t)U_ERROR_COMMON_PLATFORM;
    }

    char cmd[32];
    int32_t err;

    // Monta: AT+UGPIOC=<gpioIndex>,<function>
    snprintf(cmd, sizeof(cmd), "AT+UGPIOC=%d,%d", gpioIndex, function);
    err = at_send_ok(at, cmd);
    if (err != 0) {
        ESP_LOGW(TAG, "%s falhou (%ld)", cmd, (long)err);
        return err;
    }

    if (saveToNvm) {
        int32_t e2 = at_send_ok(at, "AT&W");
        if (e2 != 0) {
            // Não é crítico, só logamos
            ESP_LOGW(TAG, "AT&W falhou apos %s (%ld)", cmd, (long)e2);
        }
    }

    ESP_LOGI(TAG, "UGPIOC configurado: idx=%d func=%d (persist=%d)",
             gpioIndex, function, saveToNvm ? 1 : 0);

    return 0;
}

// ---------------------------------------------------------------------
// UPSV / DTR
// ---------------------------------------------------------------------
static int32_t lte_power_set_uart_psm(uDeviceHandle_t devHandle,
                                      bool enable,
                                      bool set_atd1,
                                      int upsv_mode,
                                      int32_t *upsv_state_out)
{
    if (upsv_state_out) {
        *upsv_state_out = -1;
    }
    uAtClientHandle_t at = NULL;
    if (uCellAtClientHandleGet(devHandle, &at) != 0 || (at == NULL)) {
        return (int32_t)U_ERROR_COMMON_PLATFORM;
    }

    int err = 0;

    if (enable) {
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "AT+UPSV=%d", (upsv_mode > 0) ? upsv_mode : 4);
        err = at_send_ok(at, cmd);
        if (err) {
            ESP_LOGW(TAG, "%s falhou (%d)", cmd, err);
        }
        if (set_atd1) {
            int e2 = at_send_ok(at, "AT&D=1");
            if (e2) {
                ESP_LOGW(TAG, "AT&D=1 falhou (%d)", e2);
            }
            (void) at_send_ok(at, "AT&W");
        }
    } else {
        err = at_send_ok(at, "AT+UPSV=0");
        if (err) {
            ESP_LOGW(TAG, "AT+UPSV=0 falhou (%d)", err);
        }
    }

    // ler de volta
    int32_t upsv = -1;
    int qerr = at_query_int(at, "AT+UPSV?", "+UPSV:", &upsv);
    if (qerr == 0) {
        if (upsv_state_out) {
            *upsv_state_out = upsv;
        }
        ESP_LOGI(TAG, "UPSV? => %ld", (long)upsv);
    } else {
        ESP_LOGW(TAG, "Falha lendo +UPSV? (%d)", qerr);
    }

    return err ? err : qerr;
}

int32_t lte_uart_psm_enable(uDeviceHandle_t devHandle, bool set_atd1, int32_t *upsv_out)
{
    int32_t err = 0;

#if defined(U_CFG_APP_PIN_CELL_DTR) && (U_CFG_APP_PIN_CELL_DTR >= 0)
    // agora que você já marcou o SARA-R422 com o bit de DTR na tabela,
    // podemos registrar o pino direto no ubxlib
    err = uCellPwrSetDtrPowerSavingPin(devHandle, U_CFG_APP_PIN_CELL_DTR);
    if (err != 0) {
        ESP_LOGW(TAG, "uCellPwrSetDtrPowerSavingPin(%d) falhou (%ld) – seguindo para UPSV.",
                 (int)U_CFG_APP_PIN_CELL_DTR, (long)err);
        // seguimos mesmo assim
    }
#endif

    err = lte_power_set_uart_psm(devHandle,
                                 true,
                                 set_atd1,
                                 LTE_PWR_USE_DTR_UPSV_MODE,
                                 upsv_out);
    return err;
}

int32_t lte_uart_psm_disable(uDeviceHandle_t devHandle)
{
    return lte_power_set_uart_psm(devHandle, false, false, 0, NULL);
}

// ---------------------------------------------------------------------
// 3GPP PSM (oficial, do exemplo da u-blox)
// ---------------------------------------------------------------------
int32_t lte_power_apply_3gpp(uDeviceHandle_t devHandle,
                             int32_t activeTimeSeconds,
                             int32_t periodicWakeupSeconds)
{
    // descobrir RAT
    uCellNetRat_t rat = uCellCfgGetRat(devHandle, 0);
    if (rat != U_CELL_NET_RAT_CATM1 && rat != U_CELL_NET_RAT_NB1) {
        // PSM 3GPP só faz sentido nessas
        return (int32_t)U_ERROR_COMMON_NOT_SUPPORTED;
    }

    if (activeTimeSeconds <= 0) {
        activeTimeSeconds = LTE_PWR_ACTIVE_TIME_DEFAULT_SEC;
    }
    if (periodicWakeupSeconds <= 0) {
        periodicWakeupSeconds = LTE_PWR_TAU_DEFAULT_SEC;
    }

    ESP_LOGI(TAG, "Pedindo 3GPP PSM: AT=%ld s, TAU=%ld s, RAT=%d",
             (long)activeTimeSeconds, (long)periodicWakeupSeconds, (int)rat);

    int32_t err = uCellPwrSetRequested3gppPowerSaving(devHandle,
                                                      rat,
                                                      true,
                                                      activeTimeSeconds,
                                                      periodicWakeupSeconds);
    if (err != 0) {
        ESP_LOGW(TAG, "uCellPwrSetRequested3gppPowerSaving() falhou (%ld)", (long)err);
        return err;
    }

    // se o módulo pedir reboot para aplicar
    if (uCellPwrRebootIsRequired(devHandle)) {
        ESP_LOGI(TAG, "Módulo pediu reboot para aplicar PSM 3GPP, reiniciando...");
        uCellPwrReboot(devHandle, NULL);
    }

    // aqui não dá pra garantir que a REDE aceitou (isso é com a operadora)
    return 0;
}

// ---------------------------------------------------------------------
// RING / RI para acordar o ESP via SMS/chamada
// ---------------------------------------------------------------------

/**
 * @brief   Configura o comportamento da linha RING (RI) via AT+URINGCFG.
 *
 * @param devHandle  Handle do device ubxlib.
 * @param mode       Modo do URINGCFG:
 *                     0 = RING desativado
 *                     1 = RING para SMS recebidos
 *                     2 = RING para dados (PPP, sockets em Direct Link, FTP)
 *                     3 = RING para SMS + dados
 *
 * @return  0 em sucesso,
 *          <0 em erro ubxlib (U_ERROR_COMMON_xxx) ou erro de AT.
 */
int32_t lte_power_enable_ring_sms(uDeviceHandle_t devHandle, int32_t mode)
{
    uAtClientHandle_t at = NULL;

    if (devHandle == NULL) {
        return (int32_t) U_ERROR_COMMON_INVALID_PARAMETER;
    }

    // Garante modo válido
    if (mode < 0 || mode > 3) {
        return (int32_t) U_ERROR_COMMON_INVALID_PARAMETER;
    }

    if ((uCellAtClientHandleGet(devHandle, &at) != 0) || (at == NULL)) {
        return (int32_t) U_ERROR_COMMON_PLATFORM;
    }

    // Configura URINGCFG
    uAtClientLock(at);
    uAtClientCommandStart(at, "AT+URINGCFG=");
    uAtClientWriteInt(at, mode);
    uAtClientCommandStopReadResponse(at);
    int32_t err = uAtClientErrorGet(at);
    uAtClientUnlock(at);

    if (err != 0) {
        ESP_LOGW(TAG, "AT+URINGCFG=%ld falhou (%ld)", (long) mode, (long) err);
        return err;
    }

    // Leitura de volta só para log/debug (não é obrigatório)
    int32_t readMode = -1;
    int32_t qerr = at_query_int(at, "AT+URINGCFG?", "+URINGCFG:", &readMode);
    if (qerr == 0) {
        ESP_LOGI(TAG, "URINGCFG? => modo=%ld", (long) readMode);
    } else {
        ESP_LOGW(TAG, "Falha lendo URINGCFG? (%ld)", (long) qerr);
    }

    // Se a escrita deu certo mas a leitura deu erro, devolvemos o erro da leitura.
    return qerr;
}

// ---------------------------------------------------------------------
// Wrapper público (estratégias combinadas)
// ---------------------------------------------------------------------
int32_t lte_power_apply(uDeviceHandle_t devHandle,
                        lte_pwr_strategy_t strategy,
                        int32_t activeTimeSeconds,
                        int32_t *pAppliedTauSeconds)
{
    if (pAppliedTauSeconds) {
        *pAppliedTauSeconds = -1;
    }

    switch (strategy) {
        case LTE_PWR_STRATEGY_3GPP_FIRST:
        case LTE_PWR_STRATEGY_PSM_ONLY:
            // devolve o erro pro chamador decidir o fallback
            return lte_power_apply_3gpp(devHandle,
                                        activeTimeSeconds,
                                        LTE_PWR_TAU_DEFAULT_SEC);

        case LTE_PWR_STRATEGY_UART_ONLY:
        default:
            return (int32_t)U_ERROR_COMMON_NOT_SUPPORTED;
    }
}

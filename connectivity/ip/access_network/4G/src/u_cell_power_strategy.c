#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
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

#define LTE_PWR_TAU_SECONDS_DEFAULT    (3 * 60 * 60)

static const char *TAG = "LTE_PWR";

static void encode_uint_to_binary_string(uint32_t num,
                                         char *buffer,
                                         size_t buffer_size,
                                         int32_t bit_count)
{
    int32_t pos = 0;

    for (int32_t bit = 31; bit >= 0; bit--) {
        if (bit < bit_count) {
            if (pos < (int32_t) buffer_size) {
                buffer[pos] = (num >> bit & 0x1) ? '1' : '0';
            }
            pos++;
        }
    }
}

static int32_t encode_periodic_tau(int32_t tau_seconds,
                                   char *encoded,
                                   size_t encoded_size,
                                   int32_t *applied_seconds_out)
{
    if ((encoded == NULL) || (encoded_size < 9)) {
        return (int32_t) U_ERROR_COMMON_INVALID_PARAMETER;
    }

    if (tau_seconds < 0) {
        tau_seconds = 0;
    }

    uint32_t value = 0;
    int32_t applied_seconds = 0;
    int32_t unit_seconds = 0;

    if (tau_seconds <= 2 * 0x1f) {
        unit_seconds = 2;
        value = (uint32_t) (tau_seconds / unit_seconds);
        if ((tau_seconds > 0) && (value == 0)) {
            value = 1;
        }
        strncpy(encoded, "01100000", encoded_size);
        applied_seconds = (int32_t) value * unit_seconds;
    } else if (tau_seconds <= 30 * 0x1f) {
        unit_seconds = 30;
        value = (uint32_t) (tau_seconds / unit_seconds);
        if ((tau_seconds > 0) && (value == 0)) {
            value = 1;
        }
        strncpy(encoded, "10000000", encoded_size);
        applied_seconds = (int32_t) value * unit_seconds;
    } else if (tau_seconds <= 60 * 0x1f) {
        unit_seconds = 60;
        value = (uint32_t) (tau_seconds / unit_seconds);
        if ((tau_seconds > 0) && (value == 0)) {
            value = 1;
        }
        strncpy(encoded, "10100000", encoded_size);
        applied_seconds = (int32_t) value * unit_seconds;
    } else if (tau_seconds <= 10 * 60 * 0x1f) {
        unit_seconds = 10 * 60;
        value = (uint32_t) (tau_seconds / unit_seconds);
        if ((tau_seconds > 0) && (value == 0)) {
            value = 1;
        }
        strncpy(encoded, "00000000", encoded_size);
        applied_seconds = (int32_t) value * unit_seconds;
    } else if (tau_seconds <= 60 * 60 * 0x1f) {
        unit_seconds = 60 * 60;
        value = (uint32_t) (tau_seconds / unit_seconds);
        if ((tau_seconds > 0) && (value == 0)) {
            value = 1;
        }
        strncpy(encoded, "00100000", encoded_size);
        applied_seconds = (int32_t) value * unit_seconds;
    } else if (tau_seconds <= 10 * 60 * 60 * 0x1f) {
        unit_seconds = 10 * 60 * 60;
        value = (uint32_t) (tau_seconds / unit_seconds);
        if ((tau_seconds > 0) && (value == 0)) {
            value = 1;
        }
        strncpy(encoded, "01000000", encoded_size);
        applied_seconds = (int32_t) value * unit_seconds;
    } else {
        int32_t t = tau_seconds / (320 * 60 * 60);
        if (t <= 0) {
            t = 1;
        }
        if (t > 0x1f) {
            t = 0x1f;
        }
        value = (uint32_t) t;
        strncpy(encoded, "11000000", encoded_size);
        applied_seconds = t * 320 * 60 * 60;
    }

    encode_uint_to_binary_string(value, &encoded[3], encoded_size - 3, 5);
    encoded[8] = '\0';

    if (applied_seconds_out != NULL) {
        *applied_seconds_out = applied_seconds;
    }

    return (int32_t) U_ERROR_COMMON_SUCCESS;
}

static int32_t encode_active_time_zero(char *encoded, size_t encoded_size)
{
    if ((encoded == NULL) || (encoded_size < 9)) {
        return (int32_t) U_ERROR_COMMON_INVALID_PARAMETER;
    }

    strncpy(encoded, "00000000", encoded_size);
    encoded[8] = '\0';
    return (int32_t) U_ERROR_COMMON_SUCCESS;
}

static int32_t send_disable_edrx(uAtClientHandle_t atHandle)
{
    uAtClientLock(atHandle);
    uAtClientCommandStart(atHandle, "AT+CEDRXS=");
    uAtClientWriteInt(atHandle, 0);
    uAtClientCommandStopReadResponse(atHandle);
    return uAtClientUnlock(atHandle);
}

static int32_t apply_psm_min(uDeviceHandle_t devHandle,
                             int32_t tau_seconds,
                             int32_t *tau_applied_out)
{
    int32_t tau_request = (tau_seconds > 0) ? tau_seconds : LTE_PWR_TAU_SECONDS_DEFAULT;
    int32_t tau_encoded_seconds = 0;
    char tau_encoded[9] = {0};
    char active_encoded[9] = {0};
    uAtClientHandle_t atHandle = NULL;
ESP_LOGI(TAG, "========== [LTE_PWR] PSM_MIN: BEGIN ==========");
    ESP_LOGI(TAG, "[LTE_PWR] strategy=PSM_MIN req: TAU=%ld s, AT=0", (long) tau_request);

    if (encode_periodic_tau(tau_request, tau_encoded, sizeof(tau_encoded), &tau_encoded_seconds) != 0) {
        ESP_LOGE(TAG, "[LTE_PWR] falha ao codificar TAU");
        return (int32_t) U_ERROR_COMMON_PLATFORM;
    }

    if (encode_active_time_zero(active_encoded, sizeof(active_encoded)) != 0) {
        ESP_LOGE(TAG, "[LTE_PWR] falha ao codificar tempo ativo");
        return (int32_t) U_ERROR_COMMON_PLATFORM;
    }

    if (uCellAtClientHandleGet(devHandle, &atHandle) != 0 || (atHandle == NULL)) {
        ESP_LOGE(TAG, "[LTE_PWR] não foi possível obter handle AT");
        return (int32_t) U_ERROR_COMMON_PLATFORM;
    }
    
    // --- PROVA 1: habilitar URC de PSM (+UUPSMR) antes do CPSMS ---
    // Assim que o modem entrar em PSM, veremos +UUPSMR: 1,1 no console.
    // É idempotente; pode chamar mais de uma vez sem problema.
    uAtClientLock(atHandle);
    uAtClientCommandStart(atHandle, "AT+UPSMR=");
    uAtClientWriteInt(atHandle, 1);   // enable report
    uAtClientCommandStopReadResponse(atHandle);
    (void) uAtClientUnlock(atHandle);
    
ESP_LOGI(TAG, "[LTE_PWR] step 1/5: detectar RAT ativo");
    int32_t ratOrError = uCellNetGetActiveRat(devHandle);
    if (ratOrError >= 0) {
		ESP_LOGI(TAG, "[LTE_PWR] step 2/5: desativar eDRX via API (RAT=%ld)", (long)ratOrError);
        int32_t err = uCellPwrSetRequestedEDrx(devHandle, (uCellNetRat_t) ratOrError, false, 0, 0);
 
        if (err != 0) {
            ESP_LOGW(TAG, "[LTE_PWR] falha ao desativar eDRX via API (%ld)", (long) err);
            (void) send_disable_edrx(atHandle);
            }  else {
            ESP_LOGI(TAG, "[LTE_PWR] eDRX API: OK (ou já desabilitado)");
                    }
      } else {
        ESP_LOGW(TAG, "[LTE_PWR] não foi possível obter RAT ativo (%ld)", (long) ratOrError);
        (void) send_disable_edrx(atHandle);
    }

// Só prossegue se estiver registrado na rede (Attach/TAU feito)
    if (!uCellNetIsRegistered(devHandle)) {
        ESP_LOGW(TAG, "[LTE_PWR] rede NÃO registrada (CEREG != 1/5) — não aplicando PSM agora");
        return (int32_t) U_ERROR_COMMON_TEMPORARY_FAILURE; // deixa o chamador decidir a política
    }
ESP_LOGI(TAG, "[LTE_PWR] step 3/5: aplicar CPSMS (AT=0) com TAU codificado");
    uAtClientLock(atHandle);
    int32_t previousTimeout = uAtClientTimeoutGet(atHandle);
    uAtClientTimeoutSet(atHandle, 3000);
    uAtClientCommandStart(atHandle, "AT+CPSMS=");
    uAtClientWriteInt(atHandle, 1);
    uAtClientWriteString(atHandle, "", false);
    uAtClientWriteString(atHandle, "", false);
    uAtClientWriteString(atHandle, tau_encoded, true);
    uAtClientWriteString(atHandle, active_encoded, true);
    uAtClientCommandStopReadResponse(atHandle);
    int32_t atError = uAtClientUnlock(atHandle);
    uAtClientTimeoutSet(atHandle, previousTimeout > 0 ? previousTimeout : U_AT_CLIENT_DEFAULT_TIMEOUT_MS);
    if (atError != 0) {
        ESP_LOGE(TAG, "[LTE_PWR] falha ao aplicar PSM (err=%ld)", (long) atError);
        return atError;
    }
    
     ESP_LOGI(TAG, "[LTE_PWR] step 4/5: quiet-time p/ modem poder entrar em PSM");
    /* aguarde alguns segundos SEM enviar ATs para permitir RRC->Idle->PSM */
    uPortTaskBlock(10000); /* 10 s; ajuste se necessário */

    bool psm_on = false;
    int32_t active_time_network = -1;
    int32_t tau_network = -1;
    int32_t status = uCellPwrGet3gppPowerSaving(devHandle,
                                                &psm_on,
                                                &active_time_network,
                                                &tau_network);
    if ((status == 0) && psm_on && (tau_network >= 0)) {
        ESP_LOGI(TAG, "[LTE_PWR] PSM aceito pela rede: TAU=%ld s, AT=%ld", (long) tau_network, (long) active_time_network);
        if (tau_applied_out != NULL) {
            *tau_applied_out = tau_network;
        }
    } else {
        ESP_LOGW(TAG, "[LTE_PWR] não foi possível confirmar PSM (status=%ld, on=%d)", (long) status, psm_on);
        if (tau_applied_out != NULL) {
            *tau_applied_out = tau_encoded_seconds;
        }
    }
    
  ESP_LOGI(TAG, "[LTE_PWR] diag: omitindo +CSCON? para não atrapalhar entrada em PSM");
  
ESP_LOGI(TAG, "[LTE_PWR] step 5/5: PSM_MIN aplicado; manter modem alimentado (sem CPWROFF)");
    ESP_LOGI(TAG, "========== [LTE_PWR] PSM_MIN: END ==========");
    return (int32_t) U_ERROR_COMMON_SUCCESS;
}

void lte_power_diag_enable_psm_urc(uDeviceHandle_t devHandle)
{
    if (!devHandle) return;
    uAtClientHandle_t atHandle = NULL;
    if (uCellAtClientHandleGet(devHandle, &atHandle) != 0 || !atHandle) return;
    uAtClientLock(atHandle);
    uAtClientCommandStart(atHandle, "AT+UPSMR=");
    uAtClientWriteInt(atHandle, 1);
    uAtClientCommandStopReadResponse(atHandle);
    (void) uAtClientUnlock(atHandle);
    ESP_LOGI(TAG, "[LTE_PWR] diag: +UPSMR=1 habilitado (URC de PSM)");
}

static int32_t at_send_simple_cmd(uAtClientHandle_t at, const char *cmd)
{
    if (!at || !cmd) return (int32_t) U_ERROR_COMMON_INVALID_PARAMETER;
    uAtClientLock(at);
    uAtClientCommandStart(at, cmd);
    uAtClientCommandStopReadResponse(at);
    return uAtClientUnlock(at);
}

int32_t lte_power_get_uart_psm(uDeviceHandle_t devHandle, int32_t *upsv_out)
{
    if (!devHandle || !upsv_out) return (int32_t) U_ERROR_COMMON_INVALID_PARAMETER;

    uAtClientHandle_t at = NULL;
    if (uCellAtClientHandleGet(devHandle, &at) != 0 || !at) {
        ESP_LOGE(TAG, "[LTE_PWR] não foi possível obter handle AT");
        return (int32_t) U_ERROR_COMMON_PLATFORM;
    }

    int32_t upsv = -1;
    uAtClientLock(at);
    uAtClientCommandStart(at, "AT+UPSV?");
    uAtClientCommandStop(at);
    if (uAtClientResponseStart(at, "+UPSV:") == 0) {
        upsv = uAtClientReadInt(at);
    }
    uAtClientResponseStop(at);
    int32_t err = uAtClientUnlock(at);

    if (err != 0 || upsv < 0) {
        ESP_LOGW(TAG, "[LTE_PWR] falha ao ler +UPSV? (err=%ld, upsv=%ld)", (long) err, (long) upsv);
        return (err != 0) ? err : (int32_t) U_ERROR_COMMON_PLATFORM;
    }

    *upsv_out = upsv;
    ESP_LOGI(TAG, "[LTE_PWR] +UPSV? => %ld", (long) upsv);
    return (int32_t) U_ERROR_COMMON_SUCCESS;
}

int32_t lte_power_set_uart_psm(uDeviceHandle_t devHandle,
                               bool enable,
                               bool set_atd1,
                               int32_t *upsv_after)
{
    if (!devHandle) return (int32_t) U_ERROR_COMMON_INVALID_PARAMETER;

    uAtClientHandle_t at = NULL;
    if (uCellAtClientHandleGet(devHandle, &at) != 0 || !at) {
        ESP_LOGE(TAG, "[LTE_PWR] não foi possível obter handle AT");
        return (int32_t) U_ERROR_COMMON_PLATFORM;
    }

    /* Opcional: definir AT&D=1 para comportamento padrão do DTR no modem. */
    if (set_atd1) {
        int32_t e = at_send_simple_cmd(at, "AT&D=1");
        if (e != 0) {
            ESP_LOGW(TAG, "[LTE_PWR] AT&D=1 falhou (err=%ld)", (long) e);
            /* não aborta: UPSV ainda pode funcionar */
        } else {
            ESP_LOGI(TAG, "[LTE_PWR] AT&D=1 aplicado");
        }
    }

    /* UPSV = 4 (economia máximo por UART) quando enable=true; senão desativa (0). */
    char cmd_buf[16];
    snprintf(cmd_buf, sizeof(cmd_buf), "AT+UPSV=%d", enable ? 4 : 0);
    int32_t err = at_send_simple_cmd(at, cmd_buf);
    if (err != 0) {
        ESP_LOGE(TAG, "[LTE_PWR] %s falhou (err=%ld)", cmd_buf, (long) err);
        return err;
    }
    ESP_LOGI(TAG, "[LTE_PWR] %s OK", cmd_buf);

    /* Confirma com +UPSV? */
    int32_t upsv = -1;
    err = lte_power_get_uart_psm(devHandle, &upsv);
    if (err == 0 && upsv_after) {
        *upsv_after = upsv;
    }
    return err;
}

/* ===== Compat: mantemos a assinatura antiga, mas apenas encaminhamos para PSM_MIN ===== */
int32_t lte_power_apply(uDeviceHandle_t devHandle,
                        int32_t legacy_strategy,
                        int32_t tau_seconds,
                        int32_t *tau_applied_out)
{
    if (!devHandle) return (int32_t)U_ERROR_COMMON_INVALID_PARAMETER;
    return apply_psm_min(devHandle, tau_seconds, tau_applied_out);
}

/* ===== Nova API por política: decide desligar vs low-power independentemente do sucesso ===== */
int32_t lte_apply_policy_after_tx(uDeviceHandle_t devHandle,
                                  bool delivery_ok,
                                  const lte_pwr_policy_t *policy,
                                  bool *skip_cleanup_out)
{
    if (!devHandle || !policy || !skip_cleanup_out) {
        return (int32_t) U_ERROR_COMMON_INVALID_PARAMETER;
    }
    *skip_cleanup_out = false;

    /* Falhou e a política manda forçar hard-off? */
    if (!delivery_ok && policy->force_off_on_fail) {
        *skip_cleanup_out = false;          /* segue para cleanup no chamador */
        return (int32_t) U_ERROR_COMMON_SUCCESS;
    }

    switch (policy->mode) {
        case LTE_PWR_OFF_ALWAYS:
            *skip_cleanup_out = false;      /* hard-off sempre */
            return (int32_t) U_ERROR_COMMON_SUCCESS;

        case LTE_PWR_KEEP_ON:
            *skip_cleanup_out = true;       /* mantém ligado, sem low-power explícito */
            return (int32_t) U_ERROR_COMMON_SUCCESS;

        case LTE_PWR_PSM_MIN: {
            int32_t tau = policy->tau_seconds > 0 ? policy->tau_seconds : LTE_PWR_TAU_SECONDS_DEFAULT;
            int32_t _applied = 0;
            int32_t err = apply_psm_min(devHandle, tau, &_applied);
            if (err == 0) *skip_cleanup_out = true;   /* não fazer hard-off */
            return err;
        }

        case LTE_PWR_PSM_CFG:
            /* TODO: implementar T3324 configurável; por enquanto trate como MIN */
            *skip_cleanup_out = true;
            return apply_psm_min(devHandle, policy->tau_seconds, NULL);

        case LTE_PWR_EDRX_CFG:
            /* TODO: aplicar eDRX; por ora, apenas manter ligado */
            *skip_cleanup_out = true;
            return (int32_t) U_ERROR_COMMON_SUCCESS;
    }
    return (int32_t) U_ERROR_COMMON_INVALID_PARAMETER;
}


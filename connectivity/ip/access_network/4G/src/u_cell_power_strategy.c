#include "u_cell_power_strategy.h"

#include "esp_log.h"
#include "u_error_common.h"
#include "u_cell.h"
#include "u_cell_net.h"
#include "u_cell_pwr.h"
#include "u_at_client.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

    int32_t ratOrError = uCellNetGetActiveRat(devHandle);
    if (ratOrError >= 0) {
        int32_t err = uCellPwrSetRequestedEDrx(devHandle, (uCellNetRat_t) ratOrError, false, 0, 0);
        if (err != 0) {
            ESP_LOGW(TAG, "[LTE_PWR] falha ao desativar eDRX via API (%ld)", (long) err);
            (void) send_disable_edrx(atHandle);
        }
    } else {
        ESP_LOGW(TAG, "[LTE_PWR] não foi possível obter RAT ativo (%ld)", (long) ratOrError);
        (void) send_disable_edrx(atHandle);
    }

    uAtClientLock(atHandle);
    int32_t previousTimeout = uAtClientTimeoutGet(atHandle);
    uAtClientTimeoutSet(atHandle, 10000);
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

    return (int32_t) U_ERROR_COMMON_SUCCESS;
}

int32_t lte_power_apply(uDeviceHandle_t devHandle,
                        lte_power_strategy_t strategy,
                        int32_t tau_seconds,
                        int32_t *tau_applied_out)
{
    if (devHandle == NULL) {
        return (int32_t) U_ERROR_COMMON_INVALID_PARAMETER;
    }

    switch (strategy) {
        case LTE_PWR_OFF:
            return (int32_t) U_ERROR_COMMON_SUCCESS;
        case LTE_PWR_PSM_MIN:
            return apply_psm_min(devHandle, tau_seconds, tau_applied_out);
        default:
            return (int32_t) U_ERROR_COMMON_INVALID_PARAMETER;
    }
}

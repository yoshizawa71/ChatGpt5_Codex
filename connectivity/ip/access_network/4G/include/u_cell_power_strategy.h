#ifndef U_CELL_POWER_STRATEGY_H
#define U_CELL_POWER_STRATEGY_H

#include "u_device_handle.h"
#include <stdint.h>
#include <stdbool.h>

/** Estratégias de energia que o 4G_system vai pedir */
typedef enum {
    LTE_PWR_STRATEGY_3GPP_FIRST = 0,  /**< tenta PSM 3GPP primeiro; se falhar o chamador faz fallback p/ DTR */
    LTE_PWR_STRATEGY_PSM_ONLY,        /**< só tenta PSM 3GPP */
    LTE_PWR_STRATEGY_UART_ONLY        /**< não tenta PSM; o chamador deve chamar lte_uart_psm_enable() */
} lte_pwr_strategy_t;

/** tempo “acordado” padrão que pedimos no PSM 3GPP */
#define LTE_PWR_ACTIVE_TIME_DEFAULT_SEC   60
/** TAU padrão que pedimos no PSM 3GPP (4h) */
#define LTE_PWR_TAU_DEFAULT_SEC           (4 * 60 * 60)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Habilita o modo de economia pela UART (UPSV=3) e registra o DTR.
 * Deve ser chamado DEPOIS do uDeviceOpen().
 *
 * @param devHandle handle do dispositivo celular.
 * @param set_atd1  se true, envia AT&D=1 e AT&W.
 * @param upsv_out  se não for NULL, devolve o valor lido de AT+UPSV?.
 */
int32_t lte_uart_psm_enable(uDeviceHandle_t devHandle, bool set_atd1, int32_t *upsv_out);

/**
 * Desabilita UPSV (AT+UPSV=0).
 */
int32_t lte_uart_psm_disable(uDeviceHandle_t devHandle);

/**
 * Tenta aplicar PSM 3GPP usando a função oficial do ubxlib.
 * NÃO faz fallback aqui dentro — quem decide é o chamador.
 *
 * @return 0 se conseguiu pedir ao módulo; !=0 se o módulo/RAT não suporta.
 */
int32_t lte_power_apply_3gpp(uDeviceHandle_t devHandle,
                             int32_t activeTimeSeconds,
                             int32_t periodicWakeupSeconds);

/**
 * Wrapper genérico que o seu 4G chama.
 * Hoje: se for LTE_PWR_STRATEGY_3GPP_FIRST tenta 3GPP e retorna o erro.
 * O 4G_system_control vê o erro e chama o DTR.
 */
int32_t lte_power_apply(uDeviceHandle_t devHandle,
                        lte_pwr_strategy_t strategy,
                        int32_t activeTimeSeconds,
                        int32_t *pAppliedTauSeconds);
                        
int32_t lte_power_enable_ring_sms(uDeviceHandle_t devHandle, int32_t mode);


#ifdef __cplusplus
}
#endif

#endif /* U_CELL_POWER_STRATEGY_H */

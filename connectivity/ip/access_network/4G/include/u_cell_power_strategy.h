#ifndef U_CELL_POWER_STRATEGY_H_
#define U_CELL_POWER_STRATEGY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "u_device_handle.h"
#include <stdint.h>
#include <stdbool.h>

/*typedef enum {
    LTE_PWR_OFF = 0,
    LTE_PWR_PSM_MIN = 1
} lte_power_strategy_t;*/
/* ===== modos “normal” x “low-power” ===== */
typedef enum {
    LTE_PWR_OFF_ALWAYS = 0,   /* desligar duro SEMPRE ao fim do ciclo */
    LTE_PWR_KEEP_ON,          /* manter ligado (sem low-power explícito) */
    LTE_PWR_PSM_MIN,          /* PSM, T3324=0 (não paginável) */
    LTE_PWR_PSM_CFG,          /* PSM com TAU/T3324 configuráveis (futuro) */
    LTE_PWR_EDRX_CFG          /* eDRX configurável (futuro) */
} lte_pwr_mode_t;

typedef struct {
    lte_pwr_mode_t mode;
    bool           force_off_on_fail;  /* true: falhou envio => hard-off */
    int32_t        tau_seconds;        /* TAU desejado (PSM); 0 = default */
    int32_t        t3324_seconds;      /* AT (janela ativa); 0 = mínimo */
} lte_pwr_policy_t;

int32_t lte_power_apply(uDeviceHandle_t devHandle,
                       // lte_power_strategy_t strategy,
                        int32_t legacy_strategy,   /* mantido p/ compat; ignorado internamente */
                        int32_t tau_seconds,
                        int32_t *tau_applied_out);
                        
/* Nova API: aplica a POLÍTICA após o ciclo e informa se deve pular cleanup duro. */
int32_t lte_apply_policy_after_tx(uDeviceHandle_t devHandle,
                                  bool delivery_ok,
                                  const lte_pwr_policy_t *policy,
                                  bool *skip_cleanup_out);
                                  
/** Opcional: habilita URC de PSM (+UUPSMR) para diagnóstico. */
void lte_power_diag_enable_psm_urc(uDeviceHandle_t devHandle);

/** Ativa/desativa a economia de energia na UART do modem (AT+UPSV).
 *  Se enable=true, aplica AT+UPSV=4 e (opcional) AT&D=1; se false, AT+UPSV=0.
 *  Em seguida consulta AT+UPSV? e devolve em upsv_after (se não for NULL).
 *  Retorna 0 em sucesso ou erro u_error_common.
 */
int32_t lte_power_set_uart_psm(uDeviceHandle_t devHandle,
                               bool enable,
                               bool set_atd1,
                               int32_t *upsv_after);

/** Consulta o estado atual de UPSV (AT+UPSV?). Retorna 0 e grava em upsv_out. */
int32_t lte_power_get_uart_psm(uDeviceHandle_t devHandle,
                               int32_t *upsv_out);

#ifdef __cplusplus
}
#endif

#endif /* U_CELL_POWER_STRATEGY_H_ */

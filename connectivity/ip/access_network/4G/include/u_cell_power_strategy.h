#ifndef U_CELL_POWER_STRATEGY_H_
#define U_CELL_POWER_STRATEGY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "u_device_handle.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LTE_PWR_OFF = 0,
    LTE_PWR_PSM_MIN = 1
} lte_power_strategy_t;

int32_t lte_power_apply(uDeviceHandle_t devHandle,
                        lte_power_strategy_t strategy,
                        int32_t tau_seconds,
                        int32_t *tau_applied_out);

int32_t lte_uart_psm_enable(uDeviceHandle_t devHandle,
                            bool set_atd1,
                            int32_t *upsv_out);

int32_t lte_uart_psm_disable(uDeviceHandle_t devHandle);

#ifdef __cplusplus
}
#endif

#endif /* U_CELL_POWER_STRATEGY_H_ */

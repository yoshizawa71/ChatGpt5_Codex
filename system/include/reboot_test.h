/*
 * reboot_test.h
 *
 *  Created on: 17 de set. de 2025
 *      Author: geopo
 */

#ifndef SYSTEM_INCLUDE_REBOOT_TEST_H_
#define SYSTEM_INCLUDE_REBOOT_TEST_H_

#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    REBOOT_SOFT = 0,   // esp_restart()
    REBOOT_PANIC,      // abort() -> panic
    REBOOT_WDT,        // força WDT de tarefa
} reboot_mode_t;

/** Agenda um reboot “de teste” para daqui a delay_ms milissegundos. */
esp_err_t test_trigger_reboot(reboot_mode_t mode, uint32_t delay_ms);

/** (Opcional) Loga o motivo do último reset no boot. */
void log_reset_reason_on_boot(void);

#ifdef __cplusplus
}
#endif


#endif /* SYSTEM_INCLUDE_REBOOT_TEST_H_ */

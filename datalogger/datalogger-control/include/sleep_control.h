/*
 * sleep_control.h
 *
 *  Created on: 17 de jul. de 2025
 *      Author: geopo
 */
#include <stdint.h>
#include <stdbool.h>

#ifndef DATALOGGER_DATALOGGER_CONTROL_INCLUDE_SLEEP_CONTROL_H_
#define DATALOGGER_DATALOGGER_CONTROL_INCLUDE_SLEEP_CONTROL_H_
extern uint32_t ulp_system_stable;

typedef enum wake_up_cause {
    WAKE_UP_BOOT,           // reset normal (power‑on, reset externo, SW‑reset sem ULP)
    WAKE_UP_WDT,            // reset por Watchdog Timer
    WAKE_UP_TIME,           // deep‑sleep wake por timer
    WAKE_UP_RING,           // (se você usar esse caso)
    WAKE_UP_PULSE,          // deep‑sleep wake por pulso ULP
    WAKE_UP_EXTERN_SENSOR,  // deep‑sleep wake por sensor externo
    WAKE_UP_INACTIVITY,     // primeiro boot após ULP indicar inatividade
    WAKE_UP_RST_PWR_ON,
    WAKE_UP_NONE            // nenhum evento relevante
} wake_up_cause_t;

enum wake_up_cause check_wakeup_cause(void);
void start_deep_sleep(void);
void set_inactivity(void);

void sleep_request_cap_recharge_window(void);
void sleep_clear_cap_recharge_window(void);
bool sleep_is_cap_recharge_window_pending(void);


#endif /* DATALOGGER_DATALOGGER_CONTROL_INCLUDE_SLEEP_CONTROL_H_ */

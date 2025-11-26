/*
 * system.h
 *
 *  Created on: 9 de mai. de 2025
 *      Author: geopo
 */

#ifndef SYSTEM_INCLUDE_SYSTEM_H_
#define SYSTEM_INCLUDE_SYSTEM_H_
#include "esp_err.h"
#include <stdint.h>

// --- Guard de frequência de CPU ---
typedef struct {
    int prev_mhz;
} cpu_freq_guard_t;


void init_system(void);
void set_cpu_frequency(int freq_mhz);
void set_cpu_freq_rtc(int freq_mhz);

void init_filesystem(void);
void deinit_filesystem(void);

void cpu_boost_begin_160(void);
void cpu_boost_end_160(void);

void cpu_boost_begin_240(void);
void cpu_boost_end_240(void);

// opcional (se quiser mudar o “base” em runtime)
void cpu_boost_set_base_mhz(int mhz);
int  cpu_get_current_mhz(void);

/** Lê a frequência atual da CPU (MHz) via RTC. */
int cpu_read_current_mhz_rtc(void);

/** Eleva para target_mhz, guardando a frequência anterior em g->prev_mhz. */
void cpu_freq_guard_enter(cpu_freq_guard_t *g, int target_mhz);

/** Restaura a frequência anterior salva em g->prev_mhz. */
void cpu_freq_guard_exit(cpu_freq_guard_t *g);

esp_err_t system_net_core_init(void);

#endif /* SYSTEM_INCLUDE_SYSTEM_H_ */

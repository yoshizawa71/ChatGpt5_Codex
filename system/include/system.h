/*
 * system.h
 *
 *  Created on: 9 de mai. de 2025
 *      Author: geopo
 */

#ifndef SYSTEM_INCLUDE_SYSTEM_H_
#define SYSTEM_INCLUDE_SYSTEM_H_
#include <stdint.h>

void init_system(void);
void set_cpu_frequency(int freq_mhz);
void set_cpu_freq_rtc(int freq_mhz);

void init_filesystem(void);
void deinit_filesystem(void);

#endif /* SYSTEM_INCLUDE_SYSTEM_H_ */

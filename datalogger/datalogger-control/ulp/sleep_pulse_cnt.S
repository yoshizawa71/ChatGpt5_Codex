/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* ULP Example: pulse counting

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.

   This file contains assembly code which runs on the ULP.

   ULP wakes up to run this code at a certain period, determined by the values
   in SENS_ULP_CP_SLEEP_CYCx_REG registers. On each wake up, the program checks
   the input on GPIO0. If the value is different from the previous one, the
   program "debounces" the input: on the next debounce_max_count wake ups,
   it expects to see the same value of input.
   If this condition holds true, the program increments edge_count and starts
   waiting for input signal polarity to change again.
   When the edge counter reaches certain value (set by the main program),
   this program running triggers a wake up from deep sleep.
*/

/* ULP assembly files are passed through C preprocessor first, so include directives
   and C macros may be used in these files
 */
#include "sdkconfig.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc_ulp.h"
#include "soc/sens_reg.h"

	/* Define variables, which go into .bss section (zero-initialized data) */
	.bss
//++++++++++++Adicionado para debouce do Io26++++++++++
.global io26_debounce_counter
io26_debounce_counter:
    .long 0
.global io26_debounce_max_count
io26_debounce_max_count:
    .long 0
.global ext_sensor_status
ext_sensor_status:
    .long 0
.global ext_sensor_activated
ext_sensor_activated:
    .long 0

//+++++++++++++++++++++++++++++++++++++++++
.global system_stable
system_stable:
    .long 0

.global sleep_transition_counter
sleep_transition_counter:
    .long 0
//+++++++++++++++++++++++++++++++++++++++++
.global inactivity
inactivity:
	.long 0

	/* Next input signal edge expected: 0 (negative) or 1 (positive) */
	.global next_edge
next_edge:
	.long 0

	/* Counter started when signal value changes.
	   Edge is "debounced" when the counter reaches zero. */
	.global debounce_counter
debounce_counter:
	.long 0

	/* Value to which debounce_counter gets reset.
	   Set by the main program. */
	.global debounce_max_count
debounce_max_count:
	.long 0

	/* Total number of signal edges acquired */
	.global edge_count
edge_count:
	.long 0

	/* Number of edges to acquire before waking up the SoC.
	   Set by the main program. */
	.global edge_count_to_wake_up
edge_count_to_wake_up:
	.long 0

	/* RTC IO number used to sample the input signal.
	   Set by main program. */
	.global io_number
io_number:
	.long 0

//------------------------------------------
/* EXT_SENSOR Level */

.global ext_sensor_status_next
ext_sensor_status_next:
	.long 0

.global io_ext_sensor
io_ext_sensor:
	.long 0
//------------------------------------------
.bss//Variaveis sao declaradas dentro da secao .bss

    //GPIO 26 - RTC 7 -->ext_sensor
    .set io26, 7

	/* Code goes into .text section */
	.text
	.global entry
entry:
     /* Verificar o contador de transiÃ§Ã£o para o deep sleep */
    move r3, sleep_transition_counter
    ld r3, r3, 0
    add r3, r3, 0
    jump check_transition, ov  // Se sleep_transition_counter > 0, ignorar pulsos


    /* Verificar se o sistema estÃ¡ estÃ¡vel */
    move r3, system_stable
    ld r3, r3, 0
    add r3, r3, 0
    jump skip_counting, eq  // Se system_stable == 0, pular a contagem

	/* Load io_number */
	move r3, io_number
	ld r3, r3, 0

#if CONFIG_IDF_TARGET_ESP32S2
    /* ESP32S2 powers down RTC periph when entering deep sleep and thus by association SENS_SAR_IO_MUX_CONF_REG */
	WRITE_RTC_FIELD(SENS_SAR_IO_MUX_CONF_REG, SENS_IOMUX_CLK_GATE_EN, 1)
#elif CONFIG_IDF_TARGET_ESP32S3
    /* ESP32S3 powers down RTC periph when entering deep sleep and thus by association SENS_SAR_PERI_CLK_GATE_CONF_REG */
    WRITE_RTC_FIELD(SENS_SAR_PERI_CLK_GATE_CONF_REG, SENS_IOMUX_CLK_EN, 1);
#endif

	/* Lower 16 IOs and higher need to be handled separately,
	 * because r0-r3 registers are 16 bit wide.
	 * Check which IO this is.
	 */
/*	move r0, r3
	jumpr read_io_high, 16, ge
*/

	/* Read the value of lower 16 RTC IOs into R0 */
	READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S, 16)
//----------------------
//verifica se o io26 foi ativado
    move r1, r0
	and r1, r0, 1<<7
    jump pin_check, eq
//----------------------
	rsh r0, r0, r3
//	jump read_done
    and r0, r0, 1
    move r3, next_edge
    ld r3, r3, 0
    sub r3, r0, r3
    jump changed, eq
    move r3, debounce_max_count
    move r2, debounce_counter
    ld r3, r3, 0
    st r3, r2, 0
    halt

	/* Read the value of RTC IOs 16-17, into R0 */
read_io_high:
	READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S + 16, 2)
	sub r3, r3, 16
	rsh r0, r0, r3

read_done:
    and r0, r0, 1           // Garante que r0 seja 0 ou 1
    move r3, next_edge      // Carrega o endereço de next_edge
    ld r3, r3, 0            // Carrega o valor de next_edge em r3
    sub r3, r0, r3          // Subtrai next_edge de r0
    jump no_change, eq      // Se r0 == next_edge, estado não mudou, vai para no_change
    // Se chegou aqui, r0 != next_edge, então o estado mudou
    jump changed            // Prossegue para a lógica de debounce em changed

no_change:
    move r3, debounce_max_count  // Carrega o valor máximo do debounce
    move r2, debounce_counter    // Carrega o endereço do contador de debounce
    ld r3, r3, 0                 // Obtém o valor de debounce_max_count
    st r3, r2, 0                 // Reseta debounce_counter para debounce_max_count
    halt                         // Encerra o programa

	check_transition:
    /* Decrementar sleep_transition_counter */
    move r3, sleep_transition_counter
    ld r2, r3, 0
    sub r2, r2, 1
    st r2, r3, 0
    jump skip_counting, ov // Pular contagem enquanto > 0
    halt  // Ignorar pulsos durante a transiÃ§Ã£o

	skip_counting:
    halt

	.global changed
changed:
	/* Input state changed */
	/* Has debounce_counter reached zero? */
	move r3, debounce_counter
	ld r2, r3, 0
	add r2, r2, 0 /* dummy ADD to use "jump if ALU result is zero" */
	jump edge_detected, eq
	/* Not yet. Decrement debounce_counter */
	sub r2, r2, 1
	st r2, r3, 0
	/* End program */
	halt

	.global edge_detected
edge_detected:
	/* Reset debounce_counter to debounce_max_count */
	move r3, debounce_max_count
	move r2, debounce_counter
	ld r3, r3, 0
	st r3, r2, 0
	/* Flip next_edge */
	move r3, next_edge
	ld r2, r3, 0
	add r2, r2, 1
	and r2, r2, 1
	st r2, r3, 0
	/* Increment edge_count */
	move r3, edge_count
	ld r2, r3, 0
	add r2, r2, 1
	st r2, r3, 0

	/* Not yet. End program */
	halt

//--------read ext_sensor status
pin_check:
    rsh r0, r1, io26
    and r0, r0, 1
    move r3, ext_sensor_status
    ld r3, r3, 0
    sub r3, r0, r3
    jump pin_check_reset, eq  // Sem mudança, reseta
    jump io26_debounce_check

pin_check_reset:
    move r3, io26_debounce_max_count
    move r2, io26_debounce_counter
    ld r3, r3, 0
    st r3, r2, 0
    halt

//++++++Adicionado o debouce++++++
io26_debounce_check:
rsh r0, r1, io26  // Lê novamente para confirmar estabilidade
    and r0, r0, 1
    move r3, ext_sensor_status
    ld r3, r3, 0
    sub r3, r0, r3
    jump pin_check, eq  // Se mudou de volta, reseta
    move r3, io26_debounce_counter
    ld r2, r3, 0
    sub r2, r2, 1
    st r2, r3, 0
    jump ext_sensor_status_changed, eq
    halt
//++++++++++++++++++++++++++++++
ext_sensor_status_changed:
move r3, io26_debounce_max_count
    move r2, io26_debounce_counter
    ld r3, r3, 0
    st r3, r2, 0
    move r3, ext_sensor_status
    rsh r0, r1, io26
    and r0, r0, 1
    st r0, r3, 0  // Atualiza estado
    move r3, ext_sensor_activated
    ld r2, r3, 0
    add r2, r2, 1
    st r2, r3, 0
//    jump wake_up
    wake // Acionar wakeup
    halt



/* ULP assembly files are passed through C preprocessor first, so include directives
   and C macros may be used in these files 
 */
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc_ulp.h"


/* SMS RING Level */
/*	.global ring_status
ring_status:
	.long 0

.global ring_status_next
ring_status_next:
	.long 0

.global rang
rang:
	.long 0
*/
//------------------------------------------
/* PULSE Level */
.global pulse_status
pulse_status:
	.long 0

.global pulse_status_next
pulse_status_next:
	.long 0

.global pulsed
pulsed:
	.long 0
//------------------------------------------
/* EXT_SENSOR Level */
/*.global ext_sensor_status
ext_sensor_status:
	.long 0

.global ext_sensor_status_next
ext_sensor_status_next:
	.long 0

.global ext_sensor_activated
ext_sensor_activated:
	.long 0

.global io_ext_sensor
io_ext_sensor:
	.long 0
*/
//------------------------------------------
/*.global inactivity
inactivity:
	.long 0
*/

.global err_halt
err_halt:
	.long 0

.bss//Variaveis sao declaradas dentro da secao .bss
    //GPIO 32 - RTC 9 -->Ring
    //GPIO 36 - RTC 0 -->Pulse
    //GPIO 26 - RTC 7 -->ext_sensor
//    .set io32, 9
    .set io36, 0
    .set io26, 7
   // The LED is connected on RTC GPIO 06
/*
    .set    LED,  6
    .set    timedelay, 29500 //Tempo entre as piscadas
*/
	/* Code goes into .text section */
	.text
	.global entrance
entrance:

//move    r2, timedelay

pin_check:

// Read the value of lower 16 RTC IOs into R0

	READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S, 16)
	move r1, r0

//--------read ring status
/*	move r3, ring_status
    rsh r0, r1, io32
	and r0, r0, 1
	st r0, r3, 0
*/
//--------check if ring status changed
/*    rsh r0, r1, io32
	and r0, r0, 1
	move r3, ring_status_next
	ld r3, r3, 0
	add r3, r0, r3
    and r3, r3, 1
	jump ring_status_changed, eq
*/
//--------read pulse status
	move r3, pulse_status
    rsh r0, r1, io36
	and r0, r0, 1
	st r0, r3, 0

//--------check if pulse status changed
    rsh r0, r1, io36
	and r0, r0, 1
	move r3, pulse_status_next
	ld r3, r3, 0
	add r3, r0, r3
    and r3, r3, 1
	jump pulse_status_changed, eq

/*
//--------read ext_sensor status
	move r3, ext_sensor_status
	rsh r0, r1, io26
	and r0, r0, 1
	st r0, r3, 0
//	move r2, r0
//	move io_ext_sensor, r2 //teste

//--------check if ext_sensor status changed
    rsh r0, r1, io26
	and r0, r0, 1
	move r3, ext_sensor_status_next
	ld r3, r3, 0
	add r3, r0, r3
    and r3, r3, 1
	jump ext_sensor_status_changed, eq
*/


//    wait    8000
    // Reduce the counter by one
/*    sub   r2, r2, 1
    move r0, r2
    jumpr blinkLED, 0,EQ
*/
	jump pin_check
/*
ring_status_changed:
	move r3, ring_status_next
	ld r2, r3, 0
	add r2, r2, 1
	and r2, r2, 1
	st r2, r3, 0

	move r3, rang
	ld r2, r3, 0
	add r2, r2, 1
	st r2, r3, 0
    jump wake_up
*/
//---------------------------------
pulse_status_changed:
	move r3, pulse_status_next
	ld r2, r3, 0
	add r2, r2, 1
	and r2, r2, 1
	st r2, r3, 0

	move r3, pulsed
	ld r2, r3, 0
	add r2, r2, 1
	st r2, r3, 0
    jump wake_up
//---------------------------------
/*
ext_sensor_status_changed:
	move r3, ext_sensor_status_next
	ld r2, r3, 0
	add r2, r2, 1
	and r2, r2, 1
	st r2, r3, 0

	move r3, ext_sensor_activated
	ld r2, r3, 0
	add r2, r2, 1
	st r2, r3, 0
    jump wake_up
    */
/*
blinkLED:
    WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_W1TS_S+13, 1, 1)//GPIO25 HIGH

    stage_rst//Reseta o registrador (STAGE_CNT = 0)
    1: stage_inc 1//Incrementa o registrador em +1 (STAGE_CNT++)
    wait 32000//Equivale a 32000 NOP's
    jumps 1b, 125, lt//Pula para label "1before" enquanto o registrador for menor que 125

//**********************************************
    WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_W1TC_S+13, 1, 1)//GPIO25 LOW

    stage_rst
    1: stage_inc 1
    wait 32000
    jumps 1b, 125, lt//delay 500ms
*/
jump entrance

halt




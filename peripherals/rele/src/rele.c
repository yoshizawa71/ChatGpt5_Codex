/*
 * rele.c
 *
 *  Created on: 16 de out. de 2021
 *      Author: geopo
 */

#include "driver/gpio.h"
#include "datalogger_driver.h"
#include "esp32/rom/gpio.h"
#include <stdint.h>

static void rele_setup(void);

uint8_t rele=5;


static void rele_setup(void)
{
        printf("Setup do rele\n");
        gpio_pad_select_gpio(rele);
        gpio_set_direction(rele, GPIO_MODE_OUTPUT);
        gpio_set_level(rele, 0);

}

void rele_turn_on(void)
{
    gpio_set_level(rele, 1);
}

void rele_turn_off(void)
{
    gpio_set_level(rele, 0);
}


void turn_rele_On_Off(int time)
{
	for(int i=0; i<=time; i++)
	{
     printf("rele turn on\n");
     rele_turn_on();
     vTaskDelay(pdMS_TO_TICKS(1000));
     rele_turn_off();
     printf("rele turn off\n");
	}
}

void rele_init(void)
{
	rele_setup();

}


// led_blink_control.h
#pragma once

#include "driver/gpio.h"

// Defina aqui o GPIO onde o LED está conectado
#define LED_PIN    GPIO_NUM_25

typedef enum {
    BLINK_PROFILE_NONE = 0,
    BLINK_PROFILE_DEVICE_RUNNING,
    BLINK_PROFILE_COMM_START,
    BLINK_PROFILE_COMM_REGISTERED,
    BLINK_PROFILE_DELIVERY_SUCCESS,
    BLINK_PROFILE_COMM_FAIL,
    BLINK_PROFILE_MAX
} blink_profile_t;

// Inicializa o módulo de blink (chame no startup)
void blink_init(void);

// Altera o perfil de piscada do LED
void blink_set_profile(blink_profile_t prof);

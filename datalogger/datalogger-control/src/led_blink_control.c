// led_blink_control.c

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "led_blink_control.h"
#include "rom/gpio.h"

#define BLINK_PERIOD_MS  50  // resolução do loop

// Parâmetros para cada perfil (SEM ALTERAÇÃO)
static const struct {
    uint32_t on;       // ms LED ligado
    uint32_t off;      // ms LED desligado
    uint32_t inactive; // ms extra desligado antes de repetir
} blink_params[BLINK_PROFILE_MAX] = {
    [BLINK_PROFILE_NONE] =           {   0,    0,    0 },
    [BLINK_PROFILE_DEVICE_RUNNING] = { 500,  500, 1000 },
    [BLINK_PROFILE_COMM_START] =     {200,200, 0},
    [BLINK_PROFILE_DELIVERY_SUCCESS] =  {3000,500, 1000 },
    [BLINK_PROFILE_COMM_FAIL] =      { 10,  10, 300 }
};

// Novo estado interno do LED (substitui o antigo led.elapsed_inactive etc.)
typedef enum {
    LED_STATE_INACTIVE = 0,
    LED_STATE_ON,
    LED_STATE_OFF
} LedState_t;

static struct {
    blink_profile_t profile;  // qual perfil está ativo
    LedState_t      state;    // em qual fase do ciclo estamos
    uint32_t        elapsed;  // ms desde que entrou nesse state
} led = {
    .profile = BLINK_PROFILE_NONE,
    .state   = LED_STATE_INACTIVE,
    .elapsed = 0
};

// Task que faz o blink
static void led_blink_task(void *arg) {
    for (;;) {
        // pega parâmetros do perfil
        uint32_t t_on       = blink_params[led.profile].on;
        uint32_t t_off      = blink_params[led.profile].off;
        uint32_t t_inactive = blink_params[led.profile].inactive;

        // 1) INACTIVE: pausa extra
        if (t_inactive > 0) {
            gpio_set_level(LED_PIN, 0);                // LED OFF (ativo‑alto)
            vTaskDelay(pdMS_TO_TICKS(t_inactive));
        }

        // 2) ON
        if (t_on > 0) {
            gpio_set_level(LED_PIN, 1);                // LED ON
            vTaskDelay(pdMS_TO_TICKS(t_on));
        }

        // 3) OFF
        if (t_off > 0) {
            gpio_set_level(LED_PIN, 0);                // LED OFF
            vTaskDelay(pdMS_TO_TICKS(t_off));
        }
        // repete o ciclo naturalmente
    }
}

void blink_init(void) {
    // configura o pino como saída
    gpio_pad_select_gpio(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    // LED começa desligado
    gpio_set_level(LED_PIN, 0);
    // cria a task de blink
    xTaskCreate(led_blink_task, "led_blink", 2048, NULL, tskIDLE_PRIORITY, NULL);
}

void blink_set_profile(blink_profile_t prof) {
    if ((int)prof < 0 || prof >= BLINK_PROFILE_MAX) {
        return;
    }
    led.profile = prof;
    // reinicia o ciclo para respeitar o INACTIVE antes do primeiro ON
    led.state   = LED_STATE_INACTIVE;
    led.elapsed = 0;
}

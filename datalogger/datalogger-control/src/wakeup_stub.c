/***** wakeup_stub.h *****/
#pragma once
#include "esp_sleep.h"

/**
 * @brief Register the wake-up stub to run before full CPU wake-up.
 *
 * This must be called before entering deep sleep (e.g., in app_main()).
 * The stub will execute from RTC_FAST memory immediately after wake-up,
 * disabling the RTC hold on GPIO27 and driving it high with full 3.3V push-pull.
 */
void init_wakeup_stub(void);


/***** wakeup_stub.c *****/
#include "wakeup_stub.h"
#include "driver/rtc_io.h"
#include "esp_attr.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"   // for CLEAR_PERI_REG_MASK on hold register
#include "soc/rtc_io_reg.h"   // define RTC_IO_HOLD0_REG, RTC_IO_HOLD1_REG

// GPIO used for powering the buck/MOSFET
#define WAKE_GPIO_NUM GPIO_NUM_27

// Variável na RTC memory para depuração
RTC_DATA_ATTR int stub_executed = 0;
RTC_DATA_ATTR int gpio27_level = 0;

/**
 * @brief Wake-up stub executed from RTC_FAST memory before bootloader.
 *        Releases RTC hold on WAKE_GPIO_NUM, configures it as push-pull,
 *        and drives it high to 3.3V with full digital drive.
 */
RTC_IRAM_ATTR void rtc_wakeup_stub(void)
{
    stub_executed++;

    // 1) Reseta o pino para estado conhecido
    REG_WRITE(IO_MUX_GPIO27_REG, 0); // Reseta configuração do pino

    // 2) Configura como GPIO digital
    esp_rom_gpio_pad_select_gpio(WAKE_GPIO_NUM);

    // 3) Configura como saída push-pull
    SET_PERI_REG_MASK(GPIO_ENABLE_W1TS_REG, BIT(WAKE_GPIO_NUM));
    CLEAR_PERI_REG_MASK(GPIO_ENABLE_W1TC_REG, BIT(WAKE_GPIO_NUM));

    // 4) Seta nível alto (3.3V)
    SET_PERI_REG_MASK(GPIO_OUT_W1TS_REG, BIT(WAKE_GPIO_NUM));

    // 5) Lê o nível do pino para depuração
    gpio27_level = (REG_READ(GPIO_IN_REG) >> WAKE_GPIO_NUM) & 1;

    // 6) Atraso para estabilizar o hardware
    for (volatile int i = 0; i < 10000; i++) { }

    // 7) Chama a função padrão para continuar o wake-up
    esp_default_wake_deep_sleep();
}

void init_wakeup_stub(void)
{
    esp_set_deep_sleep_wake_stub(rtc_wakeup_stub);
}


/*RTC_IRAM_ATTR void rtc_test_wakeup_led25_stub(void)
{
    // 1) Mapear pad para saída digital GPIO25
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO25_U, FUNC_GPIO25_GPIO25);
    // 2) Habilitar direção de saída
    SET_PERI_REG_MASK(GPIO_ENABLE_W1TS_REG, (1U << TEST_LED_GPIO));
    // 3) Drive HIGH (3.3 V)
    SET_PERI_REG_MASK(GPIO_OUT_W1TS_REG,    (1U << TEST_LED_GPIO));
}

*
 * @brief  Registra o stub de teste em vez do normal.
 * 
 * Chame isso ANTES de esp_deep_sleep_start() para usar
 * o rtc_test_wakeup_led25_stub em vez do rtc_wakeup_stub.
 
void init_test_wakeup_stub(void)
{
    esp_set_deep_sleep_wake_stub(rtc_test_wakeup_led25_stub);
}
*/
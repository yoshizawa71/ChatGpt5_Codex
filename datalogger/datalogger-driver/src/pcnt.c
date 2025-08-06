#include "driver/pcnt.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "datalogger_driver.h"

#include "esp_attr.h"
#include "esp_log.h"

#define PCNT_UNIT       PCNT_UNIT_0
#define PCNT_CHANNEL    PCNT_CHANNEL_0
static const char* TAG = "PCNT";

void init_pcnt(void)
{
// 1) Desliga qualquer uso RTC do pino de pulso
	rtc_gpio_deinit(PCNT_INPUT_PIN);
	
	    // 2) Define pull-down para evitar flutuação quando o reed não estiver ativo
    gpio_set_pull_mode(PCNT_INPUT_PIN, GPIO_PULLDOWN_ONLY);
    // se seu reed switch for pull-up (fechado leva ao GND), use:
    // gpio_set_pull_mode(PCNT_INPUT_PIN, GPIO_PULLUP_ONLY);

    // 3) Configura o PCNT para contar só as subidas
      pcnt_config_t pcnt_config = {
        .pulse_gpio_num   = PCNT_INPUT_PIN,
        .ctrl_gpio_num    = -1,
        .channel          = PCNT_CHANNEL,
        .unit             = PCNT_UNIT,
        .pos_mode         = PCNT_COUNT_INC,  // só subidas
        .neg_mode         = PCNT_COUNT_DIS,  // ignora descidas
        .counter_h_lim    = 32767,
        .counter_l_lim    = 0,
    };

    pcnt_unit_config(&pcnt_config);

    pcnt_set_filter_value(PCNT_UNIT, 1000);//Filter
    pcnt_filter_enable(PCNT_UNIT);

    pcnt_counter_pause(PCNT_UNIT);
    pcnt_counter_clear(PCNT_UNIT);

    pcnt_counter_resume(PCNT_UNIT);
    
     ESP_LOGI(TAG, "PCNT iniciado com filtro HW=1023 (≈12.8µs)");  
}

int16_t get_pulse_count(void)
{
    int16_t count;
    pcnt_get_counter_value(PCNT_UNIT, &count);

    return count;
}

void reset_pulse_count(void)
{
    pcnt_counter_pause(PCNT_UNIT);
    pcnt_counter_clear(PCNT_UNIT);
    pcnt_counter_resume(PCNT_UNIT);
}

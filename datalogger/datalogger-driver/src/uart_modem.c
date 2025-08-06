#include <stdbool.h>

#include "../../../libs/modem/include/esp_modem.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "sdkconfig.h"
#include "datalogger_driver.h"
#include "esp32/rom/gpio.h"
#include "freertos/FreeRTOS.h"
//#include "gpio_num.h"


#if CONFIG_MODEM_DEVICE_SIM800
    #define MODEM_UART_TX_PIN       27
    #define MODEM_UART_RX_PIN       26
    #define MODEM_UART_RTS_PIN      5
    #define MODEM_UART_CTS_PIN      22
    #define MODEM_UART_PWKEY_PIN    4
    #define MODEM_UART_POWER_ON_PIN     23
#elif CONFIG_MODEM_DEVICE_SARA_G450
    #define MODEM_UART_TX_PIN       17
    #define MODEM_UART_RX_PIN       16
    #define MODEM_UART_RTS_PIN      UART_PIN_NO_CHANGE
    #define MODEM_UART_CTS_PIN      UART_PIN_NO_CHANGE
    #define MODEM_POWER_ON_PIN     25
    #define MODEM_POWER_ON_RTC_PIN     6
    #define MODEM_POWER_OFF_PIN     2
    #define MODEM_IO_PIN     4
    #define MODEM_DTR_PIN     26
    
    #define RS422_PWR_CTRL_PIN   23
    
#else
#error "Unsupported DCE"
#endif

#define MODEM_UART_TX_BUFFER_SIZE   512
#define MODEM_UART_RX_BUFFER_SIZE   2048
#define MODEM_UART_PATTERN_QUEUE_SIZE   40
#define MODEM_UART_EVENT_QUEUE_SIZE     40
#define MODEM_UART_EVENT_TASK_STACK_SIZE    6000
#define MODEM_UART_EVENT_TASK_PRIORITY  5
#define MODEM_UART_LINE_BUFFER_SIZE     MODEM_UART_RX_BUFFER_SIZE/2

void config_modem_uart(esp_modem_dte_config_t* config)
{
    config->tx_io_num = MODEM_UART_TX_PIN;
    config->rx_io_num = MODEM_UART_RX_PIN;
    config->rts_io_num = MODEM_UART_RTS_PIN;
    config->cts_io_num = MODEM_UART_CTS_PIN;
    config->rx_buffer_size = MODEM_UART_RX_BUFFER_SIZE;
    config->tx_buffer_size = MODEM_UART_TX_BUFFER_SIZE;
    config->pattern_queue_size = MODEM_UART_PATTERN_QUEUE_SIZE;
    config->event_queue_size = MODEM_UART_EVENT_QUEUE_SIZE;
    config->event_task_stack_size = MODEM_UART_EVENT_TASK_STACK_SIZE;
    config->event_task_priority = MODEM_UART_EVENT_TASK_PRIORITY;
    config->line_buffer_size = MODEM_UART_LINE_BUFFER_SIZE;

}

void turn_on_modem(void)
{
	#if CONFIG_MODEM_DEVICE_SIM800
	    gpio_pad_select_gpio(MODEM_UART_PWKEY_PIN);
	    gpio_pad_select_gpio(MODEM_UART_POWER_ON_PIN);
	    gpio_pad_select_gpio(MODEM_UART_RTS_PIN);

	    gpio_set_direction(MODEM_UART_PWKEY_PIN, GPIO_MODE_OUTPUT);
	    gpio_set_direction(MODEM_UART_RTS_PIN, GPIO_MODE_OUTPUT);
	    gpio_set_direction(MODEM_UART_POWER_ON_PIN, GPIO_MODE_OUTPUT);

	    gpio_set_level(MODEM_UART_PWKEY_PIN, 0);
	    gpio_set_level(MODEM_UART_RTS_PIN, 1);
	    gpio_set_level(MODEM_UART_POWER_ON_PIN, 1);
	    vTaskDelay(1500/portTICK_PERIOD_MS);
	    gpio_set_level(MODEM_UART_PWKEY_PIN, 1);
	    vTaskDelay(20000/portTICK_PERIOD_MS);
	#elif CONFIG_MODEM_DEVICE_SARA_G450
	    // rtc_gpio_deinit(MODEM_POWER_ON_PIN);
	    // rtc_gpio_init(MODEM_POWER_ON_PIN);
	    // rtc_gpio_set_direction(MODEM_POWER_ON_PIN, RTC_GPIO_MODE_OUTPUT_ONLY);
	    // rtc_gpio_pulldown_dis(MODEM_POWER_ON_PIN);
	    // rtc_gpio_pullup_dis(MODEM_POWER_ON_PIN);
	    // rtc_gpio_hold_en(MODEM_POWER_ON_PIN);
	    // rtc_gpio_isolate(MODEM_POWER_ON_PIN);

	    gpio_pad_select_gpio(MODEM_POWER_ON_PIN);
	    gpio_pad_select_gpio(MODEM_POWER_OFF_PIN);
	    gpio_pad_select_gpio(MODEM_IO_PIN);
	    gpio_pad_select_gpio(MODEM_DTR_PIN);

	    gpio_set_direction(MODEM_POWER_ON_PIN, GPIO_MODE_OUTPUT);
	    gpio_set_direction(MODEM_POWER_OFF_PIN, GPIO_MODE_OUTPUT);
	    gpio_set_direction(MODEM_IO_PIN, GPIO_MODE_OUTPUT);
	    gpio_set_direction(MODEM_DTR_PIN, GPIO_MODE_OUTPUT);

	    gpio_set_level(MODEM_POWER_OFF_PIN, 1);
	    vTaskDelay(1000/portTICK_PERIOD_MS);
	    gpio_set_level(MODEM_POWER_OFF_PIN, 0);
	    gpio_set_level(MODEM_POWER_ON_PIN, 1);
	    // rtc_gpio_set_level(MODEM_POWER_ON_PIN, 1);
	    vTaskDelay(1000/portTICK_PERIOD_MS);
	    gpio_set_level(MODEM_IO_PIN, 1);
	    gpio_set_level(MODEM_POWER_ON_PIN, 0);
	    gpio_set_level(MODEM_DTR_PIN, 0);
	    vTaskDelay(15000/portTICK_PERIOD_MS);
	#else
	#error "Unsupported DCE"
	#endif
}

void turn_off_modem(void)
{
        printf(">>>>>>>>>>.Turn off modem<<<<<<<<<<<\n");
	    gpio_pad_select_gpio(MODEM_POWER_ON_PIN);
	    gpio_pad_select_gpio(MODEM_POWER_OFF_PIN);
	    gpio_pad_select_gpio(MODEM_IO_PIN);
	    gpio_pad_select_gpio(MODEM_DTR_PIN);

	    gpio_set_direction(MODEM_POWER_ON_PIN, GPIO_MODE_OUTPUT);
	    gpio_set_direction(MODEM_POWER_OFF_PIN, GPIO_MODE_OUTPUT);
	    gpio_set_direction(MODEM_IO_PIN, GPIO_MODE_OUTPUT);
	    gpio_set_direction(MODEM_DTR_PIN, GPIO_MODE_OUTPUT);

	    gpio_set_level(MODEM_POWER_OFF_PIN, 1);
	    vTaskDelay(1000/portTICK_PERIOD_MS);
	    gpio_set_level(MODEM_POWER_OFF_PIN, 0);

	    vTaskDelay(1000/portTICK_PERIOD_MS);

	    gpio_set_level(MODEM_POWER_ON_PIN, 0);

	    vTaskDelay(5000/portTICK_PERIOD_MS);

}
void pwr_ctrl(void)
{
//	gpio_num_t GPIO_PWR_CTRL_PIN=23;
	
	gpio_pad_select_gpio(RS422_PWR_CTRL_PIN);
	gpio_set_direction(RS422_PWR_CTRL_PIN, GPIO_MODE_OUTPUT);
	gpio_set_level(RS422_PWR_CTRL_PIN, 0);

	
}

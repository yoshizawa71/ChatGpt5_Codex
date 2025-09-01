#include "rs485_hw.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>
#include "esp_check.h"

static const char *TAG = "RS485_HW";
static rs485_hw_cfg_t s_cfg;

static uart_word_length_t to_word_len(int bits) {
    return (bits == 7) ? UART_DATA_7_BITS : UART_DATA_8_BITS;
}
static uart_parity_t to_parity(int p) {
    if (p == 1) return UART_PARITY_ODD;
    if (p == 2) return UART_PARITY_EVEN;
    return UART_PARITY_DISABLE;
}
static uart_stop_bits_t to_stop(int s) {
    return (s == 2) ? UART_STOP_BITS_2 : UART_STOP_BITS_1;
}

esp_err_t rs485_hw_init(const rs485_hw_cfg_t *cfg)
{
    s_cfg = *cfg;

    uart_config_t uc = {
        .baud_rate  = (int)cfg->baudrate,
        .data_bits  = to_word_len(cfg->data_bits),
        .parity     = to_parity(cfg->parity),
        .stop_bits  = to_stop(cfg->stop_bits),
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    ESP_RETURN_ON_ERROR(uart_driver_install(cfg->uart_num, 256, 0, 0, NULL, 0), TAG, "uart_driver_install");
    ESP_RETURN_ON_ERROR(uart_param_config(cfg->uart_num, &uc), TAG, "uart_param_config");
    ESP_RETURN_ON_ERROR(uart_set_pin(cfg->uart_num, cfg->tx_gpio, cfg->rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "uart_set_pin");

    // Pino DE/RE manual
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << cfg->de_re_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0, .pull_down_en = 0, .intr_type = GPIO_INTR_DISABLE
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio_config");
    gpio_set_level(cfg->de_re_gpio, 0); // RX por padrão

    // Timeout de leitura em bytes-time (aqui via tempo absoluto de read)
    uart_flush(cfg->uart_num);
    return ESP_OK;
}

static inline void set_tx(void){ gpio_set_level(s_cfg.de_re_gpio, 1); }
static inline void set_rx(void){ gpio_set_level(s_cfg.de_re_gpio, 0); }

esp_err_t rs485_hw_tx(const uint8_t *data, size_t len, TickType_t tmo)
{
    set_tx();
    int w = uart_write_bytes(s_cfg.uart_num, (const char*)data, len);
    if (w < 0 || (size_t)w != len) { set_rx(); return ESP_FAIL; }
    esp_err_t err = uart_wait_tx_done(s_cfg.uart_num, tmo);
    if (err != ESP_OK) { set_rx(); return err; }
    set_rx();
    return ESP_OK;
}

esp_err_t rs485_hw_rx(uint8_t *data, size_t max_len, size_t *out_len, TickType_t tmo)
{
    int r = uart_read_bytes(s_cfg.uart_num, data, max_len, tmo);
    if (r <= 0) return ESP_ERR_TIMEOUT;
    if (out_len) *out_len = (size_t)r;
    return ESP_OK;
}

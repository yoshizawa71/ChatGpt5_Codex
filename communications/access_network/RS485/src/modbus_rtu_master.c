#include "hal/uart_types.h"
#include "modbus_rtu_master.h"
#include "rs485_hw.h"
#include "esp_log.h"
#include <string.h>
#include "esp_check.h"
#include "soc/gpio_num.h"

static const char *TAG = "MB_RTU";





static uint16_t crc16_modbus(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t pos = 0; pos < len; pos++) {
        crc ^= buf[pos];
        for (int i = 0; i < 8; i++) {
            if (crc & 1) { crc >>= 1; crc ^= 0xA001; }
            else         { crc >>= 1; }
        }
    }
    return crc;
}

static inline void put_u16_be(uint8_t *p, uint16_t v){ p[0] = v >> 8; p[1] = v & 0xFF; }
static inline uint16_t get_u16_be(const uint8_t *p){ return ((uint16_t)p[0] << 8) | p[1]; }

esp_err_t modbus_master_rtu_init(void)
{
    rs485_hw_cfg_t hw = {
        .uart_num = UART_NUM_0,
        .tx_gpio = GPIO_NUM_1,
        .rx_gpio = GPIO_NUM_3,
        .de_re_gpio = GPIO_NUM_18,
        .baudrate = 9600,
        .data_bits = 8,
        .stop_bits = 1,
        .parity = 0,
        .rx_timeout_ms = 200,
    };
    return rs485_hw_init(&hw);
}

static esp_err_t mb_read_common(uint8_t addr, uint8_t func, uint16_t reg, uint16_t qty,
                                uint16_t *out, TickType_t tmo)
{
    uint8_t req[8];
    req[0] = addr;
    req[1] = func;
    put_u16_be(&req[2], reg);
    put_u16_be(&req[4], qty);
    uint16_t crc = crc16_modbus(req, 6);
    req[6] = crc & 0xFF;   // CRC low
    req[7] = crc >> 8;     // CRC high

    ESP_RETURN_ON_ERROR(rs485_hw_tx(req, sizeof(req), tmo), TAG, "tx");

    const size_t exp_len = 5 + (size_t)qty * 2; // addr+func+bytecount+data+crc(2)
    uint8_t resp[256] = {0};
    size_t got = 0;
    ESP_RETURN_ON_ERROR(rs485_hw_rx(resp, exp_len, &got, tmo), TAG, "rx");
    if (got != exp_len) return ESP_ERR_INVALID_SIZE;

    // CRC
    uint16_t rcrc = ((uint16_t)resp[exp_len-1] << 8) | resp[exp_len-2];
    if (crc16_modbus(resp, exp_len-2) != rcrc) return ESP_ERR_INVALID_CRC;

    if (resp[0] != addr || resp[1] != func) return ESP_FAIL;
    if (resp[2] != qty * 2) return ESP_ERR_INVALID_SIZE;

    for (int i=0;i<qty;i++) {
        out[i] = get_u16_be(&resp[3 + i*2]);
    }
    return ESP_OK;
}

esp_err_t modbus_master_read_holding(uint8_t addr, uint16_t reg, uint16_t qty,
                                     uint16_t *out, TickType_t tmo)
{
    return mb_read_common(addr, 0x03, reg, qty, out, tmo);
}

esp_err_t modbus_master_read_input(uint8_t addr, uint16_t reg, uint16_t qty,
                                   uint16_t *out, TickType_t tmo)
{
    return mb_read_common(addr, 0x04, reg, qty, out, tmo);
}

esp_err_t modbus_master_write_single(uint8_t addr, uint16_t reg, uint16_t value, TickType_t tmo)
{
    uint8_t req[8];
    req[0] = addr;
    req[1] = 0x06;
    put_u16_be(&req[2], reg);
    put_u16_be(&req[4], value);
    uint16_t crc = crc16_modbus(req, 6);
    req[6] = crc & 0xFF;
    req[7] = crc >> 8;

    ESP_RETURN_ON_ERROR(rs485_hw_tx(req, sizeof(req), tmo), TAG, "tx");

    uint8_t resp[8]; size_t got = 0;
    ESP_RETURN_ON_ERROR(rs485_hw_rx(resp, sizeof(resp), &got, tmo), TAG, "rx");
    if (got != sizeof(resp)) return ESP_ERR_INVALID_SIZE;

    uint16_t rcrc = ((uint16_t)resp[7] << 8) | resp[6];
    if (crc16_modbus(resp, 6) != rcrc) return ESP_ERR_INVALID_CRC;
    if (memcmp(req, resp, 6) != 0) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t modbus_master_ping(uint8_t addr, TickType_t tmo)
{
    uint16_t tmp;
    // leitura inocente de 1 holding em 0x0000
    return modbus_master_read_holding(addr, 0x0000, 1, &tmp, tmo);
}

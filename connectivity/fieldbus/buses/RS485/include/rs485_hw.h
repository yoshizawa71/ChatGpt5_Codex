/*
 * rs485_hw.h
 *
 * Camada fina de RS-485 com controle manual de DE/RE via GPIO
 * e UART do ESP-IDF. Projetada para trabalhar com esp-modbus
 * ou acesso RTU direto, e para integrar com um "session guard".
 */

#ifndef COMMUNICATIONS_RS485_INCLUDE_RS485_HW_H_
#define COMMUNICATIONS_RS485_INCLUDE_RS485_HW_H_
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

// Exija que tudo venha do menuconfig -----------------------------
#ifndef CONFIG_RS485_UART_NUM
#  error "CONFIG_RS485_UART_NUM não definido. Configure em menuconfig → RS-485 → UART para RS-485 (0/1/2)."
#endif
#ifndef CONFIG_RS485_TX_PIN
#  error "CONFIG_RS485_TX_PIN não definido. Configure em menuconfig → RS-485 → GPIO TX (DI do transceiver)."
#endif
#ifndef CONFIG_RS485_RX_PIN
#  error "CONFIG_RS485_RX_PIN não definido. Configure em menuconfig → RS-485 → GPIO RX (RO do transceiver)."
#endif
#ifndef CONFIG_RS485_DE_RE_PIN
#  error "CONFIG_RS485_DE_RE_PIN não definido. Configure em menuconfig → RS-485 → GPIO DE/RE (RTS)."
#endif

// Mapeie direto dos CONFIG_* -------------------------------------
#define RS485_UART_NUM     CONFIG_RS485_UART_NUM   // 0, 1 ou 2
#define RS485_TX_PIN       CONFIG_RS485_TX_PIN
#define RS485_RX_PIN       CONFIG_RS485_RX_PIN
#define RS485_DE_RE_PIN    CONFIG_RS485_DE_RE_PIN

#ifndef RS485_DEFAULT_BAUD
#  define RS485_DEFAULT_BAUD 9600
#endif
/* ========================================================================== */


typedef struct {
    int uart_num;
    int tx_gpio, rx_gpio;
    int de_re_gpio;
    uint32_t baudrate;
    int data_bits;   // 7 ou 8
    int stop_bits;   // 1 ou 2
    int parity;      // 0:none, 1:odd, 2:even
    int rx_timeout_ms;
} rs485_hw_cfg_t;

esp_err_t rs485_hw_init(const rs485_hw_cfg_t *cfg);

esp_err_t rs485_hw_tx(const uint8_t *data, size_t len, TickType_t tmo);
esp_err_t rs485_hw_rx(uint8_t *data, size_t max_len, size_t *out_len, TickType_t tmo);

/* Seleciona canal físico do multiplexador RS-485 (quando disponível). */
esp_err_t rs485_hw_select_channel(uint8_t channel);

/* Inicializa com os padrões acima (conveniência) */
esp_err_t rs485_hw_init_default(void);

/* Preenche uma cfg com os padrões (se quiser customizar e depois chamar init) */
void rs485_hw_fill_defaults(rs485_hw_cfg_t *cfg);

/* Opcional: expõe o UART configurado (útil para checagens) */
int rs485_hw_get_uart_num(void);


#endif /* COMMUNICATIONS_RS485_INCLUDE_RS485_HW_H_ */

/*
 * modbus_rtu_master.c
 * Master RTU (esp-modbus) desacoplado de qualquer "slave".
 * - init/start (sem tarefa leitora interna)
 * - wrappers de leitura (FC04/FC03) usados pelos drivers
 * - ping canônico (FC04@0x0001 → FC03@0x0000)
 */

#include "modbus_rtu_master.h"

#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "mbcontroller.h"

/* =================== Parametrização padrão (ajuste via Kconfig/compile defs) =================== */
#ifndef MB_PORT_NUM
#define MB_PORT_NUM        (0)        // UART0
#endif
#ifndef MB_DEV_SPEED
#define MB_DEV_SPEED       (9600)
#endif
#ifndef CONFIG_MB_UART_TXD
#define CONFIG_MB_UART_TXD (1)
#endif
#ifndef CONFIG_MB_UART_RXD
#define CONFIG_MB_UART_RXD (3)
#endif
#ifndef CONFIG_MB_UART_RTS
#define CONFIG_MB_UART_RTS (18)       // RTS como DE/RE (half-duplex)
#endif

/* Timeouts internos padrão (ticks) */
#ifndef MB_REQ_TIMEOUT_MS
#define MB_REQ_TIMEOUT_MS  (800)
#endif
#ifndef MB_PING_TIMEOUT_MS
#define MB_PING_TIMEOUT_MS (600)
#endif

static const char *TAG = "MODBUS_MASTER";

/* Mutex para serializar o acesso ao barramento (drivers concorrentes, endpoints, etc.) */
static SemaphoreHandle_t s_mb_req_mutex = NULL;

/* Handler do master (mantido internamente pela esp-modbus) — não é necessário guardar globalmente,
   mas deixamos explícito para facilitar depuração futura. */
static void *s_master_handler = NULL;
static bool   s_master_ready  = false;

/* Helper interno: send_request com lock */
static inline esp_err_t mb_send_locked(mb_param_request_t *req,
                                       void *data_buf,
                                       TickType_t tmo_ticks)
{
    if (!req || !data_buf || req->reg_size == 0) return ESP_ERR_INVALID_ARG;
    if (s_mb_req_mutex) xSemaphoreTake(s_mb_req_mutex, tmo_ticks);
    esp_err_t err = mbc_master_send_request(req, data_buf);
    if (s_mb_req_mutex) xSemaphoreGive(s_mb_req_mutex);
    return err;
}

/* =================== Inicialização do Master RTU =================== */
// em modbus_rtu_master.c — SUBSTITUA a função inteira
esp_err_t modbus_master_init(void)
{
    // ✅ idempotência: se já inicializado, sai cedo
    if (s_master_ready) {
        ESP_LOGI(TAG, "Master RTU já inicializado (noop).");
        return ESP_OK;
    }
    if (s_master_handler != NULL) {        // se, por algum motivo, já tiver handler
        s_master_ready = true;
        ESP_LOGI(TAG, "Master RTU já possui handler (noop).");
        return ESP_OK;
    }

    mb_communication_info_t comm = {
        .port     = MB_PORT_NUM,
    #ifdef CONFIG_MB_COMM_MODE_ASCII
        .mode     = MB_MODE_ASCII,
    #else
        .mode     = MB_MODE_RTU,
    #endif
        .baudrate = MB_DEV_SPEED,
        .parity   = MB_PARITY_NONE
    };

    ESP_LOGI(TAG, "Init Master RTU: UART%d, %d bps, mode=%s",
             MB_PORT_NUM, MB_DEV_SPEED, (comm.mode == MB_MODE_RTU ? "RTU" : "ASCII"));

    esp_err_t err = mbc_master_init(MB_PORT_SERIAL_MASTER, &s_master_handler);
    if (err != ESP_OK || s_master_handler == NULL) {
        ESP_LOGE(TAG, "mbc_master_init() fail: %s (handler=%p)", esp_err_to_name(err), s_master_handler);
        return (err == ESP_OK) ? ESP_ERR_INVALID_STATE : err;
    }

    err = mbc_master_setup((void *)&comm);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_master_setup() fail: %s", esp_err_to_name(err));
        return err;
    }

    // Ajustes UART/RS485
    err = uart_set_rx_timeout(MB_PORT_NUM, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_rx_timeout() fail: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "UART pins: TX=%d RX=%d RTS(DE)=%d",
             CONFIG_MB_UART_TXD, CONFIG_MB_UART_RXD, CONFIG_MB_UART_RTS);

    err = uart_set_pin(MB_PORT_NUM, CONFIG_MB_UART_TXD, CONFIG_MB_UART_RXD,
                       CONFIG_MB_UART_RTS, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin() fail: %s", esp_err_to_name(err));
        return err;
    }

    err = mbc_master_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_master_start() fail: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_set_mode(MB_PORT_NUM, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_mode(RS485_HALF_DUPLEX) fail: %s", esp_err_to_name(err));
        return err;
    }

    if (!s_mb_req_mutex) {
        s_mb_req_mutex = xSemaphoreCreateMutex();
        if (!s_mb_req_mutex) {
            ESP_LOGE(TAG, "xSemaphoreCreateMutex() NO MEM");
            return ESP_ERR_NO_MEM;
        }
    }

    s_master_ready = true;
    ESP_LOGI(TAG, "Master RTU inicializado e pronto.");
    return ESP_OK;
}


/* =================== Tarefa leitora interna (DESABILITADA) ===================
 * Para manter o módulo 100% desacoplado de "slaves", não criamos nenhuma task aqui.
 * Caso um dia queira reativar uma leitura cíclica interna, substitua esta função.
 */
esp_err_t modbus_master_start_task(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

/* =================== Wrappers de leitura para drivers =================== */
esp_err_t modbus_master_read_input_registers(uint8_t slave_addr,
                                             uint16_t reg_start,
                                             uint16_t words,
                                             uint16_t *out_words)
{
    if (!s_master_ready) return ESP_ERR_INVALID_STATE;
    if (slave_addr == 0 || words == 0 || out_words == NULL) return ESP_ERR_INVALID_ARG;

    mb_param_request_t req = {
        .slave_addr = slave_addr,
        .command    = 0x04,          // Input Registers
        .reg_start  = reg_start,
        .reg_size   = words
    };
    return mb_send_locked(&req, out_words, pdMS_TO_TICKS(MB_REQ_TIMEOUT_MS));
}

esp_err_t modbus_master_read_holding_registers(uint8_t slave_addr,
                                               uint16_t reg_start,
                                               uint16_t words,
                                               uint16_t *out_words)
{
    if (!s_master_ready) return ESP_ERR_INVALID_STATE;
    if (slave_addr == 0 || words == 0 || out_words == NULL) return ESP_ERR_INVALID_ARG;

    mb_param_request_t req = {
        .slave_addr = slave_addr,
        .command    = 0x03,          // Holding Registers
        .reg_start  = reg_start,
        .reg_size   = words
    };
    return mb_send_locked(&req, out_words, pdMS_TO_TICKS(MB_REQ_TIMEOUT_MS));
}

/* =================== Ping canônico =================== */
esp_err_t modbus_master_ping(uint8_t slave_addr, bool *alive, uint8_t *used_fc)
{
    if (!s_master_ready) return ESP_ERR_INVALID_STATE;
    if (alive)   *alive = false;
    if (used_fc) *used_fc = 0x00;
    if (slave_addr == 0) return ESP_ERR_INVALID_ARG;

    uint16_t rx = 0;

    /* 1) FC=0x04 (Input) @ 0x0001 — muitos sensores (p.ex. XY-MD02) respondem aqui */
    mb_param_request_t req = {
        .slave_addr = slave_addr,
        .command    = 0x04,
        .reg_start  = 0x0001,
        .reg_size   = 1
    };
    esp_err_t err = mb_send_locked(&req, &rx, pdMS_TO_TICKS(MB_PING_TIMEOUT_MS));
    if (err == ESP_OK) {
        if (alive)   *alive   = true;
        if (used_fc) *used_fc = 0x04;
        return ESP_OK;
    }

    /* 2) Fallback: FC=0x03 (Holding) @ 0x0000 */
    req.command   = 0x03;
    req.reg_start = 0x0000;
    err = mb_send_locked(&req, &rx, pdMS_TO_TICKS(MB_PING_TIMEOUT_MS));
    if (err == ESP_OK) {
        if (alive)   *alive   = true;
        if (used_fc) *used_fc = 0x03;
        return ESP_OK;
    }

    return err;  // tipicamente ESP_ERR_TIMEOUT quando não há resposta
}

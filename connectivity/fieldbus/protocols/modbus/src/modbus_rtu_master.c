/*
 * modbus_rtu_master.c
 * Master RTU (esp-modbus) desacoplado de qualquer "slave".
 * - init/start (sem tarefa leitora interna)
 * - wrappers de leitura (FC04/FC03) usados pelos drivers
 * - ping canônico (FC04@0x0001 → FC03@0x0000)
 */

#include "modbus_rtu_master.h"
#include "driver/uart.h"
#include "rs485_hw.h" 
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "mbcontroller.h"
#include "log_mux.h"

/* Mapear os símbolos do Modbus para os do RS485 (com fallback seguro) */
/*#ifndef MB_PORT_NUM
#  ifdef RS485_UART_NUM
#    define MB_PORT_NUM        RS485_UART_NUM
#  else
#    define MB_PORT_NUM        0
#  endif
#endif*/
#define MB_PORT_NUM RS485_UART_NUM

#ifndef MB_DEV_SPEED
#  ifdef RS485_DEFAULT_BAUD
#    define MB_DEV_SPEED       RS485_DEFAULT_BAUD
#  else
#    define MB_DEV_SPEED       9600
#  endif
#endif

/*#ifndef CONFIG_MB_UART_TXD
#  ifdef RS485_TX_PIN
#    define CONFIG_MB_UART_TXD RS485_TX_PIN
#  else
#    define CONFIG_MB_UART_TXD 1
#  endif
#endif*/

/*#ifndef CONFIG_MB_UART_RXD
#  ifdef RS485_RX_PIN
#    define CONFIG_MB_UART_RXD RS485_RX_PIN
#  else
#    define CONFIG_MB_UART_RXD 3
#  endif
#endif

#ifndef CONFIG_MB_UART_RTS
#  ifdef RS485_DE_RE_PIN
#    define CONFIG_MB_UART_RTS RS485_DE_RE_PIN   // RTS usado como DE/RE no half-duplex
#  else
#    define CONFIG_MB_UART_RTS 18
#  endif
#endif*/

/* Timeouts internos padrão (ticks) */
#ifndef MB_REQ_TIMEOUT_MS
#define MB_REQ_TIMEOUT_MS  (800)
#endif
#ifndef MB_PING_TIMEOUT_MS
#define MB_PING_TIMEOUT_MS (300)
#endif

static const char *TAG = "MODBUS_MASTER";

/* Mutex para serializar o acesso ao barramento (drivers concorrentes, endpoints, etc.) */
static SemaphoreHandle_t s_mb_req_mutex = NULL;

/* Handler do master (mantido internamente pela esp-modbus) — não é necessário guardar globalmente,
   mas deixamos explícito para facilitar depuração futura. */
static void *s_master_handler = NULL;
static bool   s_master_ready  = false;
static uart_mode_t s_last_uart_mode = UART_MODE_UART;
void rs485_note_set_mode(uart_mode_t m) { s_last_uart_mode = m; }

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

static void modbus_uart_selftest(void) {
    uint32_t br = 0;
    esp_err_t e1 = uart_get_baudrate(RS485_UART_NUM, &br);

    const char *mode_str =
        (s_last_uart_mode == UART_MODE_RS485_HALF_DUPLEX) ? "RS485_HALF_DUPLEX" :
        (s_last_uart_mode == UART_MODE_UART)              ? "UART" : "UNKNOWN";

    ESP_LOGI("UART-DIAG",
             "PORT=%d  BAUD=%u (%s)  MODE=%s  Pins: TX=%d RX=%d DE/RE(RTS)=%d",
             RS485_UART_NUM,
             (unsigned)br, (e1==ESP_OK ? "ok" : "err"),
             mode_str,
             RS485_TX_PIN, RS485_RX_PIN, RS485_DE_RE_PIN);

    // --- Ping conforme seu header (3 argumentos) ---
    bool alive = false;
    uint8_t used_fc = 0;
    esp_err_t pe = modbus_master_ping(/*slave*/1, &alive, &used_fc);

    ESP_LOGI("UART-DIAG", "PING addr=1 -> esp=%s  alive=%d  used_fc=0x%02X",
             esp_err_to_name(pe), (int)alive, used_fc);
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
             RS485_TX_PIN, RS485_RX_PIN, RS485_DE_RE_PIN);

   err = uart_set_pin(MB_PORT_NUM, RS485_TX_PIN, RS485_RX_PIN,
                     RS485_DE_RE_PIN, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin() fail: %s", esp_err_to_name(err));
        return err;
    }

    err = mbc_master_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_master_start() fail: %s", esp_err_to_name(err));
        return err;
    }

#if (MB_PORT_NUM == UART_NUM_0)
    // UART0 não tem RS485 HW; mantenha modo UART
    err = uart_set_mode(MB_PORT_NUM, UART_MODE_UART);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_mode(UART) fail: %s", esp_err_to_name(err));
        return err;
    }
#else
    err = uart_set_mode(MB_PORT_NUM, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_mode(RS485_HALF_DUPLEX) fail: %s", esp_err_to_name(err));
        return err;
    }
#endif

    logmux_notify_rs485_active((uart_port_t)MB_PORT_NUM, true);
  
if (!s_mb_req_mutex) {
    s_mb_req_mutex = xSemaphoreCreateMutex();
    if (!s_mb_req_mutex) {
        ESP_LOGE(TAG, "xSemaphoreCreateMutex() NO MEM");
        // rollback seguro:
        logmux_notify_rs485_active((uart_port_t)MB_PORT_NUM, false);
        (void) mbc_master_destroy();
        uart_flush_input(MB_PORT_NUM);
        uart_set_mode(MB_PORT_NUM, UART_MODE_UART);
        s_master_handler = NULL;
        return ESP_ERR_NO_MEM;
    }
}
    s_master_ready = true;
    ESP_LOGI(TAG, "Master RTU inicializado e pronto.");
    
    modbus_uart_selftest();
    return ESP_OK;
}

// modbus_rtu_master.c

// modbus_rtu_master.c
esp_err_t modbus_master_deinit(void)
{
    if (!s_master_ready && s_master_handler == NULL) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinit Master RTU.");

    // 1) Bloqueia o barramento e impede novas requisições
    if (s_mb_req_mutex) {
        xSemaphoreTake(s_mb_req_mutex, portMAX_DELAY);
    }
    s_master_ready = false;  // wrappers passam a retornar INVALID_STATE


    // 2) Devolve UART para modo normal e limpa buffers
    uart_flush_input(MB_PORT_NUM);
    uart_flush(MB_PORT_NUM);
    #if (MB_PORT_NUM != UART_NUM_0)
    uart_set_mode(MB_PORT_NUM, UART_MODE_UART);
    #endif
    
     // 3) Para e destrói o master esp-modbus
    esp_err_t err = mbc_master_destroy();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mbc_master_destroy() = %s", esp_err_to_name(err));
    }
    s_master_handler = NULL;
    
    // 4) Libera e destrói o mutex
    if (s_mb_req_mutex) {
        xSemaphoreGive(s_mb_req_mutex);
        vSemaphoreDelete(s_mb_req_mutex);
        s_mb_req_mutex = NULL;
    }

    // 5) Informa ao log_mux que RS485 não está mais ativo
    logmux_notify_rs485_active((uart_port_t)MB_PORT_NUM, false);

    ESP_LOGI(TAG, "Master RTU desinicializado.");
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
// Ping genérico Modbus RTU (read-only).
// - Tenta FC03/FC04 em 0x0000/0x0001.
// - Usa dica (hint) por endereço para tentar primeiro a FC que respondeu da última vez.
// - Faz auto-reinit se a controladora estiver em INVALID_STATE.
// Retorna ESP_OK quando obteve resposta; *alive=true e *used_fc com a FC usada.
// Em erro (ex.: timeout), retorna o último esp_err_t.
esp_err_t modbus_master_ping(uint8_t slave_addr, bool *alive, uint8_t *used_fc)
{
    if (alive)   *alive = false;
    if (used_fc) *used_fc = 0x00;
    if (slave_addr == 0 || slave_addr > 247) return ESP_ERR_INVALID_ARG;

    // Garante master UP (idempotente)
    if (!s_master_ready) {
        (void) modbus_master_init();
        if (!s_master_ready) return ESP_ERR_INVALID_STATE;
    }

    // Dica simples por endereço: 0 = sem dica; 3/4 = última FC OK.
    static uint8_t s_ping_fc_hint[248]; // indexado por endereço (1..247), zera em BSS

    uint16_t rx = 0;
    mb_param_request_t req = {
        .slave_addr = slave_addr,
        .command    = 0x03,    // ajustado a cada tentativa
        .reg_start  = 0x0000,  // ajustado a cada tentativa
        .reg_size   = 1
    };

    esp_err_t last_err = ESP_FAIL;

    // ---- 1) Tenta HINT primeiro (se houver), em offsets comuns 0x0000 e 0x0001
    uint8_t hint = s_ping_fc_hint[slave_addr];
    if (hint == 0x03 || hint == 0x04) {
        const uint16_t hint_regs[] = { 0x0000, 0x0001 };
        for (size_t i = 0; i < sizeof(hint_regs)/sizeof(hint_regs[0]); ++i) {
            req.command   = hint;
            req.reg_start = hint_regs[i];

            esp_err_t err = mb_send_locked(&req, &rx, pdMS_TO_TICKS(MB_PING_TIMEOUT_MS));
            if (err == ESP_ERR_INVALID_STATE) {
                (void) modbus_master_init();
                err = mb_send_locked(&req, &rx, pdMS_TO_TICKS(MB_PING_TIMEOUT_MS));
            }
            if (err == ESP_OK) {
                if (alive)   *alive   = true;
                if (used_fc) *used_fc = req.command;
                // reforça a dica
                s_ping_fc_hint[slave_addr] = req.command;
                return ESP_OK;
            }
            last_err = err;
        }
    }

    // ---- 2) Sequência padrão (cobre maioria dos dispositivos)
    // 1) FC03@0x0000  2) FC04@0x0000  3) FC04@0x0001  4) FC03@0x0001
    const struct { uint8_t fc; uint16_t reg; } tries[] = {
        { 0x03, 0x0000 },
        { 0x04, 0x0000 },
        { 0x04, 0x0001 },
        { 0x03, 0x0001 },
    };

    for (size_t i = 0; i < sizeof(tries)/sizeof(tries[0]); ++i) {
        // Se já tentamos a mesma FC por causa do hint para este offset, seguimos.
        req.command   = tries[i].fc;
        req.reg_start = tries[i].reg;

        esp_err_t err = mb_send_locked(&req, &rx, pdMS_TO_TICKS(MB_PING_TIMEOUT_MS));
        if (err == ESP_ERR_INVALID_STATE) {
            (void) modbus_master_init();
            err = mb_send_locked(&req, &rx, pdMS_TO_TICKS(MB_PING_TIMEOUT_MS));
        }

        if (err == ESP_OK) {
            if (alive)   *alive   = true;
            if (used_fc) *used_fc = req.command;
            s_ping_fc_hint[slave_addr] = req.command; // guarda dica para os próximos pings
            return ESP_OK;
        }

        last_err = err;
    }

    // Ninguém respondeu
    return last_err; // tipicamente ESP_ERR_TIMEOUT
}


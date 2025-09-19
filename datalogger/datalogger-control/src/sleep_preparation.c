/*
 * sleep_preparation.c
 *
 * Epílogo comum antes de dormir / sair do modo ativo.
 * - RS-485 em idle + flush (se disponível)
 * - Deinit do Modbus (se habilitado)
 * - Desabilita console TCP (se disponível)
 * - Opcionalmente para o Wi-Fi (AP/STA)
 * - Pequenos delays para drenar buffers
 */

#include "sleep_preparation.h"
#include "ifdef_features.h"
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"

// ---- Wi-Fi / UART (fixos no IDF)
#include "esp_wifi.h"
#include "driver/uart.h"

// ---- Detecção opcional de RS-485 e Guard Modbus
#if __has_include("rs485_hw.h")
  #include "rs485_hw.h"
  #define HAVE_RS485_HW 0
#else
  #define HAVE_RS485_HW 0
#endif

#if __has_include("modbus_guard_session.h")
  #include "modbus_guard_session.h"
//  #define HAVE_MODBUS_GUARD 1
#else
  #define HAVE_MODBUS_GUARD 0
#endif

// ---- Dependências opcionais (weak) — não quebram o build se não existirem
extern void console_tcp_disable(void) __attribute__((weak));
extern void stop_wifi_ap_sta(void)     __attribute__((weak));

static const char *TAG = "SLEEP_PREP";

// Guard de reentrância/idempotência do epílogo
static bool s_sleep_prep_done = false;
static portMUX_TYPE s_sleep_prep_mux = portMUX_INITIALIZER_UNLOCKED;

// (opcionais, se existirem no seu guard de Modbus; se não, são NULL e pulamos)
bool modbus_guard_block_new_sessions(TickType_t max_wait) __attribute__((weak));
bool modbus_guard_wait_all_sessions_end(TickType_t max_wait) __attribute__((weak));
// ----------------- Helpers -----------------

typedef struct {
    bool wifi_started;   // driver iniciado
    bool sta_enabled;    // modo inclui STA
    bool ap_enabled;     // modo inclui AP
    bool sta_connected;  // STA associado a um AP
} wifi_state_t;

static inline wifi_state_t wifi_get_state(void)
{
    wifi_state_t s = {0};
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        s.wifi_started = true;
        s.sta_enabled  = (mode & WIFI_MODE_STA) != 0;
        s.ap_enabled   = (mode & WIFI_MODE_AP)  != 0;

        wifi_ap_record_t ap;
        s.sta_connected = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
    }
    return s;
}

bool sta_is_connected(void)
{
    wifi_ap_record_t ap;
    return (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
}

static inline void flush_console_uart0_safe(void)
{
#if ESP_IDF_VERSION_MAJOR >= 5
    if (uart_is_driver_installed(UART_NUM_0)) {
        (void)uart_flush(UART_NUM_0);
        (void)uart_wait_tx_done(UART_NUM_0, 10 / portTICK_PERIOD_MS);
    } else {
        fflush(stdout);
    }
#else
    (void)uart_flush(UART_NUM_0);
    (void)uart_wait_tx_done(UART_NUM_0, 10 / portTICK_PERIOD_MS);
#endif
}

// ----------------- API -----------------

void sleep_prepare(bool maybe_stop_wifi)
{
    // ========== 0) Idempotente e à prova de reentrância ==========
    portENTER_CRITICAL(&s_sleep_prep_mux);
    if (s_sleep_prep_done) {            // já executou antes → sai
        portEXIT_CRITICAL(&s_sleep_prep_mux);
        return;
    }
    s_sleep_prep_done = true;
    portEXIT_CRITICAL(&s_sleep_prep_mux);

    // ========== 1) RS-485: flush e barramento em idle ==========
#if HAVE_RS485_HW
    rs485_hw_flush();        // drena UART do 485 (sua impl. já é segura)
    rs485_hw_set_idle_rx();  // DE/RE = 0 → barramento quieto
#endif

    // ========== 2) Modbus: quiesce (se guard existir) + deinit ==========
#if CONFIG_MODBUS_GUARD_ENABLE && CONFIG_MODBUS_ENABLE
    // Bloqueia novas sessões (se a API existir)
    if (modbus_guard_block_new_sessions) {
        (void)modbus_guard_block_new_sessions(pdMS_TO_TICKS(100));
    }
    // Aguarda sessões correntes encerrarem (se a API existir)
    if (modbus_guard_wait_all_sessions_end) {
        (void)modbus_guard_wait_all_sessions_end(pdMS_TO_TICKS(300));
    }
    // Deinit do master (tolerante a “já deinitado”)
    (void)modbus_master_deinit();
#elif CONFIG_MODBUS_ENABLE
    // Sem guard, apenas deinit (pode retornar INVALID_STATE se já estiver fechado)
    (void)modbus_master_deinit();
#endif

    // ========== 3) Console TCP off → logs de volta à UART ==========
    if (console_tcp_disable) {
        console_tcp_disable();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    flush_console_uart0_safe();

    // ========== 4) Parar Wi-Fi se solicitado ==========
    if (maybe_stop_wifi) {
        wifi_state_t ws = wifi_get_state();
        if (ws.wifi_started) {
            if (stop_wifi_ap_sta) {
                // seu wrapper (para AP+STA) se existir
                stop_wifi_ap_sta();
            } else {
                if (ws.sta_connected) (void)esp_wifi_disconnect();
                (void)esp_wifi_stop();   // ok chamar mesmo se já estiver parado
            }
            ESP_LOGI(TAG, "WiFi parado (started=%d, sta=%d, ap=%d, conn=%d)",
                     ws.wifi_started, ws.sta_enabled, ws.ap_enabled, ws.sta_connected);
        } else {
            ESP_LOGI(TAG, "WiFi já estava parado.");
        }
    }

    // ========== 5) Folga p/ drenar pilhas ==========
    vTaskDelay(pdMS_TO_TICKS(80));
}


/*
 * reboot_test.c
 *
 *  Created on: 17 de set. de 2025
 *      Author: geopo
 */

#include "reboot_test.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "REBOOT_TEST";
static reboot_mode_t s_mode;
static esp_timer_handle_t s_timer;

static const char* reason_str(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXT_PIN";
        case ESP_RST_SW:        return "SW";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "OTHER_WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "UNKNOWN";
    }
}

static inline void safe_timer_stop(esp_timer_handle_t t)
{
    if (!t) return;
    // Se a sua versão tiver esp_timer_is_active(), pode usar para evitar a chamada:
    // if (!esp_timer_is_active(t)) return;

    esp_err_t e = esp_timer_stop(t);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        // INVALID_STATE = timer já não estava ativo → OK ignorar
        ESP_LOGW(TAG, "esp_timer_stop() = %s", esp_err_to_name(e));
    }
}

void log_reset_reason_on_boot(void) {
    esp_reset_reason_t r = esp_reset_reason();
    ESP_LOGW(TAG, "Last reset reason: %d (%s)", (int)r, reason_str(r));
}

static void do_reboot_cb(void *arg) {
    switch (s_mode) {
        case REBOOT_SOFT:
            ESP_LOGW(TAG, "Trigger: SOFT restart (esp_restart)");
            vTaskDelay(pdMS_TO_TICKS(20));
            esp_restart();
            break;

        case REBOOT_PANIC:
            ESP_LOGW(TAG, "Trigger: PANIC (abort)");
            vTaskDelay(pdMS_TO_TICKS(20));
            abort();
            break;

        case REBOOT_WDT: {
            ESP_LOGW(TAG, "Trigger: TASK WDT (1s)… travando task atual.");

            // API nova (ESP-IDF v5.x): usa struct de config
            esp_task_wdt_config_t cfg = {
                .timeout_ms     = 1000,   // 1s
                .idle_core_mask = 0,      // não vigiar tasks Idle
                .trigger_panic  = true    // ao estourar, gera panic+reset
            };
            esp_err_t e = esp_task_wdt_init(&cfg);
            if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "esp_task_wdt_init: %s", esp_err_to_name(e));
            }
            // vigiar a task atual e NUNCA alimentar
            esp_task_wdt_add(NULL);

            // trava aqui até o WDT resetar
            while (true) {
                // sem esp_task_wdt_reset(); propositalmente
                // (opcional: um yield curto só pra sair o log)
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        } break;
    }
}

esp_err_t test_trigger_reboot(reboot_mode_t mode, uint32_t delay_ms) {
    s_mode = mode;
    if (!s_timer) {
        const esp_timer_create_args_t args = {
            .callback = &do_reboot_cb,
            .name = "rbtest",
            .dispatch_method = ESP_TIMER_TASK
        };
        ESP_ERROR_CHECK(esp_timer_create(&args, &s_timer));
    }
    ESP_LOGI(TAG, "Will trigger %d in %u ms", (int)mode, (unsigned)delay_ms);
    //ESP_ERROR_CHECK(esp_timer_stop(s_timer));
    safe_timer_stop(s_timer);  
    return esp_timer_start_once(s_timer, (uint64_t)delay_ms * 1000ULL);
}




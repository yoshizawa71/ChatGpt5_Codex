/*
 * wifi_diag.c
 *
 *  Created on: 15 de set. de 2025
 *      Author: geopo
 */

// wifi_diag.c (ou dentro do seu wifi_softap_sta.c)
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"

static const char *TAG = "WIFI/DIAG";

// Se quiser piscar um LED aqui, defina o GPIO (ou deixe -1 para não usar)
#ifndef DIAG_LED_GPIO
#define DIAG_LED_GPIO (-1)
#endif

#define HB_PERIOD_MS 60000
#define HB_TICK_MS   1000

static TaskHandle_t s_heartbeat = NULL;
static bool s_ap_running = false;

static inline uint64_t ms_since_boot(void){
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

static void log_wifi_summary(const char *prefix)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);

    uint8_t ch = 0; wifi_second_chan_t s = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&ch, &s);

    wifi_config_t apcfg = {0};
    esp_wifi_get_config(WIFI_IF_AP, &apcfg);

    wifi_sta_list_t stas = {0};
    esp_wifi_ap_get_sta_list(&stas);

    size_t free8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t min8  = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);

    ESP_LOGW(TAG,
        "%s mode=%d ap_running=%s ssid='%s' ch=%u assoc=%d heap=%u(min=%u) uptime=%llum",
        prefix ? prefix : "",
        (int)mode,
        s_ap_running ? "yes" : "no",
        (char*)apcfg.ap.ssid,
        ch,
        (int)stas.num,
        (unsigned)free8, (unsigned)min8,
        (unsigned long long)(ms_since_boot()/60000ULL));
}

static void wifi_event_logger(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case WIFI_EVENT_WIFI_READY:
        ESP_LOGI(TAG, "WIFI_READY");
        break;

    case WIFI_EVENT_AP_START:
        s_ap_running = true;
        // Se AP ativo, desligue PS (economia de energia) para estabilidade
        esp_wifi_set_ps(WIFI_PS_NONE);
        log_wifi_summary("AP_START");
        break;

    case WIFI_EVENT_AP_STOP:
        s_ap_running = false;
        log_wifi_summary("AP_STOP");
        break;

    case WIFI_EVENT_AP_STACONNECTED: {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "AP_STACONNECTED mac=%02x:%02x:%02x:%02x:%02x:%02x aid=%d",
                 e->mac[0],e->mac[1],e->mac[2],e->mac[3],e->mac[4],e->mac[5], e->aid);
        break;
    }
    case WIFI_EVENT_AP_STADISCONNECTED: {
        wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)data;
        ESP_LOGW(TAG, "AP_STADISCONNECTED mac=%02x:%02x:%02x:%02x:%02x:%02x aid=%d",
                 e->mac[0],e->mac[1],e->mac[2],e->mac[3],e->mac[4],e->mac[5], e->aid);
        break;
    }

    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "STA_CONNECTED");
        break;

    case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *e = (wifi_event_sta_disconnected_t *)data;
        // Motivo útil pra diagnosticar quedas do STA (aponta se AP+STA está mudando de canal)
        ESP_LOGW(TAG, "STA_DISCONNECTED reason=%d", e->reason);
        break;
    }

    default:
        // outros eventos também aparecem, mas os principais acima já contam a história
        break;
    }
}

static void ip_event_logger(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    // útil para ver se a pilha IP reinicializou
    ESP_LOGI(TAG, "IP_EVENT id=%" PRId32, id);
}

static void heartbeat_task(void *arg)
{
#if CONFIG_ESP_TASK_WDT_INIT
    // Registra no Task Watchdog para detectar travamentos
    esp_task_wdt_add(NULL);
#endif

#if DIAG_LED_GPIO >= 0
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << DIAG_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = 0, .pull_up_en = 0, .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io);
#endif

 uint32_t elapsed = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(HB_TICK_MS));

        // reset no WDT com frequência menor que o timeout (5s)
#if CONFIG_ESP_TASK_WDT_INIT
        esp_task_wdt_reset();
#endif

        elapsed += HB_TICK_MS;
        if (elapsed >= HB_PERIOD_MS) {
            log_wifi_summary("HEARTBEAT");
#if DIAG_LED_GPIO >= 0
            // pisca aqui também se quiser
#endif
            elapsed = 0;
        }
    }
}

// Chame isto depois do esp_wifi_init() e antes/depois de startar o Wi-Fi.
void wifi_diag_install(void)
{
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_logger, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                                        &ip_event_logger,   NULL, NULL));
    if (!s_heartbeat) {
        xTaskCreatePinnedToCore(heartbeat_task, "wifi_diag_hb", 3072, NULL, 3, &s_heartbeat, tskNO_AFFINITY);
    }
    ESP_LOGI(TAG, "Wi-Fi diag instalado");
}




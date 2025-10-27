/*
 * wifi_link.c
 *
 *  Created on: 7 de out. de 2025
 *      Author: geopo
 */

#include "wifi_link.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include <string.h>
#include <lwip/ip4_addr.h>
#include <lwip/netdb.h>
#include "wifi_softap_sta.h"   // seu start_wifi_ap_sta(), wifi_ap_force_disable()
#include "esp_log.h"

static const char *TAG = "wifi_link";

bool wifi_link_has_ip(void) {
    esp_netif_ip_info_t ip;
    memset(&ip, 0, sizeof(ip));
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    return (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0);
}

esp_err_t wifi_link_ensure_ready_sta(int timeout_ms, bool force_sta_only)
{
    wifi_mode_t m = WIFI_MODE_NULL;
    esp_wifi_get_mode(&m);

    if (m != WIFI_MODE_STA && m != WIFI_MODE_APSTA) {
        // event loop / netif podem existir de boots anteriores; INVALID_STATE é OK
        esp_err_t err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

        err = esp_netif_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

        if (start_wifi_ap_sta() != ESP_OK) {
            ESP_LOGW(TAG, "Falha ao iniciar WiFi (AP+STA).");
            return ESP_FAIL;
        }
    }
    if (force_sta_only) {
        wifi_ap_force_disable(); // garante STA-only em headless
    }

    // Espera por IP
    int waited = 0;
    const int step_ms = 200;
    while (!wifi_link_has_ip()) {
        if (waited >= timeout_ms) {
            ESP_LOGW(TAG, "Timeout aguardando GOT_IP.");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        waited += step_ms;
    }
    return ESP_OK;
}

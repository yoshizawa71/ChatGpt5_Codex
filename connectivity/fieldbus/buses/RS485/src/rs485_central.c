#include "rs485_central.h"
#include "esp_err.h"
#include "esp_log.h"

#include "energy_meter.h"

static const char *TAG = "RS485_CENTRAL";

#ifndef RS485_CENTRAL_FIX_CHANNEL
#define RS485_CENTRAL_FIX_CHANNEL   3
#endif
#ifndef RS485_CENTRAL_FIX_ADDRESS
#define RS485_CENTRAL_FIX_ADDRESS   1
#endif

void rs485_central_poll_and_save(uint32_t timeout_ms)
{
    (void)timeout_ms;

    esp_err_t err = energy_meter_save_currents(RS485_CENTRAL_FIX_CHANNEL,
                                               RS485_CENTRAL_FIX_ADDRESS);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Central: lido 1 sensor energia; gravado %d", 3);
    } else {
        ESP_LOGW(TAG, "Central: falha ao gravar energia (ch=%d addr=%d): %s",
                 RS485_CENTRAL_FIX_CHANNEL, RS485_CENTRAL_FIX_ADDRESS, esp_err_to_name(err));
    }

    // TODO(Codex): Fase 2 — substituir lista fixa por snapshot do registry e despacho via tabela.
}

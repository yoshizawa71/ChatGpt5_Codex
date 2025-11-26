// rs485_central.c
#include "rs485_central.h"

#include "esp_err.h"
#include "esp_log.h"

#include "energy_meter.h"   // energy_meter_save_registered_currents()

static const char *TAG = "RS485_CENTRAL";

/**
 * Centraliza a leitura de TODOS os sensores RS-485 de energia cadastrados
 * (aqueles que estão no arquivo de configuração RS485, via front).
 *
 * A lista de canais/endereços vem do adaptador:
 *   rs485_registry_iterate_configured()  (em rs485_registry_adapter.c)
 *
 * Para cada item, o energy_meter grava os valores no SD usando
 * save_record_sd_rs485() no formato "canal" / "canal.subcanal".
 */
void rs485_central_poll_and_save(uint32_t timeout_ms)
{
    // Por enquanto não usamos o timeout; deixei para futura expansão
    (void) timeout_ms;

    esp_err_t err = energy_meter_save_registered_currents();

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Central: leituras RS485 salvas com sucesso.");
    } else if (err == ESP_ERR_NOT_FOUND) {
        // Ninguém cadastrado (ou iterador ainda é a versão WEAK)
        ESP_LOGW(TAG, "Central: nenhum medidor RS485 cadastrado.");
    } else {
        ESP_LOGW(TAG, "Central: falha ao salvar leituras RS485: %s",
                 esp_err_to_name(err));
    }
}

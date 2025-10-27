/*
 * rs485_central.c
 *
 *  Created on: 25 de out. de 2025
 *      Author: geopo
 */

 #include "rs485_central.h"
 #include "rs485_registry.h"     // rs485_registry_read_all, rs485_measurement_t
 #include "rs485_sd_adapter.h"   // save_measurements_to_sd
 #include "esp_log.h"
 #include "modbus_rtu_master.h"

 #ifndef RS485_MAX_MEAS
 #define RS485_MAX_MEAS  64
 #endif

 static const char *TAG = "RS485_CENTRAL";

 // ======================================================================
// Central poll + persist: varre todos os sensores cadastrados e grava no SD
// Mantém exclusividade do barramento APENAS durante a leitura.
// Libera o guard ANTES de gravar no SD para não segurar o RS-485.
// ======================================================================
esp_err_t rs485_central_poll_and_save(int timeout_ms)
{
    (void)timeout_ms; // TODO: encadear timeout para rs485_poll_all quando suportado

    rs485_sensor_t      list[RS485_MAX_SENSORS];
    rs485_measurement_t meas[RS485_MAX_MEAS];

    // 1) snapshot do cadastro
    int count = rs485_registry_get_snapshot(list, RS485_MAX_SENSORS);
    if (count < 0) {
        ESP_LOGW(TAG, "rs485_registry_get_snapshot falhou (err=%d)", count);
        return ESP_FAIL;
    }
    if (count == 0) {
        ESP_LOGI(TAG, "Nenhum sensor RS485 cadastrado.");
        return ESP_ERR_NOT_FOUND;
    }

    // 2) exclusividade do barramento durante a varredura
    modbus_guard_t g = (modbus_guard_t){0};
    bool guard_locked = false;
    if (!modbus_guard_try_begin(&g, 120)) {
        ESP_LOGW("RS485_CENTRAL", "Barramento ocupado; adiando rodada.");
        return ESP_ERR_TIMEOUT;
    }
    guard_locked = true;

    esp_err_t ret = ESP_OK;

    // 3) dispatcher: lê todos e preenche vetor heterogêneo
    int n = rs485_poll_all(list, (size_t)count, meas, RS485_MAX_MEAS);
    if (n < 0) {
        ESP_LOGW(TAG, "rs485_poll_all falhou (err=%d)", n);
        ret = ESP_FAIL;
        goto out;
    }
    if (n == 0) {
        ESP_LOGI(TAG, "Nenhum sensor RS485 legível no momento.");
        ret = ESP_ERR_NOT_FOUND;
        goto out;
    }

    // 4) libera o barramento ANTES de gravar no SD
    modbus_guard_end(&g);
    guard_locked = false;

    int w = save_measurements_to_sd(meas, (size_t)n);
    if (w <= 0) {
        ESP_LOGW(TAG, "Nada gravado no SD (w=%d)", w);
        ret = ESP_FAIL;
        goto out;
    }

    ESP_LOGI(TAG, "Centralizado: lidos=%d gravados=%d", n, w);
    ret = ESP_OK;

out:
    if (guard_locked) {
        modbus_guard_end(&g);
    }
    return ret;
}

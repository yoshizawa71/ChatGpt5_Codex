/*
 * pressure_calibrate.c
 *
 *  Created on: 19 de mai. de 2025
 *      Author: geopo
 */
//#include "ads1015_reader.h" 
#include "pressure_calibrate.h"
#include <stdio.h>
#include "ads1015_reader.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <math.h> // Adicionado para usar fabs

#define TAG "PRESSURE_CALIBRATE"

#define NUM_CAL_POINTS 5
#define SENSOR_DISCONNECTED_THRESHOLD 0.01f


static const float PSI_CONVERSION = 14.5038f;  // 1 bar = 14.5038 psi
static const float MCA_CONVERSION = 10.1974f;  // 1 bar = 10.1974 mca

#define NVS_NAMESPACE "calib_data"
#define NUM_CAL_POINTS 5

// Interpolação linear segmentada
float interpolate_pressure(float voltage, float *cal_voltages, float *ref_pressures_bar) {
    // Verificar quantos pontos válidos temos (tensão > 0 ou ponto zero específico)
    int valid_points = 0;
    for (int i = 0; i < NUM_CAL_POINTS; i++) {
        if (cal_voltages[i] > 0.0f || (i == 0 && cal_voltages[i] == 0.0f && ref_pressures_bar[i] == 0.0f)) {
            valid_points++;
        }
    }

    if (valid_points == 0) {
        ESP_LOGW(TAG, "[CALIB] Nenhum ponto de calibração válido encontrado.");
        return 0.0f;
    }

    // Se só temos o ponto 0, assumimos que tensões próximas são pressão zero
    if (valid_points == 1 && ref_pressures_bar[0] == 0.0f) {
        if (fabs(voltage - cal_voltages[0]) <= 0.05f) { // Tolerância de ±0.05 V
            return 0.0f;
        } else {
            float Vmax = 4.5f, Pmax = 20.0f;
            for (int i = 1; i < NUM_CAL_POINTS; i++) {
                if (cal_voltages[i] > Vmax && ref_pressures_bar[i] > 0.0f) {
                    Vmax = cal_voltages[i];
                    Pmax = ref_pressures_bar[i];
                }
            }
            float delta_v = Vmax - cal_voltages[0];
            float delta_p = Pmax - ref_pressures_bar[0];
            if (delta_v == 0) {
                ESP_LOGW(TAG, "[CALIB] Delta V zero, retornando pressão base: %.2f bar", ref_pressures_bar[0]);
                return ref_pressures_bar[0];
            }
            float frac = (voltage - cal_voltages[0]) / delta_v;
            float pressure = ref_pressures_bar[0] + frac * delta_p;
            ESP_LOGI(TAG, "[CALIB] Extrapolação com 1 ponto: V=%.3f → P=%.3f bar (Vmax=%.3f, Pmax=%.3f)", 
                     voltage, pressure, Vmax, Pmax);
            return pressure > 0 ? pressure : 0.0f;
        }
    }

    // Ordenar pontos por tensão (bubble sort)
    float sorted_voltages[NUM_CAL_POINTS];
    float sorted_pressures[NUM_CAL_POINTS];
    for (int i = 0; i < NUM_CAL_POINTS; i++) {
        sorted_voltages[i] = cal_voltages[i];
        sorted_pressures[i] = ref_pressures_bar[i];
    }
    for (int i = 0; i < NUM_CAL_POINTS - 1; i++) {
        for (int j = 0; j < NUM_CAL_POINTS - i - 1; j++) {
            if (sorted_voltages[j] > sorted_voltages[j + 1]) {
                float temp_v = sorted_voltages[j];
                float temp_p = sorted_pressures[j];
                sorted_voltages[j] = sorted_voltages[j + 1];
                sorted_pressures[j] = sorted_pressures[j + 1];
                sorted_voltages[j + 1] = temp_v;
                sorted_pressures[j + 1] = temp_p;
            }
        }
    }

    // Interpolação linear entre pontos válidos
    for (int i = 1; i < NUM_CAL_POINTS; i++) {
        if (sorted_voltages[i] == 0.0f && sorted_pressures[i] > 0.0f) continue;

        if (voltage <= sorted_voltages[i] && voltage >= sorted_voltages[i - 1]) {
            float delta_v = sorted_voltages[i] - sorted_voltages[i - 1];
            float delta_p = sorted_pressures[i] - sorted_pressures[i - 1];
            if (delta_v == 0) {
                ESP_LOGW(TAG, "[CALIB] Delta V zero entre pontos %d e %d (V=%.3f), retornando pressão base: %.2f bar", 
                         i-1, i, sorted_voltages[i], sorted_pressures[i]);
                return sorted_pressures[i];
            }
            float frac = (voltage - sorted_voltages[i - 1]) / delta_v;
            float pressure = sorted_pressures[i - 1] + frac * delta_p;
            ESP_LOGI(TAG, "[CALIB] Interpolação: V=%.3f → P=%.3f bar (V%d=%.3f, P%d=%.2f, V%d=%.3f, P%d=%.2f)", 
                     voltage, pressure, i-1, sorted_voltages[i-1], i-1, sorted_pressures[i-1], i, sorted_voltages[i], i, sorted_pressures[i]);
            return pressure;
        }
    }

    // Extrapolar fora da faixa
    int first_valid = 0;
    while (first_valid < NUM_CAL_POINTS && sorted_voltages[first_valid] <= 0.0f) first_valid++;
    if (first_valid >= NUM_CAL_POINTS) return 0.0f;
    int last_valid = NUM_CAL_POINTS - 1;
    while (last_valid >= 0 && sorted_voltages[last_valid] <= 0.0f) last_valid--;

    if (voltage < sorted_voltages[first_valid]) {
        int next_valid = first_valid + 1;
        while (next_valid < NUM_CAL_POINTS && sorted_voltages[next_valid] <= 0.0f) next_valid++;
        if (next_valid >= NUM_CAL_POINTS) return sorted_pressures[first_valid];
        float delta_v = sorted_voltages[next_valid] - sorted_voltages[first_valid];
        float delta_p = sorted_pressures[next_valid] - sorted_pressures[first_valid];
        if (delta_v == 0) return sorted_pressures[first_valid];
        float frac = (voltage - sorted_voltages[first_valid]) / delta_v;
        float pressure = sorted_pressures[first_valid] + frac * delta_p;
        ESP_LOGI(TAG, "[CALIB] Extrapulação abaixo: V=%.3f → P=%.3f bar", voltage, pressure);
        return pressure > 0 ? pressure : 0.0f;
    } else {
        int prev_valid = last_valid - 1;
        while (prev_valid >= 0 && sorted_voltages[prev_valid] <= 0.0f) prev_valid--;
        if (prev_valid < 0) prev_valid = last_valid;
        float delta_v = sorted_voltages[last_valid] - sorted_voltages[prev_valid];
        float delta_p = sorted_pressures[last_valid] - sorted_pressures[prev_valid];
        if (delta_v == 0) return sorted_pressures[last_valid];
        float frac = (voltage - sorted_voltages[last_valid]) / delta_v;
        float pressure = sorted_pressures[last_valid] + frac * delta_p;
        ESP_LOGI(TAG, "[CALIB] Extrapulação acima: V=%.3f → P=%.3f bar", voltage, pressure);
        return pressure > 0 ? pressure : 0.0f;
    }
}


esp_err_t save_reference_points_sensor1(const reference_point_t *sensor1)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao abrir NVS (sensor1): %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, "sensor1_refs", sensor1, sizeof(reference_point_t) * NUM_CAL_POINTS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao salvar sensor1_refs: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t load_reference_points_sensor1(reference_point_t *points) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao abrir NVS (sensor1): %s", esp_err_to_name(err));
        return err;
    }

    size_t size = sizeof(reference_point_t) * NUM_CAL_POINTS;
    err = nvs_get_blob(handle, "sensor1_refs", points, &size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Nenhum dado de calibração encontrado para sensor1.");
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao ler blob sensor1: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t save_reference_points_sensor2(const reference_point_t *sensor2)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao abrir NVS (sensor2): %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, "sensor2_refs", sensor2, sizeof(reference_point_t) * NUM_CAL_POINTS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao salvar sensor2_refs: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t load_reference_points_sensor2(reference_point_t *points) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao abrir NVS (sensor2): %s", esp_err_to_name(err));
        return err;
    }

    size_t size = sizeof(reference_point_t) * NUM_CAL_POINTS;
    err = nvs_get_blob(handle, "sensor2_refs", points, &size);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Nenhum dado de calibração encontrado para sensor2.");
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao ler blob sensor2: %s", esp_err_to_name(err));
    }

    return err;
}

float get_calibrated_pressure(sensor_t leitor, pressure_unit_t unidade, bool *sensor_ok) {
    float voltage = oneshot_analog_read(leitor);
    printf("[SENSOR] Leitura atual: %.3f V\n", voltage);
    ESP_LOGI(TAG, "Unidade solicitada: %d", unidade);

    if (voltage < SENSOR_DISCONNECTED_THRESHOLD) {
        *sensor_ok = false;
        printf("[SENSOR] Sensor possivelmente desconectado ou leitura inválida.\n");
        return 0.0f;
    }

    *sensor_ok = true;

    // Carregar dados de calibração
    reference_point_t ref_points[NUM_CAL_POINTS];

esp_err_t err = (leitor == analog_1)
                    ? load_reference_points_sensor1(ref_points)
                    : load_reference_points_sensor2(ref_points);

        // Se não houver dados gravados, usa mapeamento padrão 0–4.5 V → 0–20 bar
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "[CALIB] Nenhum dado gravado, usando mapeamento padrão (0–4.5V→0–20bar)");
        *sensor_ok = true;
        const float VMAX = 4.5f, PMAX = 20.0f;
        switch (unidade) {
            case PRESSURE_UNIT_MCA:
                // converte para bar e depois para m.c.a
                return (voltage / VMAX) * PMAX * MCA_CONVERSION;
            case PRESSURE_UNIT_PSI:
                // converte para bar e depois para psi
                return (voltage / VMAX) * PMAX * PSI_CONVERSION;
            case PRESSURE_UNIT_BAR:
            default:
                // apenas mapeamento direto para bar
                return (voltage / VMAX) * PMAX;
        }
    }
    
    else if (err != ESP_OK) {
        ESP_LOGE("CALIB", "Erro ao carregar dados da NVS: %s", esp_err_to_name(err));
        *sensor_ok = false;
        return 0.0f;
    }

    // Validar e logar pontos de calibração
    float ref_pressures_bar[NUM_CAL_POINTS];
    float cal_voltages[NUM_CAL_POINTS]; // Declaração adicionada
    int valid_count = 0;
    for (int i = 0; i < NUM_CAL_POINTS; i++) {
        cal_voltages[i] = ref_points[i].real_value;
        ref_pressures_bar[i] = ref_points[i].ref_value; // Usar ref_value como pressÃ£o de referÃªncia
        if (cal_voltages[i] > 0.0f || (i == 0 && cal_voltages[i] == 0.0f && ref_pressures_bar[i] == 0.0f)) {
            valid_count++;
            ESP_LOGI("CALIB", "Ponto %d: V=%.3f, P=%.2f bar", i, cal_voltages[i], ref_pressures_bar[i]);
        } else {
            ESP_LOGW("CALIB", "Ponto %d inválido: V=%.3f", i, cal_voltages[i]);
        }
    }

  if (valid_count == 0) {
   ESP_LOGW("CALIB", "[CALIB] Nenhum ponto válido. Usando mapeamento padrão.");
   *sensor_ok = true;
   const float VMAX = 4.5f, PMAX = 20.0f;
   float pbar = (voltage / VMAX) * PMAX;
   if (pbar < 0.0f) pbar = 0.0f;
   switch (unidade) {
     case PRESSURE_UNIT_MCA: return pbar * MCA_CONVERSION;
     case PRESSURE_UNIT_PSI: return pbar * PSI_CONVERSION;
     case PRESSURE_UNIT_BAR:
     default:                return pbar;
   }
 }   
    
    float pbar = interpolate_pressure(voltage, cal_voltages, ref_pressures_bar);
    ESP_LOGI("PRESSURE_CALIBRATE", "[CALIB] InterpolaÃ§Ã£o: V=%.3f â†’ P=%.3f bar", voltage, pbar);

       // ConversÃ£o para a unidade solicitada
    ESP_LOGI("PRESSURE_CALIBRATE", "Unidade solicitada: %d", unidade); // Log para depuraÃ§Ã£o
    switch (unidade) {
        case PRESSURE_UNIT_BAR:
            return pbar;
        case PRESSURE_UNIT_MCA:
            return pbar * MCA_CONVERSION;
        case PRESSURE_UNIT_PSI:
            return pbar * PSI_CONVERSION;
        default:
            return pbar; // fallback
    }
}

// Converte bar para mca
float bar_to_mca(float bar) {
    return bar * 10.1974f;
}




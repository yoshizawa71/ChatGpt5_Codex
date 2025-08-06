/*
 * battery_monitor.h
 *
 *  Created on: 2 de ago. de 2025
 *      Author: geopo
 */

#ifndef DATALOGGER_DATALOGGER_CONTROL_INCLUDE_BATTERY_MONITOR_H_
#define DATALOGGER_DATALOGGER_CONTROL_INCLUDE_BATTERY_MONITOR_H_
#include "esp_err.h"
#pragma once

#include <stdint.h>
#include <stdbool.h>

// Configuração de tensão:
#define BATT_EMPTY_VOLTAGE          2.00f   // 0%
#define RECHARGEABLE_FULL_VOLTAGE   4.20f   // célula recarregável cheia
#define NONRECHARGEABLE_FULL_VOLTAGE 3.60f  // célula não recarregável cheia
#define SOC_SMOOTHING_ALPHA         0.2f    // suavização exponencial

// Histerese / confirmação
#define RECHARGEABLE_ENTER_V        3.8f
#define RECHARGEABLE_EXIT_V         3.5f
#define RECHARGEABLE_CONFIRM_COUNT  2
#define NONRECHARGEABLE_CONFIRM_COUNT 2

#define POWER_SOURCE_MIN_OK   4.5f
#define POWER_SOURCE_MAX_OK   18.0f

// Fatores de escala do divisor
#define BATTERY_SCALE_FACTOR        2.0f                        // divisor 10k/10k
#define POWER_SOURCE_SCALE_FACTOR  ((10000.0f + 2200.0f) / 2200.0f) // ≈5.545 divisor 10k/2.2k

#define ABSENCE_SAMPLE_COUNT 5
#define ABSENCE_CONFIRM_STREAK 2
#define ABSENCE_MAX_BASELINE_FOR_CAL 0.2f  // se a média for maior que isso, não assume ausência
#define ABSENCE_DELTA 0.05f                // margem acima do baseline


#define ABSENCE_REQUIRED_STREAK 3
#define ABSENCE_SAMPLE_THRESHOLD 0.2f  // abaixo disso é considerado "sem bateria"
#define ABSENCE_MARGIN 0.05f           // delta para threshold

// Parâmetros do teste rápido de carga
#define LOAD_TEST_DROP_THRESHOLD 0.10f  // queda de 100mV sob carga indica fraqueza
#define LOAD_TEST_DURATION_MS     100   // aplica carga por 100ms

#define HEALTH_DEGRADATION_FACTOR 0.8f

// Inicializa o monitor (carrega estado do NVS)
void battery_monitor_init(bool enable_interactive_calibration);

// Atualiza leituras de bateria e fonte, preenche self_monitoring_data e grava JSON
void battery_monitor_update(void);

// Retorna tensão medida da bateria (volts)
float battery_monitor_get_voltage(void);

// Retorna SoC estimado da bateria (0.0 .. 1.0)
float battery_monitor_get_soc(void);

// Retorna capacidade restante estimada em mAh (nominal * aging * soc)
float battery_monitor_get_remaining_mAh(void);

// Retorna tensão da fonte principal (volts)
float battery_monitor_get_power_source_voltage(void);

// Indica se a fonte está dentro da faixa aceitável
bool battery_monitor_power_source_ok(void);

// Deve ser chamada quando detectar carregamento completo (para resetar aging)
void battery_monitor_mark_full_charge(void);

// Persiste o estado manualmente (usa internamente também)
void battery_monitor_save_state(void);

esp_err_t load_power_source_correction(void);

esp_err_t calibrate_power_source(float actual_voltage);

float get_power_source_correction_factor(void);

#endif /* DATALOGGER_DATALOGGER_CONTROL_INCLUDE_BATTERY_MONITOR_H_ */

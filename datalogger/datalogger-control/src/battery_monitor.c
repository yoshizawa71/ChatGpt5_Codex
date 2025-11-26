#include "battery_monitor.h"
#include "TCA6408A.h"
#include "ads1015_reader.h" // precisa exportar oneshot_analog_read() e enum sensor_t
#include "cJSON.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "freertos/semphr.h"
#include "datalogger_control.h"
#include "datalogger_driver.h"

// Espera que estes existam em outro módulo (você já tem)
extern SemaphoreHandle_t file_mutex;
extern struct self_monitoring_data self_monitoring_data;

static const char *TAG = "BATTERY_MONITOR";

// Estado persistido entre leituras para evitar oscillação
static bool last_was_rechargeable = false;
// variável persistida (carregar de NVS no init, default 1.0f)
static float power_source_scale_correction = 1.046f;

// parâmetros persistidos
static float nominal_capacity_mAh = 6600.0f; // capacidade original de projeto
static float aging_factor = 1.0f; // 1.0 = sem degradação

 static float last_smoothed_soc = -1.0f;

// leituras correntes
static float prev_voltage_battery = -1.0f;
static float voltage_battery = 0.0f;
static float voltage_power_source = 0.0f;

static int64_t last_self_mon_save_ts = 0; // em ticks de ms

static float battery_absence_baseline = 0.0f;
static float battery_presence_threshold = 0.15f; // fallback inicial
static bool battery_absence_calibrated = false;
static float absence_accum = 0.0f;
static int absence_good_count = 0;

static float battery_full_voltage_rechargeable = RECHARGEABLE_FULL_VOLTAGE;     // default 4.2
static float battery_full_voltage_nonrechargeable = NONRECHARGEABLE_FULL_VOLTAGE; // default 3.6
static float battery_empty_voltage = BATT_EMPTY_VOLTAGE;                        // default 2.0

static bool battery_health_degraded = false;
static float effective_capacity_factor = 1.0f;
// NVS
#define NVS_NAMESPACE "batmon"
#define KEY_PS_CORR   "ps_corr"
#define KEY_AGING "aging"
#define KEY_NOMINAL "nominal"

#define SELF_MON_SAVE_DEBOUNCE_MS 500
#define SOC_SMOOTHING_ALPHA 0.2f  // quanto mais alto, mais responsivo; mais baixo, mais suave

#define KEY_BATT_FULL_REC  "b_full_rec"
#define KEY_BATT_FULL_NON  "b_full_non"
#define KEY_BATT_EMPTY     "b_empty"

// utilitários internos

static void load_battery_calibration_from_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        // não conseguiu abrir: mantém os valores padrão já inicializados
        ESP_LOGW(TAG, "Não foi possível abrir NVS para calibração da bateria (err=%d), usando valores padrão: nominal=%.1f mAh, aging=%.2f",
                 err, nominal_capacity_mAh, aging_factor);
        return;
    }

    size_t sz;

    // nominal_capacity_mAh
    sz = sizeof(nominal_capacity_mAh);
    err = nvs_get_blob(h, KEY_NOMINAL, &nominal_capacity_mAh, &sz);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Carregado nominal_capacity_mAh=%.1f mAh da NVS", nominal_capacity_mAh);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Chave nominal não encontrada, mantendo capacidade nominal de projeto=%.1f mAh", nominal_capacity_mAh);
    } else {
        ESP_LOGW(TAG, "Erro ao ler nominal_capacity_mAh da NVS (err=%d), mantendo=%.1f mAh", err, nominal_capacity_mAh);
    }

    // aging_factor
    sz = sizeof(aging_factor);
    err = nvs_get_blob(h, KEY_AGING, &aging_factor, &sz);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Carregado aging_factor=%.3f da NVS", aging_factor);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Chave aging não encontrada, usando aging_factor padrão=%.3f", aging_factor);
    } else {
        ESP_LOGW(TAG, "Erro ao ler aging_factor da NVS (err=%d), mantendo=%.3f", err, aging_factor);
    }

    nvs_close(h);
}

static void calibrate_battery_absence(void)
{
    int good_streak = 0;
    float candidate_baseline = 0.0f;

    // tenta até encontrar duas médias consecutivas baixas
    while (good_streak < ABSENCE_CONFIRM_STREAK) {
        float sum = 0.0f;
        for (int i = 0; i < ABSENCE_SAMPLE_COUNT; ++i) {
            float raw = oneshot_analog_read(bateria);
            sum += raw;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        float avg = sum / (float)ABSENCE_SAMPLE_COUNT;

        if (avg < ABSENCE_MAX_BASELINE_FOR_CAL) {
            // se estiver baixo o suficiente, conta como bom
            if (good_streak == 0) {
                candidate_baseline = avg;
            } else {
                // faz média dos dois para suavizar
                candidate_baseline = (candidate_baseline + avg) / 2.0f;
            }
            good_streak++;
        } else {
            // reinicia, não é ausência clara
            good_streak = 0;
        }

        // para evitar loop infinito, pode quebrar após muitas tentativas
        static int attempts = 0;
        if (++attempts > 10) {
            break;
        }
    }

    if (good_streak >= ABSENCE_CONFIRM_STREAK) {
        battery_absence_baseline = candidate_baseline;
        battery_presence_threshold = battery_absence_baseline + ABSENCE_DELTA;
        if (battery_presence_threshold < 0.05f) {
            battery_presence_threshold = 0.05f;
        }
        battery_absence_calibrated = true;
        printf("Calibração ausência: baseline=%.4f, threshold=%.4f\n",
               battery_absence_baseline, battery_presence_threshold);
    } else {
        // não conseguiu confirmar ausência estável
        printf("Calibração ausência falhou: leituras instáveis\n");
    }
}

// Esta função deve ser chamada uma vez logo após acordar, com bateria presente
void battery_health_check_under_load(void)
{
    // 1. Medir tensão em repouso
    float voltage_before = voltage_battery; // assume que battery_monitor_update já foi chamado

    // 2. Aplicar carga conhecida: EXEMPLO simplificado
    // Aqui você precisa ativar algo que consome corrente. Substitua por sua ação real:
    activate_mosfet(enable_analog_sensors); // exemplo: liga um pequeno load; adapte conforme seu hardware

    vTaskDelay(pdMS_TO_TICKS(LOAD_TEST_DURATION_MS));

    // 3. Medir tensão sob carga
    float raw_bat_adc = oneshot_analog_read(bateria);
    float voltage_during = raw_bat_adc * BATTERY_SCALE_FACTOR;

    // 4. Desliga a carga de teste, se era algo temporário
    activate_mosfet(disable_analog_sensors); // ou o inverso da ação usada

    // 5. Avalia queda
    float drop = voltage_before - voltage_during;
    printf("Battery load test: V_rest=%.3fV V_load=%.3fV drop=%.3fV\n",
           voltage_before, voltage_during, drop);

    if (drop >= LOAD_TEST_DROP_THRESHOLD) {
        // bateria com internal resistance alto / degradada
        battery_health_degraded = true;
        effective_capacity_factor = HEALTH_DEGRADATION_FACTOR;
        printf("Battery health degraded detected. Effective capacity scaled by %.2f\n",
               effective_capacity_factor);
    } else {
        // saudável (pode refinar: gradua baseado no drop)
        battery_health_degraded = false;
        effective_capacity_factor = 1.0f;
    }
}

esp_err_t load_battery_voltage_calibration(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Não abriu NVS para calibração de tensão da bateria (err=%d), usando defaults", err);
        return err;
    }

    size_t sz;

    sz = sizeof(battery_full_voltage_rechargeable);
    if (nvs_get_blob(h, KEY_BATT_FULL_REC, &battery_full_voltage_rechargeable, &sz) != ESP_OK) {
        battery_full_voltage_rechargeable = RECHARGEABLE_FULL_VOLTAGE;
    }

    sz = sizeof(battery_full_voltage_nonrechargeable);
    if (nvs_get_blob(h, KEY_BATT_FULL_NON, &battery_full_voltage_nonrechargeable, &sz) != ESP_OK) {
        battery_full_voltage_nonrechargeable = NONRECHARGEABLE_FULL_VOLTAGE;
    }

    sz = sizeof(battery_empty_voltage);
    if (nvs_get_blob(h, KEY_BATT_EMPTY, &battery_empty_voltage, &sz) != ESP_OK) {
        battery_empty_voltage = BATT_EMPTY_VOLTAGE;
    }

    nvs_close(h);
    return ESP_OK;
}

esp_err_t save_battery_voltage_calibration(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(h, KEY_BATT_FULL_REC, &battery_full_voltage_rechargeable, sizeof(battery_full_voltage_rechargeable));
    if (err == ESP_OK) {
        err = nvs_set_blob(h, KEY_BATT_FULL_NON, &battery_full_voltage_nonrechargeable, sizeof(battery_full_voltage_nonrechargeable));
    }
    if (err == ESP_OK) {
        err = nvs_set_blob(h, KEY_BATT_EMPTY, &battery_empty_voltage, sizeof(battery_empty_voltage));
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }

    nvs_close(h);
    return err;
}

esp_err_t save_power_source_correction(float correction)
{
    power_source_scale_correction = correction;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Erro abrindo NVS para salvar correção da fonte: %d", err);
        return err;
    }
    err = nvs_set_blob(h, KEY_PS_CORR, &power_source_scale_correction, sizeof(power_source_scale_correction));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Erro salvando correção da fonte na NVS: %d", err);
    }
    return err;
}

void battery_monitor_init(bool enable_interactive_calibration)
{
	ESP_LOGI(TAG, "Battery Monitor Init");
    // 1. Inicializa NVS (idempotente)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }

    // 2. Carrega calibração de capacidade / aging e curva full/empty da bateria
    load_battery_calibration_from_nvs();
    load_battery_voltage_calibration();

    // 3. Carrega fator de correção da fonte
    if (load_power_source_correction() != ESP_OK) {
        power_source_scale_correction = 1.0f; // fallback
    }

    ESP_LOGI(TAG, "Init: power_source_scale_correction=%.4f, full_rec=%.2f, full_non=%.2f, empty=%.2f",
             power_source_scale_correction,
             battery_full_voltage_rechargeable,
             battery_full_voltage_nonrechargeable,
             battery_empty_voltage);

    // 4. Auto-calibração interativa (somente se habilitada e não calibrado ainda)
    if (enable_interactive_calibration && power_source_scale_correction == 1.0f) {
        // calcula leitura não calibrada média
        const int N = 5;
        float avg_uncal = 0.0f;
        for (int i = 0; i < N; ++i) {
            float raw = oneshot_analog_read(fonte);
            avg_uncal += raw * POWER_SOURCE_SCALE_FACTOR;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        avg_uncal /= (float)N;

        ESP_LOGI(TAG, "Auto-calibração: leitura não calibrada estimada = %.3f V", avg_uncal);
        ESP_LOGI(TAG, "Digite a tensão real medida em Vin (por ex. 7.19) dentro de 5 segundos e pressione ENTER:");

        // lê entrada do usuário com timeout (5 segundos)
        char buf[32] = {0};
        int len = 0;
        int64_t start = esp_timer_get_time(); // microsegundos
        while ((esp_timer_get_time() - start) < 5 * 1000 * 1000) { // 5s
            uint8_t ch;
            int r = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(100));
            if (r > 0) {
                if (ch == '\r' || ch == '\n') {
                    if (len > 0) break; // terminou
                    else continue; // ignorar novas linhas no começo
                }
                if (len < (int)(sizeof(buf) - 1)) {
                    buf[len++] = (char)ch;
                }
                // eco opcional
                uart_write_bytes(UART_NUM_0, (const char *)&ch, 1);
            }
        }
        buf[len] = '\0';
        if (len == 0) {
            ESP_LOGI(TAG, "Auto-calibração: sem entrada do usuário, pulando.");
        } else {
            float actual = strtof(buf, NULL);
            if (actual > 0.0f) {
                esp_err_t err = calibrate_power_source(actual);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "Calibração aplicada, novo fator = %.4f", get_power_source_correction_factor());
                } else {
                    ESP_LOGW(TAG, "Falha ao salvar calibração da fonte (err=%d)", err);
                }
            } else {
                ESP_LOGW(TAG, "Valor inválido fornecido para calibração: '%s'", buf);
            }
        }
    }
}

// SoC dinâmico com histerese entre recarregável e não recarregável
static float soc_from_voltage_dynamic(float vbat)
{
    if (vbat >= battery_full_voltage_rechargeable) {
        return 1.0f;
    }
    if (vbat <= battery_empty_voltage) {
        return 0.0f;
    }

    // decide recarregável rapidamente
    if (vbat >= RECHARGEABLE_ENTER_V) {
        last_was_rechargeable = true;
    } else if (vbat <= RECHARGEABLE_EXIT_V) {
        last_was_rechargeable = false;
    }

    float full_voltage = last_was_rechargeable ? battery_full_voltage_rechargeable
                                               : battery_full_voltage_nonrechargeable;

    if (vbat >= full_voltage) {
        return 1.0f;
    }
    return (vbat - battery_empty_voltage) / (full_voltage - battery_empty_voltage);
}

// Leitura corrigida
static float read_battery_voltage_real(void)
{
    float v_adc = oneshot_analog_read(bateria); // nó do divisor da bateria
    return v_adc * BATTERY_SCALE_FACTOR;
}

static float read_power_source_voltage_real(void)
{
    float v_adc = oneshot_analog_read(fonte); // nó do divisor
    float vin = v_adc * POWER_SOURCE_SCALE_FACTOR; // aproximação antes da calibração
    return vin * power_source_scale_correction; // aplica correção empírica
}

// Calibra com o valor real medido (em volts) e salva
esp_err_t calibrate_power_source(float actual_voltage)
{
    if (actual_voltage <= 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    // 1. Faça média de N leituras para reduzir jitter
    const int N = 5;
    float avg_uncal = 0.0f;
    for (int i = 0; i < N; ++i) {
        float raw_src_adc = oneshot_analog_read(fonte);
        avg_uncal += raw_src_adc * POWER_SOURCE_SCALE_FACTOR; // ainda sem corrigir
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    avg_uncal /= (float)N;

    if (avg_uncal < 1e-3f) {
        return ESP_FAIL; // leitura inválida
    }

    // 2. Valor corrigido atual (com o fator que já está em power_source_scale_correction)
    float calibrated = avg_uncal * power_source_scale_correction;

    // 3. Novo ajuste: real / medido
    float ajuste = actual_voltage / calibrated;
    power_source_scale_correction *= ajuste;

    // 4. Salva na NVS o novo fator
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, KEY_PS_CORR, &power_source_scale_correction, sizeof(power_source_scale_correction));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

// Retorna o fator atual (para debug ou exibir)
float get_power_source_correction_factor(void)
{
    return power_source_scale_correction;
}

// Carrega da NVS; chama no init
esp_err_t load_power_source_correction(void)
{
    nvs_handle_t h;
    size_t sz = sizeof(power_source_scale_correction);
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_blob(h, KEY_PS_CORR, &power_source_scale_correction, &sz);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
       // power_source_scale_correction = 1.0f; // fallback
        err = ESP_OK;
    }
    nvs_close(h);
    return err;
}

float battery_monitor_get_voltage(void)
{
    return voltage_battery;
}

float battery_monitor_get_power_source_voltage(void)
{
	ESP_LOGI(TAG, ">>>Voltage = %f <<<\n",voltage_power_source);
    return voltage_power_source;
}

float battery_monitor_get_soc(void)
{
    if (last_smoothed_soc < 0.0f) {
        return 0.0f;
    }
    return last_smoothed_soc;
}

float battery_monitor_get_remaining_mAh(void)
{
    float soc = battery_monitor_get_soc();
    return nominal_capacity_mAh * aging_factor * soc;
}

bool battery_monitor_power_source_ok(void)
{
    return (voltage_power_source >= POWER_SOURCE_MIN_OK && voltage_power_source <= POWER_SOURCE_MAX_OK);
}

void battery_monitor_mark_full_charge(void)
{
    float soc = battery_monitor_get_soc();
    if (soc >= 0.98f) {
        aging_factor = 1.0f; // assume carga completa saudável
        battery_monitor_save_state();
        ESP_LOGI(TAG, "Full charge detectado, aging resetado para %.3f", aging_factor);
    }
}

void battery_monitor_save_state(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, KEY_NOMINAL, &nominal_capacity_mAh, sizeof(nominal_capacity_mAh));
        nvs_set_blob(h, KEY_AGING, &aging_factor, sizeof(aging_factor));
        nvs_commit(h);
        nvs_close(h);
    }
}

void battery_monitor_update(void)
{
	ESP_LOGI(TAG, "Battery Monitor Update");
    static int64_t last_self_mon_save_ts = 0;
    static int64_t last_health_check_ts = 0;

    // 1. Leituras raw únicas
    float raw_bat_adc = oneshot_analog_read(bateria);
    float raw_src_adc = oneshot_analog_read(fonte);

    // 2. Escala física
    voltage_battery = raw_bat_adc * BATTERY_SCALE_FACTOR;
    float voltage_power_source_uncal = raw_src_adc * POWER_SOURCE_SCALE_FACTOR;
    voltage_power_source = voltage_power_source_uncal * power_source_scale_correction;

    // --- test de saúde sob carga: faz uma vez a cada 5 minutos (ajuste se quiser) ---
    const int64_t now_ms = esp_timer_get_time() / 1000;
    if (battery_absence_calibrated && (now_ms - last_health_check_ts) >= 5 * 60 * 1000) {
        // só se bateria presente (evita testar sem bateria)
        bool battery_present_for_health = (raw_bat_adc > battery_presence_threshold) || (!battery_absence_calibrated && raw_bat_adc > 0.1f);
        if (battery_present_for_health) {
            float voltage_before = voltage_battery;

            // 2a. Aplica carga conhecida: ajuste isto para o seu hardware real.
            // Exemplo: ligar um MOSFET que consome corrente como "teste de carga".
            activate_mosfet(0); // <<< substitua por sua ação de carga breve
            vTaskDelay(pdMS_TO_TICKS(LOAD_TEST_DURATION_MS));
            float raw_bat_adc_load = oneshot_analog_read(bateria);
            float voltage_during = raw_bat_adc_load * BATTERY_SCALE_FACTOR;
            // desliga a carga de teste
            // se você usou activate_mosfet(0) para carga, precisa a função contrária; ajusta conforme seu design
            activate_mosfet(1); // <<< exemplo de desligar, troque conforme seu controle de power

            float drop = voltage_before - voltage_during;
            printf("Battery load test: V_rest=%.3fV V_load=%.3fV drop=%.3fV\n",
                   voltage_before, voltage_during, drop);

            if (drop >= LOAD_TEST_DROP_THRESHOLD) {
                battery_health_degraded = true;
                effective_capacity_factor = 0.8f; // degradação simples (80% da capacidade)
                printf("Battery health degraded. Capacity scaled by %.2f\n", effective_capacity_factor);
            } else {
                battery_health_degraded = false;
                effective_capacity_factor = 1.0f;
            }

            last_health_check_ts = now_ms;
        }
    }

    // 3. SoC bruto dinâmico
    float raw_soc = soc_from_voltage_dynamic(voltage_battery);

    // 4. Suavização assimétrica com resposta rápida a queda de tensão
    const float alpha_up = 0.05f;              // subida lenta
    const float alpha_down = 1.0f;             // descida instantânea
    const float DROP_VOLTAGE_THRESHOLD = 0.01f; // 10 mV de queda força adaptação imediata

    if (last_smoothed_soc < 0.0f) {
        last_smoothed_soc = raw_soc;
    }
    if (prev_voltage_battery < 0.0f) {
        prev_voltage_battery = voltage_battery;
    }

    float smoothed_soc;
    if (prev_voltage_battery - voltage_battery > DROP_VOLTAGE_THRESHOLD) {
        smoothed_soc = raw_soc;
    } else if (raw_soc > last_smoothed_soc) {
        smoothed_soc = alpha_up * raw_soc + (1.0f - alpha_up) * last_smoothed_soc;
    } else {
        smoothed_soc = alpha_down * raw_soc + (1.0f - alpha_down) * last_smoothed_soc;
    }

    if (smoothed_soc > 1.0f) smoothed_soc = 1.0f;
    if (smoothed_soc < 0.0f) smoothed_soc = 0.0f;
    last_smoothed_soc = smoothed_soc;
    prev_voltage_battery = voltage_battery;

    // 5. Percentual ajustado pela saúde
    float adjusted_soc = smoothed_soc * effective_capacity_factor;
    if (adjusted_soc > 1.0f) adjusted_soc = 1.0f;
    uint16_t batt_percent = (uint16_t)(adjusted_soc * 100.0f + 0.5f);

    // 6. Detecta presença de bateria usando calibração incremental
    bool battery_present = false;
    if (!battery_absence_calibrated) {
        float sample = oneshot_analog_read(bateria);
        absence_accum = (absence_accum * (ABSENCE_REQUIRED_STREAK - 1) + sample) / (float)ABSENCE_REQUIRED_STREAK;

        if (absence_accum < ABSENCE_SAMPLE_THRESHOLD) {
            absence_good_count++;
        } else {
            absence_good_count = 0;
        }

        if (absence_good_count >= ABSENCE_REQUIRED_STREAK) {
            battery_absence_baseline = absence_accum;
            battery_presence_threshold = battery_absence_baseline + ABSENCE_MARGIN;
            if (battery_presence_threshold < 0.05f) {
                battery_presence_threshold = 0.05f;
            }
            battery_absence_calibrated = true;
            printf("Calibração ausência incremental: baseline=%.4f, threshold=%.4f\n",
                   battery_absence_baseline, battery_presence_threshold);
        }
    }

    if (battery_absence_calibrated) {
        battery_present = (raw_bat_adc > battery_presence_threshold);
    } else {
        battery_present = (raw_bat_adc > 0.1f);
    }

    if (!battery_present) {
        batt_percent = 0;
        smoothed_soc = 0.0f;
    }

    // 7. Atualiza struct compartilhada
    set_battery(batt_percent);
    self_monitoring_data.power_source = (uint16_t)(voltage_power_source * 1000.0f + 0.5f); // mV

    // 8. Debug detalhado
    printf("[DEBUG BATMON] voltage_battery=%.3fV raw_soc=%.3f smoothed_soc=%.3f adjusted_soc=%.3f threshold=%.3f baseline=%.3f calibrated=%d present=%d degraded=%d\n",
           voltage_battery,
           raw_soc,
           last_smoothed_soc,
           adjusted_soc,
           battery_presence_threshold,
           battery_absence_baseline,
           battery_absence_calibrated,
           battery_present,
           battery_health_degraded);
    printf("ADC raw bateria: %.3fV -> real: %.3fV (soc=%.1f%%) present=%d\n",
           raw_bat_adc, voltage_battery, smoothed_soc * 100.0f, battery_present);
    printf("ADC raw fonte: %.3fV -> uncalibrated: %.3fV calibrated: %.3fV (factor=%.4f)\n",
           raw_src_adc, voltage_power_source_uncal, voltage_power_source, power_source_scale_correction);

    // 9. Salvamento com debounce
    if (now_ms - last_self_mon_save_ts >= 500) {
        if (save_self_monitoring_data(&self_monitoring_data) != ESP_OK) {
            ESP_LOGW(TAG, "Falha ao salvar self monitoring data");
        }
        last_self_mon_save_ts = now_ms;
    }
}
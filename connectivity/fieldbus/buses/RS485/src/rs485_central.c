#include "rs485_central.h"

#include <stdio.h>

#include "datalogger_driver.h"
#include "energy_meter.h"
#include "esp_err.h"
#include "esp_log.h"
#include "modbus_rtu_master.h"
#include "rs485_hw.h"
#include "rs485_registry.h"
#include "rs485_registry_adapter.h"
#include "rs485_sd_adapter.h"

static const char *TAG = "RS485_CENTRAL";
static const char *CFG_TAG = "RS485/CFG";

#ifndef RS485_CENTRAL_FIX_CHANNEL
#define RS485_CENTRAL_FIX_CHANNEL   3
#endif
#ifndef RS485_CENTRAL_FIX_ADDRESS
#define RS485_CENTRAL_FIX_ADDRESS   1
#endif

static char parity_letter(uart_parity_t p)
{
    switch (p) {
        case UART_PARITY_EVEN:  return 'E';
        case UART_PARITY_ODD:   return 'O';
        case UART_PARITY_DISABLE:
        default:                return 'N';
    }
}

static int stop_bits_value(uart_stop_bits_t s)
{
    return (s == UART_STOP_BITS_2) ? 2 : 1;
}

typedef struct {
    bool applied;
    rs485_port_cfg_t prev_port;
    uint32_t prev_req_timeout;
    uint32_t prev_ping_timeout;
} profile_guard_t;

static bool apply_profile(const char *context, const rs485_sensor_t *sensor, profile_guard_t *guard)
{
    if (!sensor || !guard) return false;

    rs485_profile_t prof;
    if (!rs485_get_fixed_profile(sensor->type, sensor->subtype, &prof)) {
        return false;
    }

    guard->prev_req_timeout  = modbus_master_get_request_timeout();
    guard->prev_ping_timeout = modbus_master_get_ping_timeout();
    rs485_port_cfg_capture(&guard->prev_port);

    if (rs485_apply_port_config(prof.baud, prof.parity, prof.stop) != ESP_OK) {
        ESP_LOGW(CFG_TAG, "%s: falha ao aplicar perfil (ch=%u addr=%u)",
                 context, (unsigned)sensor->channel, (unsigned)sensor->address);
        return false;
    }

    modbus_master_set_request_timeout(prof.timeout_ms);
    modbus_master_set_ping_timeout(prof.timeout_ms);
    guard->applied = true;

    ESP_LOGI(CFG_TAG,
             "%s: ch=%u addr=%u → %u baud parity=%c stop=%d timeout=%ums",
             context,
             (unsigned)sensor->channel,
             (unsigned)sensor->address,
             prof.baud,
             parity_letter(prof.parity),
             stop_bits_value(prof.stop),
             prof.timeout_ms);
    return true;
}

static void restore_profile(profile_guard_t *guard)
{
    if (!guard || !guard->applied) return;
    modbus_master_set_request_timeout(guard->prev_req_timeout);
    modbus_master_set_ping_timeout(guard->prev_ping_timeout);
    rs485_port_cfg_restore(&guard->prev_port);
    guard->applied = false;
}

static bool is_th_sensor(rs485_type_t type)
{
    return (type == RS485_TYPE_TERMOHIGRO ||
            type == RS485_TYPE_TEMPERATURA ||
            type == RS485_TYPE_UMIDADE);
}

void rs485_central_poll_and_save(uint32_t timeout_ms)
{
    (void)timeout_ms;

    rs485_sensor_t energy_sensor = {
        .channel = RS485_CENTRAL_FIX_CHANNEL,
        .address = RS485_CENTRAL_FIX_ADDRESS,
        .type    = RS485_TYPE_ENERGIA,
        .subtype = RS485_SUBTYPE_TRIFASICO,
    };

    profile_guard_t energy_guard = {0};
    (void)apply_profile("Central/energia", &energy_sensor, &energy_guard);

    esp_err_t err = energy_meter_save_currents(RS485_CENTRAL_FIX_CHANNEL,
                                               RS485_CENTRAL_FIX_ADDRESS);
    restore_profile(&energy_guard);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Central: lido 1 sensor energia; gravado %d", 3);
    } else {
        ESP_LOGW(TAG, "Central: falha ao gravar energia (ch=%d addr=%d): %s",
                 RS485_CENTRAL_FIX_CHANNEL, RS485_CENTRAL_FIX_ADDRESS, esp_err_to_name(err));
    }

    rs485_sensor_t sensors[RS485_MAX_SENSORS] = {0};
    int total = rs485_registry_get_snapshot(sensors, RS485_MAX_SENSORS);
    if (total <= 0) {
        ESP_LOGW(TAG, "Central: snapshot vazio ou erro (%d)", total);
        ESP_LOGI(TAG, "Central: T/H processados=%d; gravado %d", 0, 0);
        return;
    }

    rs485_measurement_t measurements[RS485_MAX_SENSORS * 2] = {0};
    size_t wr = 0;

    for (int i = 0; i < total; ++i) {
        if (!is_th_sensor(sensors[i].type)) {
            continue;
        }

        if (wr >= sizeof(measurements) / sizeof(measurements[0])) {
            ESP_LOGW(TAG, "Central: buffer de medições saturado (%zu entradas)", wr);
            break;
        }

        profile_guard_t guard = {0};
        bool applied = apply_profile("Central/T-H", &sensors[i], &guard);

        size_t remaining = (sizeof(measurements) / sizeof(measurements[0])) - wr;
        int got = rs485_read_measurements(&sensors[i], &measurements[wr], remaining);

        restore_profile(&guard);

        if (got < 0) {
            ESP_LOGW(TAG, "Central: leitura T/H falhou (ch=%u addr=%u ret=%d)",
                     (unsigned)sensors[i].channel,
                     (unsigned)sensors[i].address,
                     got);
            continue;
        }

        wr += (size_t)got;

        if (applied && got == 0) {
            ESP_LOGW(TAG, "Central: perfil aplicado sem medições (ch=%u addr=%u)",
                     (unsigned)sensors[i].channel,
                     (unsigned)sensors[i].address);
        }
    }

    int saved = 0;
    for (size_t i = 0; i < wr; ++i) {
        const rs485_measurement_t *m = &measurements[i];
        int sub = 0;
        switch (m->kind) {
            case RS485_MEAS_TEMP_C:
                sub = 1;
                break;
            case RS485_MEAS_HUM_PCT:
                sub = 2;
                break;
            default:
                break;
        }

        if (sub == 0) {
            continue;
        }

        char key[12];
        int len = snprintf(key, sizeof(key), "%u.%d", (unsigned)m->channel, sub);
        if (len <= 0 || len >= (int)sizeof(key)) {
            ESP_LOGW(TAG, "Central: chave inválida ao gravar T/H (ch=%u sub=%d)",
                     (unsigned)m->channel, sub);
            continue;
        }

        if (rs485_sd_adapter_save_record(key, m->value) == ESP_OK) {
            saved++;
        } else {
            ESP_LOGW(TAG, "Central: falha ao gravar T/H (key=%s)", key);
        }
    }

    ESP_LOGI(TAG, "Central: T/H processados=%d; gravado %d", (int)wr, saved);
}

/*
 * lte_payload_builder.c
 *
 *  Builder de payload para LTE (HTTP + MQTT) baseado na
 *  mesma lógica do json_data_payload() em server_comm.c:
 *
 *  - Não perde nenhuma linha do SD: todo registro lido entra
 *    no JSON (até MAX_POINTS_TO_SEND).
 *  - Usa rec_index.{last_read_idx, last_write_idx, total_idx}
 *    e cursor_position para caminhar corretamente no ring buffer.
 *
 *  Formato do JSON (por payload):
 *
 *    {
 *      "id": "COGNETi_TEST",
 *      "Nome": "...",
 *      "Número Serial": "...",
 *      "Telefone": "...",
 *      "CSQ": 18,
 *      "Battery": "7.08",
 *      "measurements": [
 *        {
 *          "DateTime": "2025-09-23T17:02:00.000-03:00"  OU  1758285600000,
 *          "Pressao": 37.1,         // opcional
 *          "Vazao": 0.5             // opcional
 *        },
 *        ...
 *      ]
 *    }
 *
 *  Observações:
 *  - A Data/Hora no SD já está em horário local (-03:00).
 *  - NÃO fazemos ajuste de fuso (nem -3h, nem +3h).
 *  - Apenas mudamos o formato:
 *      - String ISO: "AAAA-MM-DDTHH:MM:SS.000-03:00", OU
 *      - Epoch em ms, SEM mexer na hora (checkbox timestamp).
 *  - Dentro de measurements:
 *      - Só tem DateTime, Pressao (se disponível) e Vazao (se disponível).
 */

#include "lte_payload_builder.h"

#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>

#include "datalogger_control.h"
#include "esp_err.h"
#include "esp_log.h"
#include "cJSON.h"

#include "sdmmc_driver.h"         // struct record_index_config, struct record_data_saved, read_record_sd(), UNSPECIFIC_RECORD
#include "battery_monitor.h"      // battery_monitor_update(), battery_monitor_get_power_source_voltage()
#include "factory_control.h"      // get_device_id(), get_name(), get_serial_number(), get_phone(), get_csq()

// Essas funções vêm dos módulos de configuração de rede/operação.
/*bool has_network_user_enabled(void);
bool has_network_pw_enabled(void);
bool has_network_token_enabled(void);
bool has_network_http_enabled(void);

// Checkbox do timestamp em ms (vem da config / factory_control).
bool has_timestamp_mode(void);*/

// Limite de pontos enviados em um único payload (mesma ideia do server_comm.c)
#ifndef MAX_POINTS_TO_SEND
#define MAX_POINTS_TO_SEND 10
#endif

static const char *TAG = "LTE_PAYLOAD";

// ---------------------------------------------------------------------------
// Converte Data/Hora do SD ("DD/MM/AAAA" e "HH:MM:SS")
// para epoch em ms, SEM ajustar fuso (NÃO tira 3h, NÃO soma 3h).
// ---------------------------------------------------------------------------
static bool sd_datetime_to_ms_no_tz(const char *date_str,
                                    const char *time_str,
                                    int64_t *out_ms)
{
    if (!date_str || !time_str || !out_ms) {
        return false;
    }

    int day, mon, year;
    int hh, mm, ss;

    if (sscanf(date_str, "%d/%d/%d", &day, &mon, &year) != 3) {
        ESP_LOGE(TAG, "sd_datetime_to_ms_no_tz: parse date=\"%s\" falhou", date_str);
        return false;
    }

    if (sscanf(time_str, "%d:%d:%d", &hh, &mm, &ss) != 3) {
        ESP_LOGE(TAG, "sd_datetime_to_ms_no_tz: parse time=\"%s\" falhou", time_str);
        return false;
    }

    // Algoritmo days_from_civil (Howard Hinnant), sem ajuste de fuso
    int64_t y = year;
    int64_t m = mon;
    int64_t d = day;

    y -= (m <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;                            // [0, 399]
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;    // [0, 146096]
    int64_t days = era * 146097 + doe - 719468;             // dias desde 1970-01-01

    int64_t seconds = days * 86400
                    + hh * 3600
                    + mm * 60
                    + ss;

    *out_ms = seconds * 1000;
    return true;
}

// Helper simples: verifica se a string tem algum dígito/valor mesmo
static bool has_meaningful_value(const char *s)
{
    if (!s) {
        return false;
    }
    // pula espaços
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        s++;
    }
    return (*s != '\0');
}

esp_err_t lte_json_data_payload(char *buf,
                                size_t bufSize,
                                struct record_index_config rec_index,
                                uint32_t *counter_out,
                                uint32_t *cursor_position)
{
    if (!buf || bufSize == 0 || !counter_out || !cursor_position) {
        return ESP_FAIL;
    }

    buf[0] = '\0';
    *counter_out = 0;

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_FAIL;
    }

    // 1) Campos de identificação, iguais ao server_comm.c
    cJSON_AddStringToObject(root, "id", get_device_id());
    cJSON_AddStringToObject(root, "Name", get_name());
    cJSON_AddStringToObject(root, "Serial Number", get_serial_number());
    cJSON_AddStringToObject(root, "Phone", get_phone());
    cJSON_AddNumberToObject(root, "CSQ", get_csq());

    // 1.1) Battery: leitura da FONTE (não da bateria interna)
    battery_monitor_update();  // garante leitura fresca
    float v_src = battery_monitor_get_power_source_voltage();
    float v_src_rounded = roundf(v_src * 100.0f) / 100.0f;

    char bat_str[16];
    snprintf(bat_str, sizeof(bat_str), "%.2f", v_src_rounded);

    ESP_LOGI(TAG,
             "lte_json_data_payload: fonte (Battery) = %s V (raw=%.6f V)",
             bat_str, v_src);

    // Envia como string no root para manter 2 casas decimais
    cJSON_AddStringToObject(root, "Battery", bat_str);

    // 1.2) Credenciais opcionais, igual ao server_comm.c
    if (has_network_user_enabled() && !has_network_http_enabled()) {
        cJSON_AddStringToObject(root, "User", get_network_user());
    }
    if (has_network_pw_enabled() && !has_network_http_enabled()) {
        cJSON_AddStringToObject(root, "Password", get_network_pw());
    }
    if (has_network_token_enabled() && !has_network_http_enabled()) {
        cJSON_AddStringToObject(root, "Token", get_network_token());
    }

    // 2) Preparar array de medições
    cJSON *meas_array = cJSON_CreateArray();
    if (!meas_array) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    cJSON_AddItemToObject(root, "measurements", meas_array);

    // 3) Calcular index inicial (mesma lógica do json_data_payload)
    uint32_t index;
    if (rec_index.total_idx == 0) {
        index = 0;
    } else {
        index = (rec_index.last_read_idx + 1) % rec_index.total_idx;
    }
    *cursor_position = rec_index.cursor_position;

    ESP_LOGD(TAG,
             "LTE_PAYLOAD: total_idx=%u, last_read=%u, last_write=%u, index_inicial=%u, cursor_pos=%u",
             (unsigned)rec_index.total_idx,
             (unsigned)rec_index.last_read_idx,
             (unsigned)rec_index.last_write_idx,
             (unsigned)index,
             (unsigned)*cursor_position);

    bool send_ms = has_timestamp_mode();

    // 4) Loop de leitura e montagem de cada registro
    while (*counter_out < MAX_POINTS_TO_SEND) {
        struct record_data_saved database;

        // read_record_sd atualiza cursor_position conforme rec_index
        if (read_record_sd(cursor_position, &database) != ESP_OK) {
            ESP_LOGW(TAG, "lte_json_data_payload: falha ao ler registro SD no index %u", index);
            break;
        }

        cJSON *rec_obj = cJSON_CreateObject();
        if (!rec_obj) {
            ESP_LOGE(TAG, "lte_json_data_payload: falha ao criar objeto de medição");
            break;
        }

        // --------------------------------------------------------------------
        // DateTime: baseado em database.date ("DD/MM/AAAA") e database.time ("HH:MM:SS")
        // --------------------------------------------------------------------
        int64_t datetime_ms = 0;
        if (!sd_datetime_to_ms_no_tz(database.date, database.time, &datetime_ms)) {
            ESP_LOGE(TAG,
                     "lte_json_data_payload: falha na conversão Data/Hora -> ms (%s %s)",
                     database.date, database.time);
            cJSON_Delete(rec_obj);
            break;
        }

        if (send_ms) {
            // Modo timestamp: DateTime numérico (epoch ms), sem mexer em fuso
            cJSON_AddNumberToObject(rec_obj, "DateTime", (double) datetime_ms);
        } else {
            // Modo string: "AAAA-MM-DDTHH:MM:SS.000-03:00" com a MESMA hora gravada no SD
            int day, mon, year;
            int hh, mm, ss;

            if (sscanf(database.date, "%d/%d/%d", &day, &mon, &year) != 3 ||
                sscanf(database.time, "%d:%d:%d", &hh, &mm, &ss) != 3) {

                ESP_LOGE(TAG,
                         "lte_json_data_payload: falha ao parsear Data/Hora para string ISO (%s %s)",
                         database.date, database.time);
                cJSON_Delete(rec_obj);
                break;
            }

            char datetime_str[40];
            snprintf(datetime_str, sizeof(datetime_str),
                     "%04d-%02d-%02dT%02d:%02d:%02d.000-03:00",
                     year, mon, day, hh, mm, ss);

            cJSON_AddStringToObject(rec_obj, "DateTime", datetime_str);
        }
        
        // --------------------------------------------------------------------
        // Canal: sempre incluído, vem direto da tabela do SD
        // --------------------------------------------------------------------
        cJSON_AddNumberToObject(rec_obj, "Canal", database.channel);        

// --------------------------------------------------------------------
        // Pressao / Vazao
        //
        // Mapeamento definido:
        //   Canal 0 -> Pressão 1
        //   Canal 1 -> Vazão
        //   Canal 2 -> Pressão 2
        //
        // Regras:
        //  - Dentro de measurements só entra:
        //      DateTime, Canal, Pressao (se canal 0 ou 2) e Vazao (se canal 1).
        //  - Se database.data estiver vazio (sem valor), não adiciona Pressao/Vazao.
        // --------------------------------------------------------------------
        if (has_meaningful_value(database.data)) {
            double valor = atof(database.data);

            switch (database.channel) {
            case 0: // Pressão 1
            case 2: // Pressão 2
                cJSON_AddNumberToObject(rec_obj, "Pressao", valor);
                break;

            case 1: // Vazão
                cJSON_AddNumberToObject(rec_obj, "Vazao", valor);
                break;

            default:
                // Canal desconhecido: não adiciona Pressao/Vazao, só DateTime+Canal
                break;
            }
        }
        // Se não tiver valor, fica só DateTime + Canal.

        // Adiciona ao array de medições
        cJSON_AddItemToArray(meas_array, rec_obj);

        (*counter_out)++;

        // Critério de parada: chegou no last_write_idx?
        if (rec_index.last_write_idx == UNSPECIFIC_RECORD) {
            if (index == rec_index.total_idx - 1) {
                break;
            }
        } else {
            if (index == rec_index.last_write_idx) {
                break;
            }
        }

        // garante que não haja divisão por zero
        if (rec_index.total_idx == 0) {
            break;
        }
        index = (index + 1) % rec_index.total_idx;
    }

    // 5) Serializa em buffer
    char *p = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!p) {
        return ESP_FAIL;
    }

    size_t len = strlen(p);
    if (len + 1 > bufSize) {
        ESP_LOGE(TAG,
                 "lte_json_data_payload: buffer insuficiente (%u < %u).",
                 (unsigned)bufSize, (unsigned)(len + 1));
        free(p);
        return ESP_FAIL;
    }

    memcpy(buf, p, len + 1);
    free(p);

    ESP_LOGI(TAG,
             "lte_json_data_payload: enviados %u registros (MAX=%d).",
             (unsigned)(*counter_out), MAX_POINTS_TO_SEND);

    return ESP_OK;
}
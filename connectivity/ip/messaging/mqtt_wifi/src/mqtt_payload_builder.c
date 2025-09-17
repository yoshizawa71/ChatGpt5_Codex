#include "mqtt_payload_builder.h"
#include <string.h>
#include <sys/time.h>
#include "cJSON.h"
#include "esp_log.h"

// === Ajuste o nome do header conforme seu projeto (índices/SD) ===
#include "sdmmc_driver.h"   // precisa fornecer: record_index_config, record_data_saved, read_record_sd(), get_index_config(), save_index_config(), UNSPECIFIC_RECORD

static const char *TAG = "MQTT/BUILDER";

__attribute__((weak)) const char *get_mqtt_ca_pem(void)            { return NULL; }


// Callback para popular "data" do JSON canônico (se usado esse builder)
__attribute__((weak)) int mqtt_payload_fill_data(void *obj) {
    cJSON *data = (cJSON *)obj;
    cJSON_AddNumberToObject(data, "heartbeat", 1);
    return 1;
}

// =================== Utilidades ===================
static int64_t epoch_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000LL + (tv.tv_usec / 1000);
}

static void default_topic(char *out, size_t out_sz) {
    const char *dev = get_device_id();
    if (!dev || !dev[0]) dev = "esp32";
    snprintf(out, out_sz, "devices/%s/telemetry", dev);
}

// =================== Builder genérico (sem SD) ===================
esp_err_t mqtt_payload_build(char *topic_out, size_t topic_sz,
                             char *payload_out, size_t payload_sz)
{
    if (!topic_out || !payload_out || topic_sz == 0 || payload_sz == 0) return ESP_ERR_INVALID_ARG;

    const char *topic_ui = get_mqtt_topic();
    if (topic_ui && topic_ui[0]) snprintf(topic_out, topic_sz, "%s", topic_ui);
    else                         default_topic(topic_out, topic_sz);

    const char *dev = get_device_id(); if (!dev || !dev[0]) dev = "esp32";

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "dev", dev);
    cJSON_AddNumberToObject(root, "ts", epoch_ms());
    cJSON *data = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "data", data);
    (void)mqtt_payload_fill_data((void*)data);

    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) return ESP_ERR_NO_MEM;

    snprintf(payload_out, payload_sz, "%s", txt);
    cJSON_free(txt);

    ESP_LOGI(TAG, "topic=%s payload=%s", topic_out, payload_out);
    return ESP_OK;
}

// =================== Builder a partir do SD (com índice) ===================
#ifndef MAX_POINTS_TO_SEND
#define MAX_POINTS_TO_SEND 10
#endif

esp_err_t mqtt_payload_build_from_sd(char *topic_out,  size_t topic_sz,
                                     char *payload_out,size_t payload_sz,
                                     const struct record_index_config *rec_index_in,
                                     uint32_t *points_out,
                                     uint32_t *cursor_out)
{
    if (!topic_out || !payload_out || !rec_index_in || !points_out || !cursor_out) return ESP_ERR_INVALID_ARG;

    // 1) Tópico
    const char *topic_ui = get_mqtt_topic();
    if (topic_ui && topic_ui[0]) snprintf(topic_out, topic_sz, "%s", topic_ui);
    else                         default_topic(topic_out, topic_sz);

    // 2) Cabeçalho do JSON (metadados úteis)
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    const char *dev = get_device_id(); if (!dev || !dev[0]) dev = "esp32";
    cJSON_AddStringToObject(root, "id", dev);
    if (get_name() && get_name()[0])             cJSON_AddStringToObject(root, "Nome", get_name());
    if (get_serial_number() && get_serial_number()[0]) cJSON_AddStringToObject(root, "Número Serial", get_serial_number());
    if (get_phone() && get_phone()[0])           cJSON_AddStringToObject(root, "Telefone", get_phone());
    cJSON_AddNumberToObject(root, "CSQ", get_csq());

    if (has_network_user_enabled())  cJSON_AddStringToObject(root, "Usuario", get_network_user());
    if (has_network_pw_enabled())    cJSON_AddStringToObject(root, "Senha",   get_network_pw());
    if (has_network_token_enabled()) cJSON_AddStringToObject(root, "Token",   get_network_token());

    cJSON *meas_array = cJSON_CreateArray();
    if (!meas_array) { cJSON_Delete(root); return ESP_ERR_NO_MEM; }
    cJSON_AddItemToObject(root, "measurements", meas_array);

    // 3) Varredura da tabela
    struct record_index_config rec = *rec_index_in; // trabalhamos numa cópia
    uint32_t produced = 0;
    uint32_t idx;

    if (rec.total_idx == 0) idx = 0;
    else                    idx = (rec.last_read_idx + 1) % rec.total_idx;

    *cursor_out = rec.cursor_position;

    while (produced < MAX_POINTS_TO_SEND) {
        struct record_data_saved db; // do seu header sdcard_mmc.h

        if (read_record_sd(cursor_out, &db) != ESP_OK) {
            ESP_LOGW(TAG, "Falha ao ler registro SD (idx=%u)", idx);
            break;
        }

        cJSON *rec_obj = cJSON_CreateObject();
        if (!rec_obj) { ESP_LOGE(TAG, "Erro cJSON obj"); break; }

        cJSON_AddStringToObject(rec_obj, "Data",  db.date);
        cJSON_AddStringToObject(rec_obj, "Hora",  db.time);
        cJSON_AddNumberToObject(rec_obj, "Canal", db.channel);
        cJSON_AddNumberToObject(rec_obj, "Dados", atof(db.data));
        cJSON_AddItemToArray(meas_array, rec_obj);

        produced++;

        // Paramos ao alcançar o last_write_idx (se especificado)
        if (rec.last_write_idx == UNSPECIFIC_RECORD) {
            if (idx == rec.total_idx - 1) break;
        } else {
            if (idx == rec.last_write_idx) break;
        }

        if (rec.total_idx == 0) break;
        idx = (idx + 1) % rec.total_idx;
    }

    *points_out = produced;

    // 4) Serializar
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) return ESP_ERR_NO_MEM;

    size_t len = strlen(txt);
    if (len + 1 > payload_sz) { cJSON_free(txt); return ESP_ERR_NO_MEM; }
    memcpy(payload_out, txt, len + 1);
    cJSON_free(txt);

    ESP_LOGI(TAG, "topic=%s points=%u cursor=%u", topic_out, (unsigned)produced, (unsigned)*cursor_out);
    return ESP_OK;
}

// =================== UBIDOTS ===================
// Mapeamento padrão dos seus canais (sem acento):
const char *ubidots_label_for_channel(int canal) {
    switch (canal) {
        case 0: return "pressao_1";
        case 1: return "vazao";
        case 2: return "pressao_2";
        default: return "canal";
    }
}

// Converte "DD/MM/YYYY" + "HH:MM:SS" -> epoch ms (baseado no timezone local do ESP)
static int64_t to_epoch_ms_from_date_time(const char *date_ddmmyyyy, const char *time_hhmmss) {
    if (!date_ddmmyyyy || !time_hhmmss) return 0;
    int d=0,m=0,y=0,hh=0,mm=0,ss=0;
    if (sscanf(date_ddmmyyyy, "%d/%d/%d", &d,&m,&y) != 3) return 0;
    if (sscanf(time_hhmmss, "%d:%d:%d", &hh,&mm,&ss) != 3) return 0;

    struct tm t = {0};
    t.tm_mday = d;
    t.tm_mon  = m - 1;
    t.tm_year = y - 1900;
    t.tm_hour = hh;
    t.tm_min  = mm;
    t.tm_sec  = ss;
    t.tm_isdst = -1;

    time_t sec = mktime(&t);   // interpreta como hora local do dispositivo
    if (sec <= 0) return 0;
    return ((int64_t)sec) * 1000LL;
}

esp_err_t mqtt_payload_build_from_sd_ubidots(char *topic_out,  size_t topic_sz,
                                             char *payload_out,size_t payload_sz,
                                             const struct record_index_config *rec_index_in,
                                             uint32_t *points_out,
                                             uint32_t *cursor_out)
{
    if (!topic_out || !payload_out || !rec_index_in || !points_out || !cursor_out)
        return ESP_ERR_INVALID_ARG;

    // 1) Tópico
    const char *topic_ui = get_mqtt_topic();
    if (topic_ui && topic_ui[0]) {
        snprintf(topic_out, topic_sz, "%s", topic_ui);   // ex.: /v1.6/devices/Smart_data
    } else {
        // fallback: monta com device_id minúsculo/ASCII seguro
        const char *dev = get_device_id(); if (!dev || !dev[0]) dev = "esp32";
        char label[64]; size_t j=0;
        for (size_t i=0; dev[i] && j<sizeof(label)-1; ++i) {
            char c = dev[i];
            if (c==' ') c='_';
            if (c>='A' && c<='Z') c = (char)(c - 'A' + 'a');
            label[j++] = ((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-') ? c : '_';
        }
        label[j]=0;
        snprintf(topic_out, topic_sz, "/v1.6/devices/%s", label);
    }

    // 2) Pegar 1 registro do SD (Ubidots: 1 variável por publish)
    struct record_index_config rec = *rec_index_in;
    *cursor_out = rec.cursor_position;

    struct record_data_saved db;
    if (read_record_sd(cursor_out, &db) != ESP_OK) {
        return ESP_FAIL;
    }

    const char *var = ubidots_label_for_channel(db.channel);
    if (!var || !var[0]) var = "canal";

    int64_t ts_ms = to_epoch_ms_from_date_time(db.date, db.time);
    double   val  = atof(db.data);

    // 3) JSON com snprintf (locale C garante ponto decimal)
    // {"pressao_1":{"value":6.7,"timestamp":1757...}}
    int n = snprintf(payload_out, payload_sz,
                     "{\"%s\":{\"value\":%.6g,\"timestamp\":%lld}}",
                     var, val, (long long)ts_ms);
    if (n <= 0 || (size_t)n >= payload_sz) {
        return ESP_ERR_NO_MEM;
    }

    if (points_out) *points_out = 1;

    ESP_LOGI("MQTT/BUILDER", "[Ubidots] topic=%s label=%s ts=%lld value=%g",
             topic_out, var, (long long)ts_ms, val);

    return ESP_OK;
}

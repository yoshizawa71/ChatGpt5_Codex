/*
 * payload_builder.c
 *
 *  Created on: 7 de out. de 2025
 *      Author: geopo
 */


#include "payload_builder.h"
#include <string.h>
#include <sys/time.h>
#include "cJSON.h"
#include "esp_log.h"
#include <stdlib.h>
#include "payload_time.h"
// === Ajuste o nome do header conforme seu projeto (índices/SD) ===
#include "sdmmc_driver.h"   // precisa fornecer: record_index_config, record_data_saved, read_record_sd(), get_index_config(), save_index_config(), UNSPECIFIC_RECORD

// =================== Builder a partir do SD (com índice) ===================
#ifndef MAX_POINTS_TO_SEND
#define MAX_POINTS_TO_SEND 10
#endif

static const char *TAG = "MQTT/BUILDER";

__attribute__((weak)) const char *get_mqtt_ca_pem(void)            { return NULL; }


// Callback para popular "data" do JSON canônico (se usado esse builder)
__attribute__((weak)) int mqtt_payload_fill_data(void *obj) {
    cJSON *data = (cJSON *)obj;
    cJSON_AddNumberToObject(data, "heartbeat", 1);
    return 1;
}

static inline int is_date_char(int c){ return (c>='0'&&c<='9') || c=='/' || c=='-'; }
static inline int is_time_char(int c){ return (c>='0'&&c<='9') || c==':'; }
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

// Extrai "dd/mm/aaaa" ou similar sem confiar em '\0' do SD
static void extract_date_like(const char *src, char *dst, size_t dstsz){
    size_t w=0;
    if (!src || dstsz==0){ if(dstsz) dst[0]=0; return; }
    // lê no máx. 24 bytes do buffer de origem
    for (size_t i=0; i<24 && w+1<dstsz; ++i){
        unsigned char c = (unsigned char)src[i];
        if (c==0) break;
        if (is_date_char(c)) dst[w++]=(char)c;
        else if (w) break;
    }
    dst[w]=0;
}

// Extrai "hh:mm:ss" ou similar
static void extract_time_like(const char *src, char *dst, size_t dstsz){
    size_t w=0;
    if (!src || dstsz==0){ if(dstsz) dst[0]=0; return; }
    for (size_t i=0; i<16 && w+1<dstsz; ++i){
        unsigned char c = (unsigned char)src[i];
        if (c==0) break;
        if (is_time_char(c)) dst[w++]=(char)c;
        else if (w) break;
    }
    dst[w]=0;
}

// Converte canal bruto em (canal base, subcanal)
static void split_channel(int raw, int *canal_base, int *sub){
    *canal_base = raw;
    *sub = 0;

    // Mapas comuns para "3.1, 3.2, 3.3"
    if (raw>=31 && raw<=39){ *canal_base = 3; *sub = raw-30; return; }
    if (raw>=301 && raw<=309){ *canal_base = 3; *sub = raw-300; return; }

    // OPCIONAL: números de 2 dígitos X Y ⇒ X.Y
    if (raw>=10 && raw<=99){
        int b = raw/10, s = raw%10;
        if (b>=1 && b<=9 && s>=1 && s<=9){ *canal_base=b; *sub=s; return; }
    }
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


// Ponte fraca: permita mapear subcanais (ex.: fase A/B/C da energia)
__attribute__((weak))
int mqtt_subchannel_for(int canal, const struct record_data_saved *rec)
{
    (void)rec;
    // Padrão: sem subcanal (0). Você pode sobrescrever em outro TU.
    return 0;
}
// ============================================================================


// =================== Builder a partir do SD (com índice) ===================

esp_err_t mqtt_payload_build_from_sd(char *topic_out,  size_t topic_sz,
                                     char *payload_out,size_t payload_sz,
                                     const struct record_index_config *rec_index_in,
                                     uint32_t *points_out,
                                     uint32_t *cursor_out)
{
    if (!topic_out || !payload_out || !rec_index_in || !points_out || !cursor_out) {
        return ESP_ERR_INVALID_ARG;
    }

    // 1) Tópico
    const char *topic_ui = get_mqtt_topic();
    if (topic_ui && topic_ui[0]) snprintf(topic_out, topic_sz, "%s", topic_ui);
    else                         default_topic(topic_out, topic_sz);

    // 2) Cabeçalho do payload
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    const char *dev = get_device_id(); if (!dev || !dev[0]) dev = "esp32";
    cJSON_AddStringToObject(root, "id", dev);
    if (get_name() && get_name()[0])                   cJSON_AddStringToObject(root, "Nome", get_name());
    if (get_serial_number() && get_serial_number()[0]) cJSON_AddStringToObject(root, "Número Serial", get_serial_number());
    if (get_phone() && get_phone()[0])                 cJSON_AddStringToObject(root, "Telefone", get_phone());
    cJSON_AddNumberToObject(root, "CSQ", get_csq());

    if (has_network_user_enabled())  cJSON_AddStringToObject(root, "Usuario", get_network_user());
    if (has_network_pw_enabled())    cJSON_AddStringToObject(root, "Senha",   get_network_pw());
    if (has_network_token_enabled()) cJSON_AddStringToObject(root, "Token",   get_network_token());

    cJSON *meas_array = cJSON_CreateArray();
    if (!meas_array) { cJSON_Delete(root); return ESP_ERR_NO_MEM; }
    cJSON_AddItemToObject(root, "measurements", meas_array);

    // 3) Varredura do SD
    *cursor_out = rec_index_in->cursor_position;
    *points_out = 0;

    for (int i = 0; i < MAX_POINTS_TO_SEND; ++i) {
        struct record_data_saved db;
        if (read_record_sd(cursor_out, &db) != ESP_OK) break;

        // Mapeia Canal/Subcanal (31..39 -> 3.1..3.9; fallback XY -> X.Y)
        int ch   = (int)db.channel;
        int base = ch;
        int sub  = 0;

        if (ch >= 31 && ch <= 39) {          // 31..39 => 3.1..3.9
            base = 3;
            sub  = ch - 30;
        } else if (ch >= 10 && ch <= 99) {   // XY => X.Y (genérico)
            base = ch / 10;
            sub  = ch % 10;
        }

        cJSON *rec = cJSON_CreateObject();
        if (!rec) { ESP_LOGE(TAG, "cJSON_CreateObject falhou"); break; }

        // db.date/time já vêm limpos do read_record_sd()
        cJSON_AddStringToObject(rec, "Data",  db.date);
        cJSON_AddStringToObject(rec, "Hora",  db.time);
        PAYLOAD_ADD_TIME(rec, db.date, db.time);
        cJSON_AddNumberToObject(rec, "Canal", base);
        if (sub > 0) {                       // <— só envia quando existir subcanal
            cJSON_AddNumberToObject(rec, "Subcanal", sub);
        }
        cJSON_AddNumberToObject(rec, "Dados", atof(db.data));

        cJSON_AddItemToArray(meas_array, rec);
        (*points_out)++;
    }

    // 4) Serialização
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) return ESP_ERR_NO_MEM;

    size_t need = strlen(txt) + 1;
    if (need > payload_sz) {
        ESP_LOGE(TAG, "Payload insuficiente: need=%u have=%u", (unsigned)need, (unsigned)payload_sz);
        cJSON_free(txt);
        return ESP_ERR_NO_MEM;
    }
    memcpy(payload_out, txt, need);
    cJSON_free(txt);

    return ESP_OK;
}

// =================== UBIDOTS ===================
// Labels de exemplo — ajuste aos nomes que você usa no Ubidots.
const char *ubidots_label_for_channel(int canal) {
    switch (canal) {
        case 0:  return "pressao_1";
        case 1:  return "vazao";
        case 2:  return "pressao_2";
        case 31: return "energia_f1";   // 3.1
        case 32: return "energia_f2";   // 3.2
        case 33: return "energia_f3";   // 3.3
        default: return NULL;           // deixa o builder criar um label dinâmico
    }
}


// Converte "DD/MM/YYYY" + "HH:MM:SS" -> epoch ms (baseado no timezone local do ESP)
static inline void apply_tz_brt_once(void){
    static bool s_done = false;
    if (!s_done) {
        setenv("TZ", "<-03>3", 1);
        tzset();
        s_done = true;
    }
}

static int64_t to_epoch_ms_from_date_time(const char *date_ddmmyyyy, const char *time_hhmmss)
{
    apply_tz_brt_once();  // <— garante TZ antes do mktime (idempotente)

    if (!date_ddmmyyyy || !time_hhmmss) return 0;
    int d=0,m=0,y=0,hh=0,mm=0,ss=0;
    if (sscanf(date_ddmmyyyy, "%d/%d/%d", &d,&m,&y) != 3) return 0;
    if (sscanf(time_hhmmss, "%d:%d:%d", &hh,&mm,&ss) != 3) return 0;

    struct tm t = {0};
    t.tm_mday = d;  t.tm_mon  = m-1;  t.tm_year = y-1900;
    t.tm_hour = hh; t.tm_min  = mm;   t.tm_sec  = ss;
    t.tm_isdst = -1;                      // deixa o mktime decidir (sem DST)

    time_t sec = mktime(&t);              // interpreta como HORA LOCAL (TZ aplicado)
    if (sec == (time_t)-1) return 0;

    // Debug opcional:
    // ESP_LOGI("TIME", "%s %s -> epoch=%lld local=%s",
    //          date_ddmmyyyy, time_hhmmss, (long long)sec, asctime(localtime(&sec)));

    return (int64_t)sec * 1000LL;         // epoch UTC em ms
}

// ----------------------------------------------------------------------------
// Helpers locais para o builder do Ubidots
// ----------------------------------------------------------------------------

// Mapeia 31..39 -> base=3, sub=1..9; fallback XY (10..99) -> X.Y
static inline void ubidots_split_channel(int ch, int *base, int *sub)
{
    *base = ch;
    *sub  = 0;

    if (ch >= 31 && ch <= 39) {          // 31..39 => 3.1..3.9
        *base = 3;
        *sub  = ch - 30;
    } else if (ch >= 10 && ch <= 99) {   // XY => X.Y (genérico)
        *base = ch / 10;
        *sub  = ch % 10;
    }
}

// Gera um label padrão para a variável (pode trocar pelos nomes oficiais do seu Ubidots)
static inline void ubidots_make_var_label(int ch, char out[], size_t out_sz)
{
    int base, sub;
    ubidots_split_channel(ch, &base, &sub);
    if (sub > 0)  snprintf(out, out_sz, "canal_%d_%d", base, sub);
    else          snprintf(out, out_sz, "canal_%d", base);
}

// Normaliza device_id para montar o tópico do Ubidots (minúsculo/ASCII seguro)
static inline void make_ubidots_topic_from_device(char *dst, size_t dstsz)
{
    const char *dev = get_device_id(); if (!dev || !dev[0]) dev = "esp32";
    char label[64]; size_t j=0;
    for (size_t i=0; dev[i] && j<sizeof(label)-1; ++i) {
        char c = dev[i];
        if (c==' ') c = '_';
        if (c>='A' && c<='Z') c = (char)(c - 'A' + 'a');
        label[j++] = ((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-') ? c : '_';
    }
    label[j] = 0;
    snprintf(dst, dstsz, "/v1.6/devices/%s", label);
}

// ----------------------------------------------------------------------------
// UBIDOTS: 1 variável por publish
// ----------------------------------------------------------------------------

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
    if (topic_ui && topic_ui[0]) snprintf(topic_out, topic_sz, "%s", topic_ui);
    else                         make_ubidots_topic_from_device(topic_out, topic_sz);

    // 2) Um registro por publish
    struct record_index_config rec = *rec_index_in;
    *cursor_out = rec.cursor_position;

    struct record_data_saved db;
    if (read_record_sd(cursor_out, &db) != ESP_OK) return ESP_FAIL;

    // 3) Label (mapa -> fallback)
    char var[32];
    const char *mapped = ubidots_label_for_channel((int)db.channel);
    if (mapped) snprintf(var, sizeof(var), "%s", mapped);
    else        ubidots_make_var_label((int)db.channel, var, sizeof(var));

    // 4) Valor e timestamp
/*    double   val   = atof(db.data);
    int64_t  ts_ms = to_epoch_ms_from_date_time(db.date, db.time);
    if (ts_ms <= 0) { time_t now = time(NULL); if (now > 0) ts_ms = (int64_t)now * 1000LL; }*/
double  val = atof(db.data);
//int64_t ts_ms = local_date_time_to_utc_ms(db.date, db.time);
int64_t ts_ms = PAYLOAD_TS_FROM_SD(db.date, db.time);
if (ts_ms <= 0) ts_ms = epoch_ms_utc();

char    ts_iso[25];
int64_t ts_s = ts_ms / 1000LL;
iso8601_utc_from_ms(ts_ms, ts_iso, sizeof ts_iso);  // <-- formata a partir do MESMO timestamp

    // 5) Payload Ubidots
/*    int n = snprintf(payload_out, payload_sz,
                     "{\"%s\":{\"value\":%.6g,\"timestamp\":%lld}}",
                     var, val, (long long)ts_ms);*/
                     
     int n = snprintf(payload_out, payload_sz,
                 "{\"%s\":{\"value\":%.6g,\"timestamp\":%lld}}",
                 var, val, (long long)ts_ms);
                           
    if (n <= 0 || (size_t)n >= payload_sz) return ESP_ERR_NO_MEM;

    if (points_out) *points_out = 1;
   ESP_LOGI("MQTT/BUILDER",
         "[Ubidots] topic=%s var=%s ts_ms=%lld (%s) val=%g",
         topic_out, var, (long long)ts_ms, ts_iso, val);

    return ESP_OK;
}

// ======== WEGNOLOGY/LOSANT ========
// Tópico padrão: wnology/<DEVICE_ID>/state
static inline void make_weg_topic_from_device(char *dst, size_t dstsz) {
    const char *dev = get_device_id(); if (!dev || !dev[0]) dev = "esp32";
    snprintf(dst, dstsz, "wnology/%s/state", dev);
}

/* Lê do SD e monta um único "state" com i_a/i_b/i_c.
   Avança o cursor conforme os registros consumidos. */
// Novo builder WEG – Energia (i_a, i_b, i_c) + pulse_count (canal 1)
esp_err_t mqtt_payload_build_from_sd_weg_energy(char *topic_out,  size_t topic_sz,
                                                char *payload_out, size_t payload_sz,
                                                const struct record_index_config *rec_index_in,
                                                uint32_t *points_out,
                                                uint32_t *cursor_out)
{
    if (!topic_out || !payload_out || !rec_index_in || !points_out || !cursor_out) {
        return ESP_ERR_INVALID_ARG;
    }

    // 1) Tópico
    const char *topic_ui = get_mqtt_topic();
    if (topic_ui && topic_ui[0]) {
        snprintf(topic_out, topic_sz, "%s", topic_ui);
    } else {
        // "wnology/<DEVICE_ID>/state"
        make_weg_topic_from_device(topic_out, topic_sz);
    }

    // 2) Varre o SD e coleta um "snapshot"
    struct record_index_config rec = *rec_index_in;
    *cursor_out = rec.cursor_position;

    bool     have_a     = false;
    bool     have_b     = false;
    bool     have_c     = false;
    bool     have_pulse = false;   // <<< NOVO
    double   val_a      = 0.0;
    double   val_b      = 0.0;
    double   val_c      = 0.0;
    double   val_pulse  = 0.0;     // <<< NOVO
    int64_t  ts_ms      = 0;       // manteremos o MAIS RECENTE em UTC (ms)
    uint32_t consumed   = 0;

    for (int i = 0; i < MAX_POINTS_TO_SEND; ++i) {
        struct record_data_saved db;
        if (read_record_sd(cursor_out, &db) != ESP_OK) {
            break;
        }
        consumed++;

        int base = 0, sub = 0;
        split_channel((int)db.channel, &base, &sub);  // mapeia 31.39 -> 3.1.3.9

        if (base == 3) {
            // 3.x → correntes
            if      (sub == 1) { val_a = atof(db.data); have_a = true; }
            else if (sub == 2) { val_b = atof(db.data); have_b = true; }
            else if (sub == 3) { val_c = atof(db.data); have_c = true; }

            // timestamp do registro (do SD) → UTC (ms)
       //     int64_t t = local_date_time_to_utc_ms(db.date, db.time);
              int64_t t = PAYLOAD_TS_FROM_SD(db.date, db.time);
            if (t > ts_ms) ts_ms = t;
        }
        else if (base == 1) {
            // <<< NOVO: canal 1 = contador/pulsos >>>
            // no seu SD está assim: "canal 1   valor 6"
            val_pulse  = atof(db.data);
            have_pulse = true;

            // garante que se o pulso for o mais recente, o time siga ele
          //  int64_t t = local_date_time_to_utc_ms(db.date, db.time);
              int64_t t = PAYLOAD_TS_FROM_SD(db.date, db.time);
            if (t > ts_ms) ts_ms = t;
        }

        // se já temos tudo que nos interessa, podemos sair mais cedo
        if (have_a && have_b && have_c && have_pulse) {
            break;
        }
    }

    // Nada útil para enviar (isso aqui quase nunca vai acontecer porque ao menos 3.x você está mandando)
    if (!have_a && !have_b && !have_c && !have_pulse) {
        *points_out = 0;
        return ESP_OK;
    }

    // Fallback 100% UTC se o timestamp não veio do SD
    if (ts_ms <= 0) {
        ts_ms = epoch_ms_utc();
    }

    // 3) Monta o "state" da WEGnology
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    // Campo obrigatório da plataforma: epoch em milissegundos (UTC)
    cJSON_AddNumberToObject(root, "time", (double)ts_ms);

    // Opcional: metadata com carimbos em UTC (seguro para ingestion e útil para auditoria)
    {
        cJSON *meta = cJSON_CreateObject();
        if (meta) {
            char ts_iso[25];
            iso8601_utc(ts_iso, sizeof ts_iso);            // "YYYY-MM-DDTHH:MM:SSZ"
            cJSON_AddStringToObject(meta, "ts_iso", ts_iso);
            cJSON_AddNumberToObject(meta, "ts_s", (double)(ts_ms / 1000));
            cJSON_AddItemToObject(root, "metadata", meta);
        }
    }

    // Dados do snapshot
    cJSON *data = cJSON_CreateObject();
    if (!data) { cJSON_Delete(root); return ESP_ERR_NO_MEM; }
    cJSON_AddItemToObject(root, "data", data);

    if (have_a)     cJSON_AddNumberToObject(data, "i_a",        val_a);
    if (have_b)     cJSON_AddNumberToObject(data, "i_b",        val_b);
    if (have_c)     cJSON_AddNumberToObject(data, "i_c",        val_c);
    if (have_pulse) cJSON_AddNumberToObject(data, "pulse_count", val_pulse);   // <<< NOVO

    // Serializa para o buffer de saída
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) return ESP_ERR_NO_MEM;

    size_t need = strlen(txt) + 1;
    if (need > payload_sz) {
        cJSON_free(txt);
        return ESP_ERR_NO_MEM;
    }
    memcpy(payload_out, txt, need);
    cJSON_free(txt);

    // quantos registros do SD foram consumidos neste ciclo
    *points_out = consumed;
    return ESP_OK;
}

// Novo builder WEG – Water Meter (press_1, press_2, flow)
esp_err_t mqtt_payload_build_from_sd_weg_water(char *topic_out,  size_t topic_sz,
                                               char *payload_out,size_t payload_sz,
                                               const struct record_index_config *rec_index_in,
                                               uint32_t *points_out,
                                               uint32_t *cursor_out)
{
    if (!topic_out || !payload_out || !rec_index_in || !points_out || !cursor_out)
        return ESP_ERR_INVALID_ARG;

    // 1) Tópico (mesma lógica que você já usa)
    const char *topic_ui = get_mqtt_topic();
    if (topic_ui && topic_ui[0]) {
        snprintf(topic_out, topic_sz, "%s", topic_ui);
    } else {
        make_weg_topic_from_device(topic_out, topic_sz); // "wnology/<DEVICE_ID>/state"
    }

    // 2) Varre o SD e coleta snapshot de pressão/fluxo
    struct record_index_config rec = *rec_index_in;
    *cursor_out = rec.cursor_position;

    bool     have_p1 = false, have_p2 = false, have_flow = false;
    double   val_p1  = 0.0,   val_p2  = 0.0,   val_flow = 0.0;
    int64_t  ts_ms   = 0;     // mais recente observado entre os registros
    uint32_t consumed = 0;

    for (int i = 0; i < MAX_POINTS_TO_SEND; ++i) {
        struct record_data_saved db;
        if (read_record_sd(cursor_out, &db) != ESP_OK) break;
        consumed++;

        // ===== MAPA DE CANAIS =====
        // ajuste aqui se seus canais forem outros
        if      ((int)db.channel == 0) { val_p1   = atof(db.data); have_p1   = true; }
        else if ((int)db.channel == 2) { val_p2   = atof(db.data); have_p2   = true; }
        else if ((int)db.channel == 1) { val_flow = atof(db.data); have_flow = true; }

        // timestamp (UTC) do registro, fica com o mais recente
     //   int64_t t = local_date_time_to_utc_ms(db.date, db.time);
        int64_t t = PAYLOAD_TS_FROM_SD(db.date, db.time);
        if (t > ts_ms) ts_ms = t;

        // snapshot completo alcançado? pode parar cedo
        if (have_p1 && have_p2 && have_flow) break;
    }

    // Nada relevante para enviar
    if (!have_p1 && !have_p2 && !have_flow) {
        *points_out = 0;
        return ESP_OK;
    }

    // Fallback: carimbo 100% UTC (sem depender de TZ local)
    if (ts_ms <= 0) ts_ms = epoch_ms_utc();

    // 3) Monta o "state" da WEGnology/Losant
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    // Campo obrigatório para WEG/Losant: sempre em ms UTC
    cJSON_AddNumberToObject(root, "time", ts_ms);

    // Carimbos extras (opc.: úteis no debug/traço)
    {
        char ts_iso[25];
        iso8601_utc(ts_iso, sizeof ts_iso);              // "YYYY-MM-DDTHH:MM:SSZ"
        cJSON_AddStringToObject(root, "timestamp", ts_iso);
        cJSON_AddNumberToObject(root, "ts_ms", (double)ts_ms);
    }

    cJSON *data = cJSON_CreateObject();
    if (!data) { cJSON_Delete(root); return ESP_ERR_NO_MEM; }
    cJSON_AddItemToObject(root, "data", data);

    // ===== ATRIBUTOS DO DEVICE (precisam existir lá) =====
    if (have_p1)   cJSON_AddNumberToObject(data, "press_1", val_p1);
    if (have_p2)   cJSON_AddNumberToObject(data, "press_2", val_p2);
    if (have_flow) cJSON_AddNumberToObject(data, "flow",    val_flow);

    // Serializa
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) return ESP_ERR_NO_MEM;

    size_t need = strlen(txt) + 1;
    if (need > payload_sz) { cJSON_free(txt); return ESP_ERR_NO_MEM; }
    memcpy(payload_out, txt, need);
    cJSON_free(txt);

    *points_out = consumed; // quantos registros do SD foram consumidos
    ESP_LOGI("MQTT/BUILDER", "[WEG/WATER] topic=%s ts_ms=%lld press_1=%g press_2=%g flow=%g",
             topic_out, (long long)ts_ms, val_p1, val_p2, val_flow);
    return ESP_OK;
}





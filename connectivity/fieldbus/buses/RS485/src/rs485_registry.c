/*
 * rs485_registry.c — Registro e dispatcher de sensores RS-485 (baixo acoplamento)
 * - mapeia strings ⇄ enums (alinhado ao front)
 * - leitura centralizada chamando drivers específicos
 * - varredura de drivers (probe) para identificar o tipo a partir do endereço
 */

#include "rs485_registry.h"
#include "datalogger_driver.h" 
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdbool.h>
#include "xy_md02_driver.h"              // temperature_rs485_probe/read  (XY-MD02)
#include "energy_jsy_mk_333_driver.h"    // jsy_mk333_probe               (JSY-MK-333)

/*typedef struct {
    rs485_sensor_t *out;
    size_t max;
    int filled;
} _snap_ctx_t;
*/

extern esp_err_t energy_meter_read_currents(uint8_t addr, float outI[3]);

/* ---------------- utils ---------------- */
static bool streq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
static bool streq_ci(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if ((unsigned char)tolower(*a) != (unsigned char)tolower(*b)) return false;
        ++a; ++b;
    }
    return *a == *b;
}

/* ---------------- string -> enum ---------------- */
rs485_type_t rs485_type_from_str(const char *s) {
    if (!s || !*s) return RS485_TYPE_INVALID;

    if (streq_ci(s, "energia"))             return RS485_TYPE_ENERGIA;

    if (streq_ci(s, "termohigrometro") ||
        streq_ci(s, "termo-higrometro") ||
        streq_ci(s, "termohigrômetro") ||
        streq_ci(s, "termo-higrômetro"))    return RS485_TYPE_TERMOHIGRO;

    if (streq_ci(s, "temperatura"))         return RS485_TYPE_TEMPERATURA;
    if (streq_ci(s, "umidade"))             return RS485_TYPE_UMIDADE;

    if (streq_ci(s, "pressao") || streq_ci(s, "pressão"))
                                            return RS485_TYPE_PRESSAO;

    if (streq_ci(s, "vazao")   || streq_ci(s, "vazão") || streq_ci(s, "fluxo"))
                                            return RS485_TYPE_FLUXO;

    if (streq_ci(s, "gps"))                 return RS485_TYPE_GPS;
    if (streq_ci(s, "luz"))                 return RS485_TYPE_LUZ;

    if (streq_ci(s, "gas") || streq_ci(s, "gás"))
                                            return RS485_TYPE_GAS;

    if (streq_ci(s, "outro") || streq_ci(s, "outros"))
                                            return RS485_TYPE_OUTRO;

    return RS485_TYPE_INVALID;
}

/* Canoniza p/ nomes do front */
const char* rs485_type_to_str(rs485_type_t t) {
    switch (t) {
        case RS485_TYPE_ENERGIA:     return "energia";
        case RS485_TYPE_TERMOHIGRO:  return "termohigrometro";
        case RS485_TYPE_TEMPERATURA: return "temperatura";
        case RS485_TYPE_UMIDADE:     return "umidade";
        case RS485_TYPE_PRESSAO:     return "pressao";
        case RS485_TYPE_FLUXO:       return "vazao";
        case RS485_TYPE_GPS:         return "gps";
        case RS485_TYPE_LUZ:         return "luz";
        case RS485_TYPE_GAS:         return "gas";
        case RS485_TYPE_OUTRO:       return "outro";
        default:                     return "";
    }
}

rs485_subtype_t rs485_subtype_from_str(const char *s) {
    if (!s) return RS485_SUBTYPE_NONE;
    if (streq_ci(s, "monofasico") || streq_ci(s, "monofásico")) return RS485_SUBTYPE_MONOFASICO;
    if (streq_ci(s, "trifasico")  || streq_ci(s, "trifásico"))  return RS485_SUBTYPE_TRIFASICO;
    return RS485_SUBTYPE_NONE;
}

const char* rs485_subtype_to_str(rs485_subtype_t st) {
    switch (st) {
        case RS485_SUBTYPE_MONOFASICO: return "monofasico";
        case RS485_SUBTYPE_TRIFASICO:  return "trifasico";
        default: return "";
    }
}

// Abaixo pego type/subtype “do jeito que você já armazena” (ajuste os getters se o nome divergir).
/*static bool _snap_iter_cb(uint8_t channel, uint8_t addr, void *user)
{
    _snap_ctx_t *ctx = (_snap_ctx_t*)user;
    if (!ctx || !ctx->out || ctx->filled >= (int)ctx->max) return false;

    rs485_sensor_t *dst = &ctx->out[ctx->filled];
    dst->channel = channel;
    dst->address = addr;

    // ---> AJUSTE ESTES GETTERS se seus nomes forem diferentes <---
    dst->type    = rs485_registry_get_channel_type(channel);
    dst->subtype = rs485_registry_get_channel_subtype(channel);
    // --------------------------------------------------------------

    ctx->filled++;
    return true; // continua
}*/

/*int rs485_registry_get_snapshot(rs485_sensor_t *out, size_t max)
{
    if (!out || max == 0) return -1;
    _snap_ctx_t ctx = { .out = out, .max = max, .filled = 0 };
    int tot = rs485_registry_iterate_configured(_snap_iter_cb, &ctx);
    // tot = total visitado; ctx.filled = quantos realmente colocamos (limitado por max)
    return (tot < 0) ? tot : ctx.filled;
}*/



/* --------------- Dispatcher de leitura ---------------
 * Retorna >=0 (# medições escritas em out) ou <0 em erro.
 */
int rs485_read_measurements(const rs485_sensor_t *sensor,
                            rs485_measurement_t *out, size_t out_len)
{
    if (!sensor || !out || out_len == 0) return -1;

    switch (sensor->type) {

        case RS485_TYPE_UMIDADE:     
        case RS485_TYPE_TEMPERATURA:
        case RS485_TYPE_TERMOHIGRO:
         {
            float t_c = 0.0f, rh = 0.0f;
            bool  has_hum = false;
/*
             Driver XY-MD02: lê T (e UR se houver) do endereço informado 
            int r = temperature_rs485_read((uint8_t)sensor->address, &t_c, &rh, &has_hum);
            if (r != ESP_OK) return -2;

            int wr = 0;

            if (sensor->type == RS485_TYPE_TERMOHIGRO || sensor->type == RS485_TYPE_TEMPERATURA) {
                if (wr < (int)out_len) {
                    out[wr++] = (rs485_measurement_t){
                        .channel = sensor->channel,
                        .kind    = RS485_MEAS_TEMP_C,
                        .value   = t_c
                    };
                }
            }

            if ((sensor->type == RS485_TYPE_TERMOHIGRO || sensor->type == RS485_TYPE_UMIDADE) && has_hum) {
                if (wr < (int)out_len) {
#if TH_HUM_SAME_CHANNEL
                    out[wr++] = (rs485_measurement_t){
                        .channel = sensor->channel,
                        .kind    = RS485_MEAS_HUM_PCT,
                        .value   = rh
                    };
#else
                    out[wr++] = (rs485_measurement_t){
                        .channel = (uint16_t)(sensor->channel + 1),
                        .kind    = RS485_MEAS_HUM_PCT,
                        .value   = rh
                    };
#endif
                }
            }

            if (sensor->type == RS485_TYPE_UMIDADE && !has_hum) return -4;

            return wr;
        }*/
        /* Driver XY-MD02: lê T (e UR se houver) do endereço informado */
            esp_err_t e = temperature_rs485_read((uint8_t)sensor->address, &t_c, &rh, &has_hum);
            if (e != ESP_OK) return -2;  /* leitura falhou */

            int wr = 0;

            /* Publica TEMPERATURA sempre que o tipo NÃO for “só umidade” */
            if (sensor->type != RS485_TYPE_UMIDADE && wr < (int)out_len) {
                out[wr++] = (rs485_measurement_t){
                    .channel = sensor->channel,
                    .kind    = RS485_MEAS_TEMP_C,
                    .value   = t_c
                };
            }

            /* Publica UMIDADE se o tipo NÃO for “só temperatura” e o driver forneceu UR
               OBS: política de subcanal/canal fica no rs485_sd_adapter.c */
            if (sensor->type != RS485_TYPE_TEMPERATURA && has_hum && wr < (int)out_len) {
                out[wr++] = (rs485_measurement_t){
                    .channel = sensor->channel,
                    .kind    = RS485_MEAS_HUM_PCT,
                    .value   = rh
                };
            }

            /* Se foi cadastrado como “só umidade” mas o driver não retornou UR, sinaliza erro coerente */
            if (sensor->type == RS485_TYPE_UMIDADE && !has_hum) return -4;

            return wr;  /* 0..2 medições */
        }
        
       case RS485_TYPE_ENERGIA: {
            float I[3] = {0};
            if (energy_meter_read_currents((uint8_t)sensor->address, I) != ESP_OK)
                return -2;
            int wr = 0;
            /* L1 */
            if (wr < (int)out_len) {
                out[wr++] = (rs485_measurement_t){
                    .channel = sensor->channel,
                    .kind    = RS485_MEAS_I_RMS,
                    .value   = I[0]
                };
            }
            /* L2 (se existir ou >0, opcional manter sempre para padronizar) */
            if (wr < (int)out_len) {
                out[wr++] = (rs485_measurement_t){
                    .channel = sensor->channel,
                    .kind    = RS485_MEAS_I_RMS,
                    .value   = I[1]
                };
            }
            /* L3 */
            if (wr < (int)out_len) {
                out[wr++] = (rs485_measurement_t){
                    .channel = sensor->channel,
                    .kind    = RS485_MEAS_I_RMS,
                    .value   = I[2]
                };
            }
            return wr; /* 3 registros (mono pode vir 0 nos extras; o adapter decide gravar ou não) */
        }

        case RS485_TYPE_FLUXO:
            return -2;

        case RS485_TYPE_PRESSAO:
            return -2;

        case RS485_TYPE_GPS:
        case RS485_TYPE_LUZ:
        case RS485_TYPE_GAS:
            return -2;

        case RS485_TYPE_OUTRO:
        default:
            return -3; // tipo ainda não suportado
    }
}

/* --------------- Varredura simples da lista --------------- */
int rs485_poll_all(const rs485_sensor_t *list, size_t count,
                   rs485_measurement_t *out, size_t out_len)
{
    if (!list || !out) return -1;
    size_t wr = 0;
    for (size_t i = 0; i < count; ++i) {
        if (wr >= out_len) break;
        int n = rs485_read_measurements(&list[i], &out[wr], out_len - wr);
        if (n > 0) wr += (size_t)n;
    }
    return (int)wr;
}

/* --------------- Snapshot do cadastro persistido --------------- */
int rs485_registry_get_snapshot(rs485_sensor_t *out, size_t max)
{
    if (!out || max == 0) return -1;

    sensor_map_t map[RS485_MAX_SENSORS] = {0};
    size_t count = 0;
    if (load_rs485_config(map, &count) != ESP_OK) {
        return -2;
    }

    size_t wr = 0;
    for (size_t i = 0; i < count && wr < max; ++i) {
        const uint16_t ch = map[i].channel;
        const uint8_t  ad = map[i].address;
        if (ch == 0 || ad < 1 || ad > 247) continue;  /* saneamento */

        rs485_sensor_t *dst = &out[wr++];
        dst->channel = ch;
        dst->address = ad;
        dst->type    = rs485_type_from_str(map[i].type);       /* string -> enum */
        dst->subtype = rs485_subtype_from_str(map[i].subtype); /* string -> enum */
    }
    return (int)wr; /* quantos preenchidos */
}

/* --------------- NOVO: Varredura de drivers em um endereço ---------------
 * Tenta identificar o equipamento instalado no endereço `addr`.
 * Retorna true se algum driver reconheceu; preenche out_* com a identificação.
 *
 * OBS: mantive parâmetros “simples” (sem struct) para você poder
 * chamar sem precisar alterar o header imediatamente (basta um extern).
 */
bool rs485_registry_probe_any(uint8_t addr,
                              rs485_type_t *out_type,
                              rs485_subtype_t *out_subtype,
                              uint8_t *out_used_fc,
                              const char **out_driver_name)
{
    if (out_type)      *out_type      = RS485_TYPE_INVALID;
    if (out_subtype)   *out_subtype   = RS485_SUBTYPE_NONE;
    if (out_used_fc)   *out_used_fc   = 0;
    if (out_driver_name) *out_driver_name = NULL;

    /* 1) XY-MD02 (Termo/UR) */
    uint8_t fc = 0;
    if (temperature_rs485_probe(addr, &fc) > 0) {              /* XY probe */  /* :contentReference[oaicite:1]{index=1} */
        if (out_type)      *out_type = RS485_TYPE_TERMOHIGRO;
        if (out_subtype)   *out_subtype = RS485_SUBTYPE_NONE;
        if (out_used_fc)   *out_used_fc = fc;
        if (out_driver_name) *out_driver_name = "XY_MD02";
        return true;
    }

    /* 2) JSY-MK-333 (Energia) — usa um “mapa” default até você ajustar */
    jsy_map_t map = jsy_mk333_default_map();
    fc = 0;
    if (jsy_mk333_probe(addr, &map, &fc) == 0) {               /* JSY probe */ /* :contentReference[oaicite:2]{index=2} */
        if (out_type)      *out_type = RS485_TYPE_ENERGIA;
        if (out_subtype)   *out_subtype = RS485_SUBTYPE_MONOFASICO; // placeholder
        if (out_used_fc)   *out_used_fc = fc;
        if (out_driver_name) *out_driver_name = "JSY_MK_333";
        return true;
    }

    /* Acrescente aqui outros probes de drivers futuros */

    return false; // nenhum driver reconheceu
}

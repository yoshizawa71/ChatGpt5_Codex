/*
 * 485_registry.c
 *
 *  Created on: 13 de ago. de 2025
 *      Author: geopo
 */
/*
 * rs485_registry.c — Alinhado com as opções do front
 */

#include <string.h>
#include <ctype.h>
#include "rs485_registry.h"
#include "temperature_rs485.h"  // seu driver T/UR

// ===== Configuração: canal da umidade no mesmo canal do T? =====
// 1 = mesmo canal (recomendado pelo seu fluxo atual)
// 0 = usar canal+1 para UR
#define TH_HUM_SAME_CHANNEL 1

// -------- utils --------
static bool streq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
static bool streq_ci(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        ++a; ++b;
    }
    return *a == *b;
}

// -------- string -> enum --------
rs485_type_t rs485_type_from_str(const char *s) {
    if (!s || !*s) return RS485_TYPE_INVALID;

    // valores do front (e sinônimos/acentos)
    if (streq_ci(s, "energia"))             return RS485_TYPE_ENERGIA;

    if (streq_ci(s, "termohigrometro") ||
        streq_ci(s, "termo-higrometro") ||
        streq_ci(s, "termohigrômetro") ||
        streq_ci(s, "termo-higrômetro"))    return RS485_TYPE_TERMOHIGRO;

    if (streq_ci(s, "temperatura"))         return RS485_TYPE_TEMPERATURA;
    if (streq_ci(s, "umidade"))             return RS485_TYPE_UMIDADE;

    if (streq_ci(s, "pressao") ||
        streq_ci(s, "pressão"))             return RS485_TYPE_PRESSAO;

    if (streq_ci(s, "vazao")   ||
        streq_ci(s, "vazão")   ||
        streq_ci(s, "fluxo"))               return RS485_TYPE_FLUXO;

    if (streq_ci(s, "gps"))                  return RS485_TYPE_GPS;
    if (streq_ci(s, "luz"))                  return RS485_TYPE_LUZ;

    if (streq_ci(s, "gas") ||
        streq_ci(s, "gás"))                  return RS485_TYPE_GAS;

    if (streq_ci(s, "outro") ||
        streq_ci(s, "outros"))               return RS485_TYPE_OUTRO;

    return RS485_TYPE_INVALID;
}

// Canoniza para os nomes do **front**
const char* rs485_type_to_str(rs485_type_t t) {
    switch (t) {
        case RS485_TYPE_ENERGIA:     return "energia";
        case RS485_TYPE_TERMOHIGRO:  return "termohigrometro";
        case RS485_TYPE_TEMPERATURA: return "temperatura";
        case RS485_TYPE_UMIDADE:     return "umidade";
        case RS485_TYPE_PRESSAO:     return "pressao";
        case RS485_TYPE_FLUXO:       return "vazao";         // front usa "vazao"
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

// -------- Dispatcher de leitura --------
// IMPORTANTE: para T/UR usamos o seu driver "temperature_rs485.h".
// Assinatura esperada (ajuste se seu header for diferente):
//   int temperature_rs485_read(uint8_t addr, float *temp_c, float *hum_pct, bool *has_hum);
//
// Retorno: >=0 quantidade de medições escritas; <0 erro.
int rs485_read_measurements(const rs485_sensor_t *sensor,
                            rs485_measurement_t *out, size_t out_len)
{
    if (!sensor || !out || out_len == 0) return -1;

    switch (sensor->type) {

        case RS485_TYPE_TERMOHIGRO:
        case RS485_TYPE_TEMPERATURA:
        case RS485_TYPE_UMIDADE: {
            float t_c = 0.0f, rh = 0.0f;
            bool  has_hum = false;

            int r = temperature_rs485_read((uint8_t)sensor->address, &t_c, &rh, &has_hum);
            if (r < 0) return r;

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
                        .channel = sensor->channel,     // mesmo canal (alinhado ao seu pedido)
                        .kind    = RS485_MEAS_HUM_PCT,
                        .value   = rh
                    };
#else
                    out[wr++] = (rs485_measurement_t){
                        .channel = (uint16_t)(sensor->channel + 1), // alternativa canal vizinho
                        .kind    = RS485_MEAS_HUM_PCT,
                        .value   = rh
                    };
#endif
                }
            }

            // Se pediram "umidade" e o sensor NÃO reporta UR:
            if (sensor->type == RS485_TYPE_UMIDADE && !has_hum) return -4;

            return wr;
        }

        case RS485_TYPE_ENERGIA:
            // TODO: chamar driver de energia (mono/tri por subtype)
            return -2;

        case RS485_TYPE_FLUXO:
            // TODO: chamar driver de vazão/fluxo
            return -2;

        case RS485_TYPE_PRESSAO:
            // TODO: chamar driver de pressão
            return -2;

        case RS485_TYPE_GPS:
        case RS485_TYPE_LUZ:
        case RS485_TYPE_GAS:
            // TODO: implementar quando drivers estiverem prontos
            return -2;

        case RS485_TYPE_OUTRO:
        default:
            return -3; // tipo não suportado (ainda)
    }
}

// Varredura simples
int rs485_poll_all(const rs485_sensor_t *list, size_t count,
                   rs485_measurement_t *out, size_t out_len)
{
    if (!list || !out) return -1;
    size_t wr = 0;
    for (size_t i = 0; i < count; ++i) {
        if (wr >= out_len) break;
        int n = rs485_read_measurements(&list[i], &out[wr], out_len - wr);
        if (n > 0) wr += (size_t)n;
        // n <= 0 => sem leitura/erro: pode logar/contabilizar aqui
    }
    return (int)wr;
}

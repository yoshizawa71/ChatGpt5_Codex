/*
 * payload_time.h
 *
 *  Created on: 9 de out. de 2025
 *      Author: geopo
 */


#ifndef PAYLOAD_TIME_H
#define PAYLOAD_TIME_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef USE_EPOCH_SECONDS
#define USE_EPOCH_SECONDS 0
#endif

// Retorna epoch em milissegundos (UTC), baseado em time(NULL)*1000 (sem timezone local)
int64_t epoch_ms_utc(void);

// Formata "YYYY-MM-DDTHH:MM:SSZ" em UTC usando gmtime_r(). Retorna buf para encadeamento.
const char *iso8601_utc(char *buf, size_t n);

void utc_selftest_once(void);

int64_t local_date_time_to_utc_ms(const char *date, const char *time_str);

// adiciona este protótipo:
const char *iso8601_utc_from_ms(int64_t epoch_ms, char *buf, size_t n);

void iso8601_local_from_ms(int64_t ts_ms, char *out, size_t out_sz);

#if USE_EPOCH_SECONDS
// Opcional: epoch em segundos (UTC)
int64_t epoch_s_utc(void);
#endif


// === Seleção do timestamp (epoch ms) a partir de data/hora do SD ===
// Observação: epoch numérico é sempre UTC (exigência de várias plataformas).
#if CONFIG_PAYLOAD_TIMESTAMP_UTC
  // Converte "DD/MM/YYYY"+"HH:MM:SS" local do SD para epoch ms UTC
  #define PAYLOAD_TS_FROM_SD(date, time) local_date_time_to_utc_ms((date), (time))
#else
  // Mesmo no modo "Local ISO", mantemos epoch numérico em UTC (compatibilidade)
  #define PAYLOAD_TS_FROM_SD(date, time) local_date_time_to_utc_ms((date), (time))
#endif

// ==== Nova API canônica para colocar o campo "time" no JSON ====
//
// - Lê a data/hora "local" do SD (date, time_str).
// - Obedece o menuconfig:
//     * CONFIG_PAYLOAD_TIMESTAMP_UTC   -> formata como "YYYY-MM-DDTHH:MM:SSZ"
//     * CONFIG_PAYLOAD_TIMESTAMP_LOCAL -> formata como "YYYY-MM-DDTHH:MM:SS±HH:MM"
// - Aplica opcionalmente um offset fixo (CONFIG_PAYLOAD_TS_OFFSET_MINUTES).
// - Adiciona em root o campo:  "time": "<ISO-8601>"
//
// Retorna true se adicionou com sucesso.
bool payload_add_time_iso(cJSON *root, const char *date, const char *time_str);

// Macro “azucar sintáctico”
#define PAYLOAD_ADD_TIME(root_, date_, time_)  \
    (void)payload_add_time_iso((root_), (date_), (time_))
// --------------------------------------------------------------------

#ifdef __cplusplus
}
#endif

#endif /* PAYLOAD_TIME_H */



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


#if USE_EPOCH_SECONDS
// Opcional: epoch em segundos (UTC)
int64_t epoch_s_utc(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PAYLOAD_TIME_H */



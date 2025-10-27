#include <time.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "payload_time.h"

static const char *TAG = "TIME/UTC";

void utc_selftest_once(void)
{
    const char *TAG = "UTC/SELFTEST";

    // 1) UTC “fonte da verdade”
    char iso[32];
    int64_t ms = epoch_ms_utc();
    iso8601_utc(iso, sizeof iso);

    // 2) Hora local só pra checar offset (NÃO é usada nos payloads)
    time_t now = (time_t)(ms / 1000);
    struct tm tm_local = {0}, tm_utc = {0};
    localtime_r(&now, &tm_local);
    gmtime_r(&now,   &tm_utc);

    // 3) Calcular offset local–UTC em horas (aproximado)
    int local_h = tm_local.tm_hour, utc_h = tm_utc.tm_hour;
    // cuidado na virada do dia:
    int diff_h = local_h - utc_h;
    if (diff_h >  12) diff_h -= 24;
    if (diff_h < -12) diff_h += 24;

    ESP_LOGI(TAG, "epoch_ms_utc=%lld  iso=%s", (long long)ms, iso);
    ESP_LOGI(TAG, "local=%04d-%02d-%02d %02d:%02d:%02d  utc=%04d-%02d-%02d %02d:%02d:%02d  (offset ~ %dh)",
             tm_local.tm_year+1900, tm_local.tm_mon+1, tm_local.tm_mday,
             tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec,
             tm_utc.tm_year+1900,   tm_utc.tm_mon+1,   tm_utc.tm_mday,
             tm_utc.tm_hour,   tm_utc.tm_min,   tm_utc.tm_sec,
             diff_h);

    // 4) Checagens simples
    if (iso[19] == 'Z') {
        ESP_LOGI(TAG, "OK: ISO termina em 'Z' (UTC).");
    } else {
        ESP_LOGW(TAG, "ALERTA: ISO não termina com 'Z'.");
    }
    if (ms > 1700000000000LL) { // ~2023-11-14 em ms; só pra evitar época errada
        ESP_LOGI(TAG, "OK: epoch_ms_utc plausível.");
    } else {
        ESP_LOGW(TAG, "ALERTA: epoch_ms_utc baixo demais (SNTP sem sync?).");
    }
}

int64_t epoch_ms_utc(void)
{
    time_t now = time(NULL);
    if (now < 0) {
        return 0;
    }
    return ((int64_t)now) * 1000;
}

const char *iso8601_utc(char *buf, size_t n)
{
    // Tamanho m�nimo "YYYY-MM-DDTHH:MM:SSZ" + '\0' = 20 + 1
    const size_t min_len = 21;
    if (!buf || n == 0) {
        return "";
    }
    buf[0] = '\0';

    if (n < min_len) {
        // Buffer insuficiente: retorna vazio de forma segura
        return buf;
    }

    time_t now = time(NULL);
    if (now < 0) {
        return buf;
    }

    struct tm tm_utc;
    if (gmtime_r(&now, &tm_utc) == NULL) {
        return buf;
    }

    // YYYY-MM-DDTHH:MM:SSZ
    // Garantido caber pois checamos n >= 21
    (void)snprintf(
        buf,
        n,
        "%04d-%02d-%02dT%02d:%02d:%02dZ",
        tm_utc.tm_year + 1900,
        tm_utc.tm_mon + 1,
        tm_utc.tm_mday,
        tm_utc.tm_hour,
        tm_utc.tm_min,
        tm_utc.tm_sec
    );

    int64_t ts_ms = ((int64_t)now) * 1000;
    ESP_LOGI(TAG, "ts_ms=%" PRId64 ", iso=%s", ts_ms, buf);
    return buf;
}

// ADICIONE AO FINAL DO ARQUIVO ------------------------------
static int parse_ddmmyyyy(const char *s, int *d, int *m, int *y) {
    // aceita "DD/MM/YYYY"
    if (!s || strlen(s) < 10) return -1;
    return (sscanf(s, "%2d/%2d/%4d", d, m, y) == 3) ? 0 : -1;
}
static int parse_hhmmss(const char *s, int *H, int *M, int *S) {
    // aceita "HH:MM:SS"
    if (!s || strlen(s) < 8) return -1;
    return (sscanf(s, "%2d:%2d:%2d", H, M, S) == 3) ? 0 : -1;
}

/* Converte data/hora armazenadas em LOCAL-TIME (ex.: TZ=-03)
 * para epoch UTC em milissegundos, respeitando o offset atual.
 * Formatos aceitos: date="DD/MM/YYYY", time="HH:MM:SS".
 * Retorna 0 se não conseguir converter.
 */
int64_t local_date_time_to_utc_ms(const char *date, const char *time_str)
{
    int d, m, y, H, Mi, S;

    // Sucesso esperado: parse_* retornam 0; qualquer não-zero => erro
    if (!date || !time_str) return 0;
    if (parse_ddmmyyyy(date, &d, &m, &y) != 0) return 0;
    if (parse_hhmmss(time_str, &H, &Mi, &S) != 0) return 0;

    struct tm tm_local = {0};
    tm_local.tm_mday = d;
    tm_local.tm_mon  = m - 1;
    tm_local.tm_year = y - 1900;
    tm_local.tm_hour = H;
    tm_local.tm_min  = Mi;
    tm_local.tm_sec  = S;

    // 1) mktime() interpreta tm_local como HORA LOCAL -> epoch (segundos) local
    time_t epoch_local = mktime(&tm_local);
    if (epoch_local == (time_t)-1) return 0;

    // 2) Calcular o offset local↔UTC no PRÓPRIO instante do registro
    //    - gmtime_r() dá a visão UTC daquele epoch
    //    - mktime() sobre esse tm_utc trata-o como "local", resultando em epoch_utc_as_local
    //    - a diferença é exatamente o offset (segundos)
    struct tm tm_utc;
    if (gmtime_r(&epoch_local, &tm_utc) == NULL) return 0;

    time_t epoch_utc_as_local = mktime(&tm_utc);
    if (epoch_utc_as_local == (time_t)-1) return 0;

    time_t tz_offset_sec = epoch_local - epoch_utc_as_local;  // p.ex. -10800

    // 3) Converter epoch local -> UTC
    int64_t epoch_utc_ms = ((int64_t)epoch_local - (int64_t)tz_offset_sec) * 1000LL;
    return epoch_utc_ms;
}

const char *iso8601_utc_from_ms(int64_t epoch_ms, char *buf, size_t n)
{
    const size_t min_len = 21; // "YYYY-MM-DDTHH:MM:SSZ" + '\0'
    if (!buf || n < min_len) { if (buf && n) buf[0] = '\0'; return buf ? buf : ""; }

    time_t t = (time_t)(epoch_ms / 1000LL);
    struct tm tm_utc;
    if (gmtime_r(&t, &tm_utc) == NULL) { buf[0] = '\0'; return buf; }

    snprintf(buf, n, "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
             tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
    return buf;
}


#if USE_EPOCH_SECONDS
int64_t epoch_s_utc(void)
{
    time_t now = time(NULL);
    if (now < 0) {
        return 0;
    }
    return (int64_t)now;
}
#endif
#include "payload_time.h"
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"


static const char *TAG = "TIME/UTC";

// ------------------------------------------------------------------
// Helper: aplica ajuste fixo em minutos (menuconfig)
// ------------------------------------------------------------------
static inline int64_t apply_minutes_offset_ms(int64_t ms) {
#if CONFIG_PAYLOAD_TS_ADJUST_ENABLE
    return ms + (int64_t)CONFIG_PAYLOAD_TS_OFFSET_MINUTES * 60LL * 1000LL;
#else
    return ms;
#endif
}

// ------------------------------------------------------------------
// Helper: parse "DD/MM/YYYY" "HH:MM:SS" -> inteiros (local)
// ------------------------------------------------------------------
static bool parse_ddmmyyyy_hhmmss(const char *date, const char *time_str,
                                  int *Y, int *M, int *D, int *h, int *m, int *s)
{
    if (!date || !time_str) return false;
    
    int d = 0, mo = 0, y = 0, hh = 0, mi = 0, ss = 0;
    if (sscanf(date, "%d/%d/%d", &d, &mo, &y) != 3) {
        return false;
    }
    if (sscanf(time_str, "%d:%d:%d", &hh, &mi, &ss) != 3) {
        return false;
    }
    if (Y) { *Y = y; }
    if (M) { *M = mo; }
    if (D) { *D = d; }
    if (h) { *h = hh; }
    if (m) { *m = mi; }
    if (s) { *s = ss; }
    return true;
}

// ------------------------------------------------------------------
// Helper: formata offset "+HH:MM" / "-HH:MM" a partir de minutos
// ------------------------------------------------------------------
static void format_offset_from_minutes(int minutes, char *buf, size_t n) {
    // clamp opcional: de -12h a +12h
    if (minutes < -720) minutes = -720;
    if (minutes >  720) minutes =  720;
    char sign = (minutes < 0) ? '-' : '+';
    int absmin = (minutes < 0) ? -minutes : minutes;
    int hh = absmin / 60;
    int mm = absmin % 60;
    snprintf(buf, n, "%c%02d:%02d", sign, hh, mm);
}

// ------------------------------------------------------------------
// IMPLEMENTAÇÃO PRINCIPAL
// ------------------------------------------------------------------
bool payload_add_time_iso(cJSON *root, const char *date, const char *time_str)
{
    if (!root) return false;

#if CONFIG_PAYLOAD_TIMESTAMP_UTC

    // Caminho UTC: pega o horário LOCAL do SD, converte para UTC (ms),
    // aplica offset opcional e formata como "YYYY-MM-DDTHH:MM:SSZ".
    int64_t ts_ms = local_date_time_to_utc_ms(date, time_str);
    if (ts_ms <= 0) ts_ms = epoch_ms_utc();
    ts_ms = apply_minutes_offset_ms(ts_ms);

    char iso_utc[32];
    iso8601_utc_from_ms(ts_ms, iso_utc, sizeof iso_utc);  // garante o 'Z' no final
    cJSON_AddStringToObject(root, "time", iso_utc);
    return true;

#else // CONFIG_PAYLOAD_TIMESTAMP_LOCAL

    // Caminho LOCAL: usa a data/hora do SD "como está" e acrescenta um offset explícito (±HH:MM).
    // Isso gera string ISO-8601 válida: "YYYY-MM-DDTHH:MM:SS-03:00"
    int Y=0, M=0, D=0, h=0, m=0, s=0;
    if (!parse_ddmmyyyy_hhmmss(date, time_str, &Y, &M, &D, &h, &m, &s)) {
        // fallback: gera UTC Z se parsing falhar
        int64_t ts_ms = epoch_ms_utc();
        char iso_utc[32];
        iso8601_utc_from_ms(ts_ms, iso_utc, sizeof iso_utc);
        cJSON_AddStringToObject(root, "time", iso_utc);
        return true;
    }

    // Offset configurável em minutos (default 0). Para Brasil, use -180.
    char tz_off[8];
#if CONFIG_PAYLOAD_TS_ADJUST_ENABLE
    format_offset_from_minutes((int)CONFIG_PAYLOAD_TS_OFFSET_MINUTES, tz_off, sizeof tz_off);
#else
    // Se você quiser travar em -03:00 por enquanto, troque por "-03:00".
    format_offset_from_minutes(-180, tz_off, sizeof tz_off);
#endif

    char iso_local[40];
    // "YYYY-MM-DDTHH:MM:SS±HH:MM"
    snprintf(iso_local, sizeof iso_local,
             "%04d-%02d-%02dT%02d:%02d:%02d%s",
             Y, M, D, h, m, s, tz_off);

    cJSON_AddStringToObject(root, "time", iso_local);
    return true;

#endif
}

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

    // mktime interpreta tm_local como HORA LOCAL e devolve epoch em UTC
    time_t epoch = mktime(&tm_local);
    if (epoch == (time_t)-1) {
        return 0;
    }

    // Já é UTC, só converter para ms
    return ((int64_t)epoch) * 1000LL;
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
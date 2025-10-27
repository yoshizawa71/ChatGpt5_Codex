Arquivos novos (código completo)

- FILE: connectivity/ip/messaging/payload_common/include/payload_time.h
----------------------------------------
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

#if USE_EPOCH_SECONDS
// Opcional: epoch em segundos (UTC)
int64_t epoch_s_utc(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PAYLOAD_TIME_H */
----------------------------------------

- FILE: connectivity/ip/messaging/payload_common/src/payload_time.c
----------------------------------------
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "connectivity/ip/messaging/payload_common/include/payload_time.h"

static const char *TAG = "TIME/UTC";

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
    // Tamanho mínimo "YYYY-MM-DDTHH:MM:SSZ" + '\0' = 20 + 1
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
----------------------------------------


Patches mínimos (inserções nos builders)

1) MQTT

- Arquivo(s) e função(ões) afetados:
  - connectivity/ip/messaging/mqtt/src/mqtt_payload_builder.c
    - Função onde o JSON do payload MQTT é montado (ex.: mqtt_payload_build_*)

- Inclusão de header (no topo do arquivo)
ANTES:
#include "algum_header_ja_existente.h"
...
DEPOIS:
#include "algum_header_ja_existente.h"
...
#include "connectivity/ip/messaging/payload_common/include/payload_time.h"

- Inserção no ponto de montagem do JSON (logo após campos já existentes):
ANTES:
// ... campos existentes do JSON ...
DEPOIS:
// ... campos existentes do JSON ...
{
    char ts_iso[25];
    json_add_str("timestamp", iso8601_utc(ts_iso, sizeof ts_iso));
    json_add_int("ts_ms", epoch_ms_utc());
}

Motivo:
- Padroniza timestamp em UTC e envia ambos os formatos exigidos ("timestamp" ISO8601 e "ts_ms") sem alterar campos existentes.


2) HTTP

- Arquivo(s) e função(ões) afetados:
  - connectivity/ip/messaging/http/src/http_payload_builder.c  (ou arquivo equivalente do builder HTTP)
    - Função onde o JSON do payload HTTP é montado (ex.: http_payload_build_*)

- Inclusão de header (no topo do arquivo)
ANTES:
#include "outro_header_ja_existente.h"
...
DEPOIS:
#include "outro_header_ja_existente.h"
...
#include "connectivity/ip/messaging/payload_common/include/payload_time.h"

- Inserção no ponto de montagem do JSON:
ANTES:
// ... campos existentes do JSON ...
DEPOIS:
// ... campos existentes do JSON ...
{
    char ts_iso[25];
    json_add_str("timestamp", iso8601_utc(ts_iso, sizeof ts_iso));
    json_add_int("ts_ms", epoch_ms_utc());
}

Motivo:
- Garante inclusão dos dois campos UTC em todos os payloads HTTP, sem quebrar assinaturas e mantendo compatibilidade.


Notas de teste

- Log esperado no console (exemplo):
I (123456) TIME/UTC: ts_ms=1739088185000, iso=2025-10-09T14:23:05Z

- Exemplo de payload (trecho):
{
  ...
  "timestamp": "2025-10-09T14:23:05Z",
  "ts_ms": 1739088185000,
  ...
}

- Dicas:
  - Certifique-se de que o serviço de SNTP do projeto esteja sincronizando o relógio.
  - Verifique se o log aparece quando um payload é montado/enviado (MQTT e HTTP).
  - O campo "timestamp" usa gmtime_r() (UTC) e não sofre influência de timezone local.
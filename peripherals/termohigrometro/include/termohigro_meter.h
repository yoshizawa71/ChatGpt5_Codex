 #pragma once
 #include <stddef.h>
 #include <stdint.h>
 #include "rs485_registry.h"

 #ifdef __cplusplus
 extern "C" {
 #endif

 // Constrói 1..2 measurements (TEMP_C e opcional HUM_PCT)
 // Retorna quantas medições foram escritas em 'out', <0 em erro.
 int termohigrometro_read(uint8_t addr, uint16_t channel,
                          rs485_measurement_t *out, size_t out_len);

 #ifdef __cplusplus
 }
 #endif

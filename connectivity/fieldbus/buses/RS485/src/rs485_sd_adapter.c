/*
 * rs485_sd_adapter.c
 *
 *  Created on: 25 de out. de 2025
 *      Author: geopo
 */

 #include "rs485_sd_adapter.h"
 #include "esp_log.h"
#include "sdmmc_driver.h"

 // já existente no seu projeto (sdcard_mmc.c / datalogger)

 static const char *TAG = "RS485_SD";

 static inline int _save_one(uint16_t channel, int subindex, float value) {
     char buf[24];
     int n = snprintf(buf, sizeof(buf), "%.3f", (double)value);
     if (n <= 0 || n >= (int)sizeof(buf)) return -1;
     esp_err_t e = save_record_sd_rs485((int)channel, subindex, buf);
     return (e == ESP_OK) ? 1 : -2;
 }
 
 int save_measurements_to_sd(const rs485_measurement_t *m, size_t n)
 {
     if (!m || n == 0) return 0;
     int written = 0;
     /* contador por canal para energia (I_RMS → subindex 1..3) */
     uint8_t i_subcount[1024] = {0};
     
     for (size_t i = 0; i < n; ++i) {
         const rs485_measurement_t *mm = &m[i];
         switch (mm->kind) {
        case RS485_MEAS_TEMP_C:
            // TH: temperatura em canal.1
            written += _save_one(mm->channel, /*subindex=*/1, mm->value);
            break;
        case RS485_MEAS_HUM_PCT:
             /* TH: umidade: mesmo canal (.2) por padrão;
               se RS485_SD_HUM_SAME_CHANNEL == 0 -> grava em canal+1 (.1) */
#if RS485_SD_HUM_SAME_CHANNEL
            written += _save_one(mm->channel, /*subindex=*/2, mm->value);
#else
            written += _save_one((uint16_t)(mm->channel + 1), /*subindex=*/1, mm->value);
#endif
            break;
        case RS485_MEAS_I_RMS: {
            /* Energia: subíndices 1..3 por canal (L1/L2/L3) */
            uint8_t next = (mm->channel < (uint16_t)sizeof(i_subcount))
                           ? ++i_subcount[mm->channel] : 1;
            if (next > 3) next = 3;
            written += _save_one(mm->channel, (int)next, mm->value);
            break;
            }
         default:
             // kinds futuros (energia, fluxo, etc.) podem ganhar mapeamento aqui
             ESP_LOGW(TAG, "kind %d sem mapeamento SD (canal=%u)", (int)mm->kind, mm->channel);
             break;
         }
     }
     return written;
 }




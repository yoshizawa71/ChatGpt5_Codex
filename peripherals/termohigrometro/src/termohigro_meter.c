 #include "termohigro_meter.h"
 #include "xy_md02_driver.h"   // seu driver anexo
 #include "esp_log.h"

 static const char *TAG = "TH_METER";

 int termohigrometro_read(uint8_t addr, uint16_t channel,
                          rs485_measurement_t *out, size_t out_len)
 {
     if (!out || out_len == 0) return -1;
     float t=0, h=0; bool has_h=false;
     int r = temperature_rs485_read(addr, &t, &h, &has_h);  // tenta FC04→FC03
     if (r < 0) return r;

     int wr = 0;
     out[wr++] = (rs485_measurement_t){ .channel = channel,
                                        .kind = RS485_MEAS_TEMP_C,
                                        .value = t };
     if (has_h && wr < (int)out_len) {
         out[wr++] = (rs485_measurement_t){ .channel = channel,
                                            .kind = RS485_MEAS_HUM_PCT,
                                            .value = h };
     }
     return wr;
 }

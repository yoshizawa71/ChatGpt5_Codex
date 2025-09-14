#ifndef ENERGY_METER_H
#define ENERGY_METER_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float i_a, i_b, i_c;        /* A */
    /* Reservas p/ futuro (tensão, potência, energia etc.) */
    float v_a, v_b, v_c;
    float p_act_total_w;
    float freq_hz;
    float pf_total;
    float energy_kwh;
} energy_readings_t;

esp_err_t energy_meter_init(void);

/* Lê correntes A/B/C (A) do JSY-MK-333 por endereço Modbus. */
esp_err_t energy_meter_read_currents(uint8_t addr, float outI[3]);

/* Salva sempre as 3 fases (sub=1..3) em “canal.sub” → use só se quiser forçar trifásico. */
esp_err_t energy_meter_save_currents(uint8_t channel, uint8_t addr);

/* Versão “por cadastro”:
   - pega endereço pelo canal no registry;
   - consulta se é monofásico(1) ou trifásico(3);
   - grava 1 linha "canal" (ex.: "3") ou 3 linhas "canal.sub" (ex.: "4.1/4.2/4.3") com DADOS=valor.
*/
esp_err_t energy_meter_save_currents_by_channel(uint8_t channel);

/* Best-effort: percorre todos cadastrados e salva correntes dos de energia. */
esp_err_t energy_meter_save_registered_currents(void);

/* Futuro: leitura “completa”. */
esp_err_t energy_meter_read_all(uint8_t addr, energy_readings_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ENERGY_METER_H */

#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "datalogger_control.h"

// =================== Getters vindos do seu FRONT/NVS ===================
// Suas implementações existentes irão sobrescrever as WEAK do .c
#ifdef __cplusplus
extern "C" {
#endif
// Builder específico para Ubidots (a partir do SD)
struct record_index_config; // forward
esp_err_t mqtt_payload_build_from_sd_ubidots(char *topic_out,  size_t topic_sz,
                                             char *payload_out,size_t payload_sz,
                                             const struct record_index_config *rec_index_in,
                                             uint32_t *points_out,
                                             uint32_t *cursor_out);

// (opcional) função de mapeamento canal->label Ubidots (você pode trocar depois)
const char *ubidots_label_for_channel(int canal);
const char *get_mqtt_ca_pem(void);

// =================== BUILDERS ===================

// Builder genérico simples (não lê SD). Mantido para utilidade.
esp_err_t mqtt_payload_build(char *topic_out,  size_t topic_sz,
                             char *payload_out,size_t payload_sz);

// Builder que MONTA o payload a partir da TABELA gravada no SD.
// - Usa sua API de índices (record_index_config, read_record_sd, etc.)
// - Retorna quantos pontos foram empacotados em *points_out
// - Retorna o novo cursor em *cursor_out (para você persistir após sucesso)
struct record_index_config; // forward-decl (definição real vem do seu header)
esp_err_t mqtt_payload_build_from_sd(char *topic_out,  size_t topic_sz,
                                     char *payload_out,size_t payload_sz,
                                     const struct record_index_config *rec_index_in,
                                     uint32_t *points_out,
                                     uint32_t *cursor_out);

// Callback opcional para injetar campos adicionais em "data" do JSON canônico.
// (Se não implementar, uma versão WEAK adiciona {"heartbeat":1})
int mqtt_payload_fill_data(void *cjson_data_object /* cJSON* */);

esp_err_t mqtt_payload_build_from_sd_weg_energy(char *topic_out,  size_t topic_sz,
                                         char *payload_out,size_t payload_sz,
                                         const struct record_index_config *rec_index_in,
                                         uint32_t *points_out,
                                         uint32_t *cursor_out);
                                         
 esp_err_t mqtt_payload_build_from_sd_weg_water(char *topic_out,  size_t topic_sz,
                                         char *payload_out,size_t payload_sz,
                                         const struct record_index_config *rec_index_in,
                                         uint32_t *points_out,
                                         uint32_t *cursor_out);

#ifdef __cplusplus
}
#endif

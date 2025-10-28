/*
 * rs485_registry.h — Catálogo e dispatcher de sensores RS-485
 * - mapeia strings ⇄ enums (alinhado ao front)
 * - dispatcher de leitura (drivers específicos)
 * - probe/identificação por endereço (varre drivers)
 */
#ifndef CONNECTIVITY_FIELDBUS_BUSES_RS485_INCLUDE_RS485_REGISTRY_H_
#define CONNECTIVITY_FIELDBUS_BUSES_RS485_INCLUDE_RS485_REGISTRY_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================
 * Tipos de dispositivo
 * ========================= */
typedef enum {
    RS485_TYPE_INVALID = 0,
    RS485_TYPE_ENERGIA,
    RS485_TYPE_TERMOHIGRO,
    RS485_TYPE_TEMPERATURA,
    RS485_TYPE_UMIDADE,
    RS485_TYPE_PRESSAO,
    RS485_TYPE_FLUXO,
    RS485_TYPE_GPS,
    RS485_TYPE_LUZ,
    RS485_TYPE_GAS,
    RS485_TYPE_OUTRO
} rs485_type_t;

typedef enum {
    RS485_SUBTYPE_NONE = 0,
    RS485_SUBTYPE_MONOFASICO,
    RS485_SUBTYPE_TRIFASICO,
    RS485_SUBTYPE_XY_MD02
} rs485_subtype_t;

/* Medições publicadas pelo barramento (adicione conforme precisar) */
typedef enum {
    RS485_MEAS_NONE = 0,
    RS485_MEAS_TEMP_C,     /* °C */
    RS485_MEAS_HUM_PCT,    /* %RH */
    /* Exemplos futuros:
       RS485_MEAS_POWER_W,
       RS485_MEAS_ENERGY_KWH,
       RS485_MEAS_FLOW_LMH,
       RS485_MEAS_PRESS_BAR,
    */
} rs485_meas_kind_t;

/* Sensor cadastrado pelo front */
typedef struct {
    uint16_t        channel;     /* canal lógico */
    uint8_t         address;     /* endereço Modbus */
    rs485_type_t    type;
    rs485_subtype_t subtype;
} rs485_sensor_t;

/* Medição produzida pelo dispatcher */
typedef struct {
    uint16_t          channel;
    rs485_meas_kind_t kind;
    float             value;
} rs485_measurement_t;

typedef struct {
    unsigned        baud;
    uart_parity_t   parity;
    uart_stop_bits_t stop;
    uint32_t        timeout_ms;
} rs485_profile_t;

/* =========================
 * Mapas string ⇄ enum
 * ========================= */
rs485_type_t     rs485_type_from_str(const char *s);
const char*      rs485_type_to_str(rs485_type_t t);
rs485_subtype_t  rs485_subtype_from_str(const char *s);
const char*      rs485_subtype_to_str(rs485_subtype_t st);

/* =========================
 * Dispatcher de leitura
 * =========================
 * Retorna:
 *  >=0 : número de medições gravadas em 'out'
 *  <0  : erro (tipo não suportado, falha do driver, etc.)
 */
int rs485_read_measurements(const rs485_sensor_t *sensor,
                            rs485_measurement_t *out, size_t out_len);

/* Varredura simples de uma lista de sensores já cadastrados */
int rs485_poll_all(const rs485_sensor_t *list, size_t count,
                   rs485_measurement_t *out, size_t out_len);

/* =========================
 * Probe / identificação
 * =========================
 * Varre os drivers conhecidos no endereço 'addr'.
 * Se algum reconhecer, retorna true e preenche:
 *  - out_type/out_subtype: tipo/subtipo para cadastro
 *  - out_used_fc: FC usada pelo driver (0x03/0x04)
 *  - out_driver_name: identificador do driver ("XY_MD02", "JSY_MK_333", ...)
 */
bool rs485_registry_probe_any(uint8_t addr,
                              rs485_type_t *out_type,
                              rs485_subtype_t *out_subtype,
                              uint8_t *out_used_fc,
                              const char **out_driver_name);

bool rs485_get_fixed_profile(uint16_t type, uint16_t subtype, rs485_profile_t *out);

#ifdef __cplusplus
}
#endif
#endif /* CONNECTIVITY_FIELDBUS_BUSES_RS485_INCLUDE_RS485_REGISTRY_H_ */

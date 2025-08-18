/*
 * rs485_registry.h
 *
 *  Created on: 13 de ago. de 2025
 *      Author: geopo
 */
/*
 * rs485_registry.h  — Alinhado com os tipos do front-end
 */

#ifndef COMMUNICATIONS_RS485_INCLUDE_RS485_REGISTRY_H_
#define COMMUNICATIONS_RS485_INCLUDE_RS485_REGISTRY_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ---------------- Tipos de sensor ----------------
typedef enum {
    RS485_TYPE_INVALID = 0,
    RS485_TYPE_ENERGIA,
    RS485_TYPE_TEMPERATURA,
    RS485_TYPE_FLUXO,         // "vazao" e "fluxo" mapeiam aqui
    RS485_TYPE_OUTRO,

    // Novos alinhados ao front:
    RS485_TYPE_TERMOHIGRO,    // "termohigrometro"/"termo-higrometro"
    RS485_TYPE_UMIDADE,       // "umidade" isolado
    RS485_TYPE_PRESSAO,       // "pressao"/"pressão"
    RS485_TYPE_GPS,           // "gps"
    RS485_TYPE_LUZ,           // "luz"
    RS485_TYPE_GAS,           // "gas"/"gás"
} rs485_type_t;

// Subtipo (hoje só energia usa)
typedef enum {
    RS485_SUBTYPE_NONE = 0,
    RS485_SUBTYPE_MONOFASICO,
    RS485_SUBTYPE_TRIFASICO,
} rs485_subtype_t;

// Estrutura tipada (após converter strings do front)
typedef struct {
    uint16_t        channel;    // p.ex. 3
    uint16_t        address;    // 1..247
    rs485_type_t    type;
    rs485_subtype_t subtype;    // só energia (mono/tri)
} rs485_sensor_t;

// ---------------- Tipos de medição ----------------
typedef enum {
    RS485_MEAS_TEMP_C,     // °C
    RS485_MEAS_HUM_PCT,    // %RH
    RS485_MEAS_POW_KW,     // kW (exemplo energia)
    RS485_MEAS_FLOW,       // Vazão (unidade definida no driver)
    RS485_MEAS_PRESSAO,    // Pressão (unidade definida no driver)
    RS485_MEAS_LUZ_LUX,    // Lux
    RS485_MEAS_GAS_PPM,    // ppm (exemplo)
    RS485_MEAS_GPS_LAT,    // graus
    RS485_MEAS_GPS_LON,    // graus
} rs485_meas_kind_t;

typedef struct {
    uint16_t          channel;   // canal lógico
    rs485_meas_kind_t kind;      // tipo da medição
    float             value;     // valor
} rs485_measurement_t;

// ---------------- Conversores string <-> enum ----------------
rs485_type_t     rs485_type_from_str(const char *s);
const char*      rs485_type_to_str(rs485_type_t t);
rs485_subtype_t  rs485_subtype_from_str(const char *s);
const char*      rs485_subtype_to_str(rs485_subtype_t st);

// ---------------- Dispatcher de leitura ----------------
int rs485_read_measurements(const rs485_sensor_t *sensor,
                            rs485_measurement_t *out, size_t out_len);

// Varre lista de sensores e enfileira medições
int rs485_poll_all(const rs485_sensor_t *list, size_t count,
                   rs485_measurement_t *out, size_t out_len);

#endif /* COMMUNICATIONS_RS485_INCLUDE_RS485_REGISTRY_H_ */


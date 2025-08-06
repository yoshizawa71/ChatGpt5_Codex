/*
 * pressure_calibrate.h
 *
 *  Created on: 19 de mai. de 2025
 *      Author: geopo
 */
#include "esp_err.h"
#include <stdbool.h>
#include "pressure_meter.h"

#ifndef PERIPHERALS_PRESSURE_INCLUDE_PRESSURE_CALIBRATE_H_
#define PERIPHERALS_PRESSURE_INCLUDE_PRESSURE_CALIBRATE_H_

typedef struct {
    float ref_value;
    float real_value;
    char unit[8];
} reference_point_t;

typedef enum {
    PRESSURE_UNIT_BAR,
    PRESSURE_UNIT_MCA,
    PRESSURE_UNIT_PSI
} pressure_unit_t;

float interpolate_pressure(float voltage, float *cal_voltages, float *ref_pressures_bar);
float bar_to_mca(float bar);
float get_calibrated_pressure(sensor_t leitor, pressure_unit_t unidade, bool *sensor_ok);

esp_err_t save_reference_points_sensor1(const reference_point_t *sensor1);
esp_err_t save_reference_points_sensor2(const reference_point_t *sensor2);
esp_err_t load_reference_points_sensor1(reference_point_t *points);
esp_err_t load_reference_points_sensor2(reference_point_t *points);



#endif /* PERIPHERALS_PRESSURE_INCLUDE_PRESSURE_CALIBRATE_H_ */

/*
 * pressure_meter.h
 *
 *  Created on: 5 de out. de 2024
 *      Author: geopo
 */

#ifndef DATALOGGER_DATALOGGER_CONTROL_INCLUDE_PRESSURE_METER_H_
#define DATALOGGER_DATALOGGER_CONTROL_INCLUDE_PRESSURE_METER_H_

#include "ads1015_reader.h"
#include <stdbool.h>
#include <stdint.h>

struct pressure_data {
    char       pressure1[8];
    char       pressure2[8];
};

/*struct pressure_index_control {
	uint32_t    last_write_press_idx;
    uint32_t    last_read_press_idx;
    uint32_t    total_counter;
};*/

/*struct record_pressure {
    char        date[11];
    char        time[9];
    char        pressure_1[8];
    char        pressure_2[8];
};*/

bool has_pressure_measurement_to_send(void);


esp_err_t save_pressure_measurement(int channel);

void save_default_pressure_index_control(void);

bool has_record_pressure_index_config(void);

bool has_pressure_data_set(void);

//------------------------------------------------------------------
//                        Pressure Sensor
//------------------------------------------------------------------

void save_pressure_data(struct pressure_data *config);
void get_pressure_data(struct pressure_data *config);


void start_pressure_sensor_read(void);

//void pressure_sensor_read(void);
//uint8_t pressure_sensor_read(void);
void pressure_sensor_read(bool *sensor_1, bool *sensor_2);

//float get_pressure_sensor_read(enum sensor leitor, float *Vmin, float *Fcorr, bool *zerado, bool *fcorr_cali, float P_ref);
//float get_pressure_sensor_read(enum sensor leitor, float *Vmin, float *Fcorr, bool no_pressure, bool correction_cali, float P_ref);
float get_pressure_sensor_read(sensor_t leitor, float *Vmin, float *Fcorr, bool no_pressure, bool correction_cali, float P_ref,bool *dev_pressure);
void pressao_referencia(float *vref);

void signal_pressao_referencia(float *Vref);


bool has_pressure_measurement_to_send(void);
void init_pressure_sensor_read(void);
//void start_pressure_sensor_read(void);
//void pressure_sensor_read(void);

void init_pressure_task(void);
//void init_pressure_meter_task(void);
void init_pressure_meter_config(void);
bool has_pressure_data_to_send(void);

#endif /* DATALOGGER_DATALOGGER_CONTROL_INCLUDE_PRESSURE_METER_H_ */

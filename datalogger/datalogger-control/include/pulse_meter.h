/*
 * pulse_meter.h
 *
 *  Created on: 5 de out. de 2024
 *      Author: geopo
 */

#ifndef DATALOGGER_DATALOGGER_CONTROL_INCLUDE_PULSE_METER_H_
#define DATALOGGER_DATALOGGER_CONTROL_INCLUDE_PULSE_METER_H_


#include <stdint.h>
#include "esp_err.h"
#include "stdbool.h"
#include "time.h"
#include <sys/time.h>

struct record_pulse_config {
    uint32_t    last_write_idx;
    uint32_t    last_read_idx;
    uint32_t    total;
    uint32_t    last_pulse_count;
    uint32_t    current_pulse_count;
    uint32_t    last_saved_daykey; 
};

struct record_pulse {
    char        date[11];
    char        time[9];
    float       flow;
    char        total_pulses[8];
};

// Estrutura para armazenar os dados
typedef struct {
    float vazao;         // Valor da vazão (em litros/s)
    uint32_t timestamp;  // Timestamp em segundos desde o epoch
} FlowData;


void set_last_pulse_time(void);
time_t get_last_pulse_time(void);
void set_last_pulse_milisec(void);
time_t get_last_pulse_milisec(void);

bool pulse_meter_inactivity_task_time(void);

void save_measurement_from_sleep(uint32_t pulse_counter);
void reset_pulse_meter(void);
bool has_measurement_to_send(void);
bool send_keep_alive_to_server(void);
bool server_connection(void);
bool send_data_to_server(void);
void Pulse_Meter_Task(void* pvParameters);
void init_pulse_meter_task(void);
//void update_pulse_count(void);
float flow(uint32_t count);
uint32_t get_measured_pulse_count(void);
void pulse_meter_prepare_for_sleep(void);

esp_err_t save_pulse_measurement(int);

//PLUVIOMETER FUNCTIONS
void init_pulse_meter(void);
void init_pulse_meter_task(void);
void pulse_meter_task(void);

//void set_last_pulse_time(void);
//time_t get_last_pulse_time(void);



//void save_record(uint32_t* record_index, struct record record);
//void save_record_pulse(uint32_t* record_index, struct record_pulse record);

//void read_record(uint32_t record_index, struct record_sensors_data* record);
//void read_record_pulse(uint32_t record_index, struct record_pulse* record);

//void save_pulse_counter_data_set(struct pulse_counter_dataset *config);
//void get_pulse_counter_data_set(struct pulse_counter_dataset *config);


#endif /* DATALOGGER_DATALOGGER_CONTROL_INCLUDE_PULSE_METER_H_ */

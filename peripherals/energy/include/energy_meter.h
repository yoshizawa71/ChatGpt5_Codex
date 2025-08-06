/*
 * energy_meter.h
 *
 *  Created on: 20 de nov. de 2024
 *      Author: geopo
 */


#ifndef PERIPHERALS_ENERGY_INCLUDE_ENERGY_METER_H_
#define PERIPHERALS_ENERGY_INCLUDE_ENERGY_METER_H_

#include "ads1015_reader.h"

static uint32_t current_total_counter = 0;

struct energy_measured {
	float       V1;
	float       V2;
    char       last_power_1[8];
    char       last_power_2[8];
};

struct energy_index_control {
	uint32_t    last_write_energy_idx;
    uint32_t    last_read_energy_idx;
    uint32_t    total_counter;
};

struct record_energy {
    char        date[11];
    char        time[9];
    char        power_1[8];
    char        power_2[8];
};

void save_default_energy_index_control(void);
bool has_energy_data_to_send(void);

bool has_record_energy_index_config(void);
void save_energy_index_control(struct energy_index_control *config);
void get_energy_index_control(struct energy_index_control *config);

void save_energy_measured(struct energy_measured *config);
void get_energy_measured(struct energy_measured *config);

void init_energy_task(void);



#endif /* PERIPHERALS_ENERGY_INCLUDE_ENERGY_METER_H_ */

/*
 * data_register.h
 *
 *  Created on: 3 de jun. de 2024
 *      Author: geopo
 */

#ifndef DATALOGGER_DATALOGGER_DRIVER_INC_DATA_REGISTER_H_
#define DATALOGGER_DATALOGGER_DRIVER_INC_DATA_REGISTER_H_
#include "pressure_meter.h"

struct record_data_saved {
    char        date[11];
    char        time[9];
    uint8_t     channel;
    char        data[8];
};

esp_err_t mount_sd_card(void);
esp_err_t mount_sdcard_littlefs(void);
void unmount_sd_card(void);
esp_err_t unmount_sdcard_littlefs(void);

esp_err_t has_SD_FILE_Created(void);
bool has_SD_FILE_Deleted_Pulse(void);
bool has_SD_FILE_Deleted_Pressure(void);

//bool read_record_file(uint32_t* byte_to_read, char* str);
//void save_record_pulse(uint32_t* record_index, struct record_pulse record);
bool read_record_file_sd(uint32_t* byte_to_read, char* str);
//void read_record_pulse(uint32_t record_index, struct record_pulse* record);
bool read_record_pulse_file(uint32_t* byte_to_read, char* str);

//void save_record_pressure(uint32_t* record_index, struct record_pressure record,int channel);
//void read_record_pressure(uint32_t record_index, struct record_pressure * record, int channel);
bool read_record_pressure_file(uint32_t* byte_to_read, char* str);

esp_err_t save_record_sd(int channel, char *data);
esp_err_t read_record_sd(uint32_t *cursor_pos, struct record_data_saved* record_data);
esp_err_t delete_record_sd(void);
void index_config_init(void);


#endif /* DATALOGGER_DATALOGGER_DRIVER_INC_DATA_REGISTER_H_ */

#ifndef DATALOGGER_CONTROL_H
#define DATALOGGER_CONTROL_H

#include <stdbool.h>
#include <stdint.h>
#include "datalogger_driver.h"
#define TIME_REFERENCE         1609470000 // Tempo definido para 0 horas de Janeiro de 2021

//BLINK FUNCTIONS & DATA
/*enum blink_profile
{
    BLINK_PROFILE_NONE,
    BLINK_PROFILE_1,
    BLINK_PROFILE_2,
    BLINK_PROFILE_3,
    BLINK_PROFILE_DEVICE_RUNNING,
    BLINK_PROFILE_COMM_OK,
    BLINK_PROFILE_COMM_FAIL,
    BLINK_PROFILE_NO_SIGNAL,
    BLINK_PROFILE_WEAK_SIGNAL,
    BLINK_PROFILE_FAIR_SIGNAL,
    BLINK_PROFILE_STRONG_SIGNAL
};

void blink_setup(void);
void blink_task( void * pvParameters );
void blink_profile_set (enum led_pin led_pin, enum blink_profile profile);*/

//SLEEP FUNCTIONS AND DEFINES


//GPRS CONTROL FUNCTIONS
void get_gsm_time(void); //pegar hora da rede GSM
bool gprs_network_connect(void);
void gprs_network_setup(void);
bool is_gprs_network_ready(void);
void power_down_gprs(void);


//OTA FUNCTIONS
void ota_start_transfer(void);
bool check_update(int *http_status);
char* get_ota_version();

/*//PLUVIOMETER FUNCTIONS
void init_pulse_meter(void);
void init_pulse_meter_task(void);
void pulse_meter_task(void);
bool send_keep_alive_to_server(void);
/----------------------------
bool server_connection(void);
/----------------------------
bool send_data_to_server(void);
void save_measurement_from_sleep(void);
/--------------------
void save_default_record_config(void);
/--------------------
bool has_measurement_to_send(void);
void reset_pulse_meter(void);
bool pulse_meter_inactivity_task_time(void);

//void set_last_pulse_time(void);
//time_t get_last_pulse_time(void);

void set_last_pulse(void);
time_t get_last_pulse(void);

void set_last_pulse_milisec(void);
time_t get_last_pulse_milisec(void);*/

//CONFIG FUNCTIONS
void init_config_control(void);
void save_config(void);
void save_system_config_data_time(void);
char * get_apn(void);
void set_apn(char* apn);
char * get_lte_user(void);
void set_lte_user(char* lte_user);
char * get_lte_pw(void);
void set_lte_pw(char* lte_pw);

char * get_data_server_url(void);
void set_data_server_url(char* data_server_url);
uint16_t get_data_server_port(void);
void set_data_server_port(uint16_t port);
char * get_data_server_path(void);
void set_data_server_path(char* data_server_path);

void enable_network_mqtt(bool enable);
bool has_network_mqtt_enabled(void);
char * get_mqtt_url(void);
void set_mqtt_url(char* mqtt_url);
uint16_t get_mqtt_port(void);
void set_mqtt_port(uint16_t port);
char * get_mqtt_topic(void);
void set_mqtt_topic(char* mqtt_topic);

void enable_network_http(bool enable);
bool has_network_http_enabled(void);

char * get_network_user(void);
void set_network_user(char* user);
char * get_network_token(void);
void set_network_token(char* token);
char * get_network_pw(void);
void set_network_pw(char* pw);
bool has_network_user_enabled(void);
void enable_network_user(bool enable);
bool has_network_token_enabled(void);
void enable_network_token(bool enable);
bool has_network_pw_enabled(void);
void enable_network_pw(bool enable);
//--------------------------------
//Servidor de Configuração OTA
//--------------------------------
char * get_config_server_url(void);
void set_config_server_url(char* config_server_url);
uint32_t get_config_server_port(void);
void set_config_server_port(uint32_t port);
char * get_config_server_path(void);
void set_config_server_path(char* config_server_path);
uint32_t get_level_min(void);
void set_level_min(uint32_t level_min);
uint32_t get_level_max(void);
void set_level_max(uint32_t level_max);
//--------------------------------

char * get_device_id(void);
void set_device_id(char* id);
char * get_name(void);
void set_name(char* name);
char * get_phone(void);
void set_phone(char* phone);
char * get_ssid_ap(void);
void set_ssid_ap(char* ssid);
char * get_password_ap(void);
void set_password_ap(char* password);
bool has_activate_sta(void);
void set_activate_sta(bool activate_sta);
char * get_ssid_sta(void);
void set_ssid_sta(char* ssid);
char * get_password_sta(void);
void set_password_sta(char* password);

/*bool has_check_send_freq_mode(void);
void set_activate_send_freq_mode(bool activate_freq_mode);
bool has_check_send_time_mode(void);
void set_activate_send_time_mode(bool activate_time_mode);*/

void    set_send_mode(const char *mode);
const char *get_send_mode(void);
bool    is_send_mode_freq(void);
bool    is_send_mode_time(void);
void set_send_time(int index, int hour);
int get_send_time1(void);
int get_send_time2(void);
int get_send_time3(void);
int get_send_time4(void);

uint32_t get_send_period(void);
void set_send_period(uint32_t send_period);
uint32_t get_deep_sleep_period(void);
void set_deep_sleep_period(uint32_t deep_sleep_period);
char * get_date(void);
void set_date(char* date);
char * get_time(void);
void set_time(char* time);
//**************
int get_hour_minutes_seconds_in_seconds(void);
int get_time_hour(void);
int get_time_minute(void);
int get_time_second(void);
//**************
uint32_t get_scale(void);
void set_scale(uint32_t volume);
float get_flow_rate(void);
void set_flow_rate(float vazao);
bool has_factory_config(void);
void set_factory_config(bool factory_config);
bool has_device_active(void);
void set_device_active(bool turn_on_off);
void config_system_time(void);
void log_system_time(void);

void get_keep_alive_time(struct tm* timeinfo);
char * get_serial_number(void);
void set_serial_number(char* serial_number);
char * get_company(void);
void set_company(char* company);
char * get_deep_sleep_start(void);
void set_deep_sleep_start(char* deep_sleep_start);
char * get_deep_sleep_end(void);
void set_deep_sleep_end(char* deep_sleep_end);
bool has_reset_count(void);
void enable_reset_count(bool enable);
char * get_keep_alive(void);
void set_keep_alive(char* keep_alive);
bool has_log_level_1(void);
void enable_log_level_1(bool enable);
bool has_log_level_2(void);
void enable_log_level_2(bool enable);
//-----------------------
bool has_calibration(void);
void enable_calibration(bool enable);
/*
bool has_level_zero(void);
void enable_level_zero(bool enable);
bool has_correction(void);
void enable_correction(bool enable);

char * get_reference_cali1(void);
void set_reference_cali1(char* reference);
char * get_reference_cali2(void);
void set_reference_cali2(char* reference);
*/
char * get_analog_data1(void);
void set_analog_data1(char* analog_data);
char * get_analog_data2(void);
void set_analog_data2(char* analog_data);
//-----------------------

bool has_enable_post(void);
void enable_post(bool enable);
bool has_enable_get(void);
void enable_get(bool enable);

//-----------------------
//Pulse
//-----------------------
bool should_save_pulse_zero(void);
void save_pulse_zero(bool save_pulse_zero);
bool should_send_value(void);
//void send_value(bool send_value);
void set_last_pulse_count(uint32_t counter);
uint32_t get_last_pulse_count(void);
void set_current_pulse_count(uint32_t counter);
uint32_t get_current_pulse_count(void);

//void set_pulse_count_from_ulp (uint32_t pulse_count);
uint32_t get_pulse_count_from_ulp (void);


//-----------------------
//Pressure
//-----------------------
struct pressure_data get_saved_pressure_data(void);

char * get_pressure_data1(void);

void set_pressure_data1(char* pressure_data_value);

char * get_pressure_data2(void);

void set_pressure_data2(char* pressure_data_value);


//--------------------------------------------------------------
//Servidor OTA
//--------------------------------------------------------------
char * get_config_server_ota_url(void);
void set_config_server_ota_url(char* config_server_url);
uint32_t get_config_server_ota_port(void);
void set_config_server_ota_port(uint32_t port);
char * get_config_server_ota_path(void);
void set_config_server_ota_path(char* config_server_path);

//----------------------------
uint16_t get_connection_gsm_network_error_count(void);
void set_connection_gsm_network_error_count(uint16_t count);
uint16_t get_connection_server_error_count(void);
void set_connection_server_error_count(uint16_t count);

uint32_t get_last_minute(void);
void set_last_minute(uint32_t count);

uint32_t get_csq(void);
void set_csq(uint32_t csq);

uint32_t get_power_source(void);
float get_power_source_volts(void);
void set_power_source(uint32_t power_source);

uint32_t get_battery(void);
void set_battery(uint32_t battery);

//----------------------------
time_t get_last_data_sent(void);
void set_last_data_sent(time_t date);
time_t get_last_sys_time(void);
void set_last_sys_time(time_t date);
bool check_enable_modem(void);
bool is_modem_enabled(void);
void enable_modem(bool en);
bool check_enable_led(void);
bool is_led_enabled(void);
void enable_led(bool en);
//----------------------------*



#endif

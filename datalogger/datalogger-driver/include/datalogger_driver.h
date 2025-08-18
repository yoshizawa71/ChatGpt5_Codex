#ifndef DATALOGGER_DRIVER_H
#define DATALOGGER_DRIVER_H

#include <stdbool.h>
#include <stdint.h>
#include <float.h>
#include "esp_modem.h"
#include "pulse_meter.h"
#include "pressure_meter.h"

//PCNT FUNCTIONS
typedef struct {
    int unit;  // the PCNT unit that originated an interrupt
    uint32_t status; // information on the event type that caused the interrupt
} pcnt_evt_t;

#define PCNT_UNIT       PCNT_UNIT_0
#define PCNT_CHANNEL    PCNT_CHANNEL_0
#define PCNT_H_LIM_VAL      0
#define PCNT_L_LIM_VAL      -1
#define PCNT_THRESH1_VAL    0
#define PCNT_THRESH0_VAL    -1
#define PCNT_INPUT_PIN  36 // Pulse Input GPIO

#define RS485_MAX_SENSORS 10

void init_pcnt(void);
int16_t get_pulse_count(void);
void reset_pulse_count(void);

// LED FUNCTIONS
/*enum led_pin {
        LED_COM,
        LED_STATUS,
        LED_SIGNAL
};

void led_setup(void);
void turn_led_on(enum led_pin led_pin, bool on);
void prepare_led_for_sleep(void);*/

// TIMER FUNCTIONS
uint64_t get_timestamp_ms(void);

//UART MODEM FUNCTIONS
void config_modem_uart(esp_modem_dte_config_t* config); 
void turn_on_modem(void);
void turn_off_modem(void);

//SARA RS422
void pwr_ctrl(void);

//WIFI AP FUNCTIONS
/*#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
void ap_restart_cb(void* arg);
esp_err_t start_wifi_ap(void);
esp_err_t start_wifi_ap_sta(void);
//void stop_wifi_ap(void);
void stop_wifi_ap_sta(void);*/
void get_mac_address(char* baseMacChr);




//void Wakeup_Pulse_Task(void* pvParameters);

//UART RS485
void init_rs485_log_console(void);
void deinit_rs485_log_console(void);

// RTS for RS485 Half-Duplex Mode manages DE/~RE
#define RS485_RTS   18 //(CONFIG_ECHO_UART_RTS)

#define RS485TX 1 //(CONFIG_ECHO_UART_TXD)
#define RS485RX 3 //(CONFIG_ECHO_UART_RXD)

// CTS is not used in RS485 Half-Duplex Mode
#define RS485_CTS   (UART_PIN_NO_CHANGE)

#define BUF_SIZE             (2048)
#define UART_PORT_NUM          0 //(UART 0)

//Pressure sensor Functions


//Rele
void rele_init(void);
void rele_turn_on(void);
void rele_turn_off(void);

void turn_rele_On_Off(int time);

//CONFIG STRUCTS

struct device_config {
    char        id[30];
    char        name[35];
    char        phone[30];
    char        ssid_ap[30];
    char        wifi_password_ap[30];
    bool        activate_sta;
    char        ssid_sta[30];
    char        wifi_password_sta[30];
     // modos de envio
    char        send_mode[6];
    uint32_t    send_period;               // minutos, modo “freq”
    uint8_t     send_times[4];             // horas (0–23), modo “time”
    uint32_t    deep_sleep_period;         // frequência de gravação
    char        date[11];
    char        time[9];
    uint32_t    scale;
    float       flow_rate;
//    bool        send_value;
    bool        save_pulse_zero;
    bool        finished_factory;
    bool        device_active;
};

struct network_config {
    char        apn[30];
    char        lte_user[30];
    char        lte_pw[30];
    char        data_server_url[50];
    uint16_t    data_server_port;
    char        data_server_path[30];
    char        user[30];
    char        token[30];
    char        pw[30];
    char        mqtt_url[50];
    uint16_t    mqtt_port;
    char        mqtt_topic[30];   
    bool        user_en;
    bool        token_en;
    bool        pw_en;
    bool        http_en;
    bool        mqtt_en;
};

struct operation_config {
    char        serial_number[30];
    char        deep_sleep_start[9];
    char        deep_sleep_end[9];
    bool        reset_count;
    char        keep_alive[9];
    bool        log_level_1;
    bool        log_level_2;
    bool        enable_post;
    bool        enable_get;
    char        company[30];
    char        config_server_url[50];
    uint32_t    config_server_port;
    char        config_server_path[20];
    uint32_t    level_min;
    uint32_t    level_max;  
};

struct maintenance_config {
    char        analog_data1[8];
    char        analog_data2[8];
    bool        calibration;
};

struct record_index_config {
    uint32_t    last_write_idx;
    uint32_t    last_read_idx;
    uint32_t    total_idx;
    uint32_t    cursor_position;
};

//-------------------------------------
//  RS485
//-------------------------------------

typedef struct {
    uint8_t  channel;           // 3…13
    uint8_t  address;           // 1…247
    char     type[16];          // "energia", "temperatura", ...
    char     subtype[16];       // "monofasico"/"trifasico" ou ""
} sensor_map_t;

// protótipos
esp_err_t save_rs485_config(const sensor_map_t *map, size_t count);
esp_err_t load_rs485_config(sensor_map_t *map, size_t *count);

struct system_config {
//    uint32_t gsm_network_error_count;
	uint16_t connection_gsm_network_error_count;
	uint16_t connection_server_error_count;
    time_t last_data_sent;
    time_t last_sys_time;
    bool modem_enabled;
    bool led_enabled;
};

struct record_last_unit_time {
	uint32_t last_minute;
};

struct self_monitoring_data{
	uint16_t csq;
	uint16_t power_source;
	uint16_t battery;
};
extern struct self_monitoring_data self_monitoring_data; // declaração, não definição

void mount_driver(void);
void unmount_driver(void);
bool has_device_config(void);
bool has_network_config(void);
bool has_operation_config(void);
bool has_record_index_config(void);
bool has_record_pulse_config(void);


//bool has_record_pressure_index_config(void);

bool has_system_config(void);

//**********
bool has_record_last_unit_time(void);

bool has_self_monitoring_data(void);

bool has_pressure_data(void);

//-----------------------
void save_device_config(struct device_config *config);
void get_device_config(struct device_config *config);
void save_network_config(struct network_config *config);
void get_network_config(struct network_config *config);

void save_record_pulse_config(struct record_pulse_config *config);
void get_record_pulse_config(struct record_pulse_config *config);

esp_err_t save_index_config(struct record_index_config *config);
esp_err_t get_index_config(struct record_index_config *config);

//void save_pressure_index_control(struct pressure_index_control *config);
//void get_pressure_index_control(struct pressure_index_control *config);

void save_operation_config(struct operation_config *config);
void get_operation_config(struct operation_config *config);
void save_system_config(struct system_config *config);
void get_system_config(struct system_config *config);
//-------------------------
void save_record_last_unit_time(struct record_last_unit_time *config);
void get_record_last_unit_time(struct record_last_unit_time *config);

esp_err_t save_self_monitoring_data(struct self_monitoring_data *config);
esp_err_t load_self_monitoring_data(struct self_monitoring_data *config);

/*void save_pressure_data_set(struct pressure_dataset *config);
void get_pressure_data_set(struct pressure_dataset *config);*/

//**********
//SD FUNCTIONS

#define UNSPECIFIC_RECORD 0x7FFFFFFFU




//SERVER COMMUNICATION
//esp_err_t json_data_payload(char* server_payload, size_t payload_size, uint32_t* counter_out, uint32_t* cursor_position_out);
bool simulate_send_data_to_server(void);
//esp_err_t json_data_payload(char* server_payload, size_t payload_size, struct record_index_config rec_index,uint32_t* counter_out);
esp_err_t json_data_payload(char* server_payload, size_t payload_size, struct record_index_config rec_index,uint32_t* counter_out,uint32_t *cursor_position);
esp_err_t build_custom_payload(char *buf,
                               size_t bufSize,
                               struct record_index_config rec_index,
                               uint32_t *counter_out,
                               uint32_t *cursor_position);
void server_comm_init(void);
//esp_err_t update_index_after_send(uint32_t counter, uint32_t cursor_position);
#endif

/**
 * @brief   HTTP Client events id
 */


//Little FS functions

void listFilesInDir(void);
void littlefs_read_file_with_content(const char* filename);

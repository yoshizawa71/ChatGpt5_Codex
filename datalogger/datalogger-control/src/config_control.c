#include "datalogger_control.h"
#include <string.h>
#include <stdint.h>
#include "datalogger_driver.h"
#include "pulse_meter.h"
#include "pressure_meter.h"
#include "energy_meter.h"
#include "esp_log.h"
#include "time.h"
#include <sys/time.h>

static const char *TAG = "Config_Control";

static struct device_config dev_config = {0};
static struct network_config net_config = {0};
static struct operation_config op_config = {0};
static struct maintenance_config maint_config = {0};
static struct system_config system_config = {0};
static struct record_last_unit_time record_last_unit_time = {0};
struct self_monitoring_data self_monitoring_data = {0};
static struct record_pulse_config pulse = {0};
static struct pressure_data pressure_data = {0};


static void save_default_device_config(void);
static void save_default_network_config(void);
static void save_default_operation_config(void);
static void save_default_system_config(void);
static void save_default_record_last_unit_time(void);
static void save_default_self_monitoring_data(void);

static void save_default_pressure_data(void);

static uint8_t g_weg_payload_mode = 0; // 0=energy (padrão), 1=water

static SemaphoreHandle_t myMutex;

void de_init_config_control(void)
{
    unmount_driver();
}

void init_config_control(void)
{
    if (myMutex == NULL)
    {
        myMutex = xSemaphoreCreateMutex();
        if (myMutex == NULL)
        {
            ESP_LOGE(TAG, "Falha ao criar myMutex");
        }
    }


    if (!has_network_config())
    {
        save_default_network_config();
    }
    else
    {
        get_network_config(&net_config);
    }
    
    if (!has_device_config())
    {
        save_default_device_config();
    }
    else
    {
        get_device_config(&dev_config);
    }

    if (!has_operation_config())
    {
        save_default_operation_config();
    }
    else
    {
        get_operation_config(&op_config);
    }

    if (!has_system_config())
    {
        save_default_system_config();
    }
    else
    {
        get_system_config(&system_config);
    }

    if (!has_record_last_unit_time())
       {
           save_default_record_last_unit_time();
       }
       else
       {
           get_record_last_unit_time(&record_last_unit_time);
       }

    if (!has_self_monitoring_data())
       {
           save_default_self_monitoring_data();
       }
       else
       {
    	   load_self_monitoring_data(&self_monitoring_data);
       }

    if (!has_pressure_data())
         {

    	save_default_pressure_data();
         }
         else
         {
      	   get_pressure_data(&pressure_data);
         }

}

void save_config(void) {
    xSemaphoreTake(myMutex, portMAX_DELAY);
    ESP_LOGI(TAG, "Antes de salvar - dev_config.date: %s, dev_config.time: %s", dev_config.date, dev_config.time);
    save_device_config(&dev_config);
    ESP_LOGI(TAG, "Após save_device_config - dev_config.date: %s, dev_config.time: %s", dev_config.date, dev_config.time);
    save_network_config(&net_config);
    save_operation_config(&op_config);
    save_system_config(&system_config);
    save_record_last_unit_time(&record_last_unit_time);
    save_self_monitoring_data(&self_monitoring_data);
    save_pressure_data(&pressure_data);
    xSemaphoreGive(myMutex);
}

void save_system_config_data_time(void)
{
    xSemaphoreTake(myMutex,portMAX_DELAY);
    time_t system_time;
    time(&system_time);
    set_last_sys_time(system_time);
	save_system_config(&system_config);
    xSemaphoreGive(myMutex);
}

void config_system_time(void)
{
    struct tm tm;

    tm.tm_year = (2000 + (dev_config.date[8] - '0')*10 + (dev_config.date[9] - '0') ) - 1900;
    tm.tm_mon = (dev_config.date[3] - '0')*10 + (dev_config.date[4] - '0') - 1;
    tm.tm_mday = (dev_config.date[0] - '0')*10 + (dev_config.date[1] - '0');

    tm.tm_hour = (dev_config.time[0] - '0')*10 + (dev_config.time[1] - '0');
    tm.tm_min = (dev_config.time[3] - '0')*10 + (dev_config.time[4] - '0');
    tm.tm_sec = (dev_config.time[6] - '0')*10 + (dev_config.time[7] - '0');

    setenv("TZ", "GMT-03:00", 1);
    tzset();

    time_t t = mktime(&tm);

    t+= 3*60*60;

    system_config.last_sys_time = t;
    vTaskDelay(pdMS_TO_TICKS(10));
    struct timeval now = { .tv_sec = t };
    settimeofday(&now, NULL);
    save_system_config_data_time();
}


void log_system_time(void)
{
    xSemaphoreTake(myMutex, portMAX_DELAY);
    time_t now;
    time(&now); // Obtém o horário atual
    struct tm timeinfo;
    setenv("TZ", "GMT-03:00", 1);
    tzset();
    localtime_r(&now, &timeinfo);
    char date_str[11];
    char time_str[9];
    strftime(date_str, sizeof(date_str), "%d/%m/%Y", &timeinfo);
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "Last System Time: %ld", now);
    ESP_LOGI(TAG, "Data: %s", date_str);
    ESP_LOGI(TAG, "Hora: %s", time_str);
    ESP_LOGI(TAG, "Escala: %u", dev_config.scale);
    xSemaphoreGive(myMutex);
}
//-------------------------------------------------------------------

struct pressure_data get_saved_pressure_data(void)
{
	get_pressure_data(&pressure_data);
	return pressure_data;

}

/*void save_pressure_measurement(struct pressure_dataset send_to_save)
{
		printf("pressure_dataset.vmin1= %f\n", send_to_save.vmin1);
		printf("pressure_dataset.vmin2= %f\n", send_to_save.vmin2);
		printf("pressure_dataset.fcorr1= %f\n", send_to_save.fcorr1);
		printf("pressure_dataset.fcorr2= %f\n", send_to_save.fcorr2);

		printf("pressure_dataset.pressure1= %s\n", send_to_save.pressure1);
		printf("pressure_dataset.pressure2= %s\n", send_to_save.pressure2);
		printf("pressure_dataset.last_pressure1= %s\n", send_to_save.last_pressure1);
		printf("pressure_dataset.last_pressure2= %s\n", send_to_save.last_pressure2);
		printf("pressure_dataset.no_pressure1= %s\n", send_to_save.no_pressure1 ? "true" : "false");
		printf("pressure_dataset.no_pressure2= %s\n", send_to_save.no_pressure2 ? "true" : "false");


	save_pressure_data_set(&send_to_save);

}*/




static void save_default_device_config(void)
{
//    get_mac_address(dev_config.id);
	strcpy(dev_config.id ,"COGNETi_TEST");
	strcpy(dev_config.name, "RESERVA_00");
	strcpy(dev_config.phone, "");
	strcpy(dev_config.ssid_ap,"COGNETi_TEST");
    strcpy(dev_config.wifi_password_ap, "cogneti123");
    dev_config.activate_sta = false;
    strcpy(dev_config.ssid_sta,"");
    strcpy(dev_config.wifi_password_sta, "");
	strcpy(dev_config.send_mode, "freq");
	for(int i = 0; i < 4; i++) {
        dev_config.send_times[i] = -1;   // sentinela “sem valor”
    }
    dev_config.send_period = 15;
    dev_config.deep_sleep_period = 5;
    dev_config.scale = 1;
    dev_config.save_pulse_zero = true;
    dev_config.finished_factory = false;
    dev_config.always_on = false;
    dev_config.device_active = false;
    strcpy(dev_config.date, "");
    strcpy(dev_config.time, "00:00:00");
   
    save_device_config(&dev_config);

}

static void save_default_network_config(void)
{
    strcpy(net_config.apn, "virtueyes.com.br");
    strcpy(net_config.lte_user, "virtu");
    strcpy(net_config.lte_pw, "virtu");
    net_config.http_en = false;
    strcpy(net_config.data_server_url, "cogneti.ddns.net");
    net_config.data_server_port = 8899;
    strcpy(net_config.data_server_path, "/post");
    strcpy(net_config.user, "");
    strcpy(net_config.token, "");
    strcpy(net_config.pw, "");
    net_config.user_en = false;
    net_config.token_en = false;
    net_config.pw_en = false;
    net_config.mqtt_en = false;
    strcpy(net_config.mqtt_url, "cogneti.ddns.net");
    net_config.mqtt_port = 9999;
    strcpy(net_config.mqtt_topic, "/topic/qos1");

    save_network_config(&net_config);
}

static void save_default_operation_config(void)
{
//    get_mac_address(op_config.serial_number);
	strcpy(op_config.serial_number, "COGNETi_TEST");
	strcpy(op_config.company, "COGNETi-Sistemas");
    strcpy(op_config.deep_sleep_start, "19:00:00");
    strcpy(op_config.deep_sleep_end, "07:00:00");
    op_config.reset_count = true;
    strcpy(op_config.keep_alive, "00:00:00");
/*    op_config.log_level_1 = false;
    op_config.log_level_2 = false;

    strcpy(op_config.led_start, "07:00:00");
    strcpy(op_config.led_end, "19:00:00");*/
    op_config.enable_post = true;
    op_config.enable_get = false;

    strcpy(op_config.config_server_url, "");
    op_config.config_server_port = 4044;
    strcpy(op_config.config_server_path, "/latestbin");
    
    op_config.level_min = 0;
    op_config.level_max = 100;
    
    save_operation_config(&op_config);
}

static void save_default_system_config(void)
{
	system_config.connection_gsm_network_error_count = 0;
	system_config.connection_server_error_count = 0;
    system_config.last_data_sent = 0;
    system_config.last_sys_time = 0;
    system_config.modem_enabled = true;
    system_config.led_enabled = true;

    save_system_config(&system_config);
}

static void save_default_record_last_unit_time(void)
{
	record_last_unit_time.last_minute = 0;
    save_record_last_unit_time(&record_last_unit_time);
}

static void save_default_self_monitoring_data(void)
{
	self_monitoring_data.csq = 0;
	self_monitoring_data.battery = 0;
    save_self_monitoring_data(&self_monitoring_data);

}

static void save_default_pressure_data(void)
{

strcpy(pressure_data.pressure1 ,"");
strcpy(pressure_data.pressure2 ,"");


save_pressure_data(&pressure_data);
}

//-------------------------------------------------------------------
char * get_apn(void)
{
    return net_config.apn;
}

void set_apn(char* apn)
{
    strcpy(net_config.apn, apn);
}

char * get_lte_user(void)
{
    return net_config.lte_user;
}

void set_lte_user(char* lte_user)
{
    strcpy(net_config.lte_user, lte_user);
}

char * get_lte_pw(void)
{
    return net_config.lte_pw;
}

void set_lte_pw(char* lte_pw)
{
    strcpy(net_config.lte_pw, lte_pw);
}

char * get_data_server_url(void)
{
    return net_config.data_server_url;
}

void set_data_server_url(char* data_server_url)
{
    strcpy(net_config.data_server_url, data_server_url);
}

uint16_t get_data_server_port(void)
{
    return net_config.data_server_port;
}

void set_data_server_port(uint16_t port)
{
    net_config.data_server_port = port;
}

char * get_data_server_path(void)
{
//	printf(">>>>GET HTTP PATH<<<%s\n", net_config.data_server_path);
    return net_config.data_server_path;
}

void set_data_server_path(char* data_server_path)
{
//	printf(">>>>SET HTTP PATH<<<%s\n", data_server_path);
    strcpy(net_config.data_server_path, data_server_path);
}

//-----------------------------


void enable_network_mqtt(bool enable)
{
     net_config.mqtt_en = enable;
}

bool has_network_mqtt_enabled(void)
{
	return  net_config.mqtt_en;
}


char * get_mqtt_url(void)
{
    return net_config.mqtt_url;
}

void set_mqtt_url(char* mqtt_url)
{
    strcpy(net_config.mqtt_url, mqtt_url);
}

uint16_t get_mqtt_port(void)
{
    return net_config.mqtt_port;
}

void set_mqtt_port(uint16_t port)
{
    net_config.mqtt_port = port;
}

char * get_mqtt_topic(void)
{
    return net_config.mqtt_topic;
}

void set_mqtt_topic(char* mqtt_topic)
{
    strcpy(net_config.mqtt_topic, mqtt_topic);
}
//-----------------------------

void enable_network_http(bool enable)
{
     net_config.http_en = enable;
}

bool has_network_http_enabled(void)
{
	return  net_config.http_en;
}

char * get_network_user(void)
{
    return net_config.user;
}

void set_network_user(char* user)
{
    strcpy(net_config.user, user);
}

char * get_network_token(void)
{
    return net_config.token;
}

void set_network_token(char* token)
{
    strcpy(net_config.token, token);
}

char * get_network_pw(void)
{
    return net_config.pw;
}

void set_network_pw(char* pw)
{
    strcpy(net_config.pw, pw);
}

bool has_network_user_enabled(void)
{
    return net_config.user_en;
}

void enable_network_user(bool enable)
{
    net_config.user_en = enable;
}

bool has_network_token_enabled(void)
{
    return net_config.token_en;
}

void enable_network_token(bool enable)
{
    net_config.token_en = enable;
}

bool has_network_pw_enabled(void)
{
    return net_config.pw_en;
}

void enable_network_pw(bool enable)
{
    net_config.pw_en = enable;
}

char * get_device_id(void)
{
    return dev_config.id;
}

void set_device_id(char* id)
{
    strcpy(dev_config.id, id);
}

char * get_name(void)
{
    return dev_config.name;
}

void set_name(char* name)
{
    strcpy(dev_config.name, name);
}

char * get_phone(void)
{
    return dev_config.phone;
}

void set_phone(char* phone)
{
    strcpy(dev_config.phone, phone);
}

char * get_ssid_ap(void)
{
    return dev_config.ssid_ap;
}

void set_ssid_ap(char* ssid)
{
    strcpy(dev_config.ssid_ap, ssid);
}

char * get_password_ap(void)
{
    return dev_config.wifi_password_ap;
}

void set_password_ap(char* password)
{
    strcpy(dev_config.wifi_password_ap, password);
}

bool has_activate_sta(void)
{
    return dev_config.activate_sta;
}

void set_activate_sta(bool activate_sta)
{
    dev_config.activate_sta = activate_sta;
}

char * get_ssid_sta(void)
{
    return dev_config.ssid_sta;
}

void set_ssid_sta(char* ssid)
{
    strcpy(dev_config.ssid_sta, ssid);
}

char * get_password_sta(void)
{
    return dev_config.wifi_password_sta;
}

void set_password_sta(char* password)
{
    strcpy(dev_config.wifi_password_sta, password);
}

//----------------------------------------
// Armazena “freq” ou “time” em dev_config.send_mode
void set_send_mode(const char *mode) {
    // copia até 5 caracteres + \0
    strncpy(dev_config.send_mode, mode, sizeof(dev_config.send_mode) - 1);
    dev_config.send_mode[sizeof(dev_config.send_mode) - 1] = '\0';
}

// Devolve o ponteiro para a string “freq” ou “time”
const char *get_send_mode(void) {
    return dev_config.send_mode;
}

// Auxiliares para decisão rápida
bool is_send_mode_freq(void) {
    return strcmp(dev_config.send_mode, "freq") == 0;
}

bool is_send_mode_time(void) {
    return strcmp(dev_config.send_mode, "time") == 0;
}

void set_send_time(int index, int hour) {

    if (index >= 1 && index <= 4) {
       dev_config.send_times[index - 1] = (uint8_t)hour;
    }
}

int get_send_time1(void)
{ return dev_config.send_times[0]; }
int get_send_time2(void)
{ return dev_config.send_times[1]; }
int get_send_time3(void)
{ return dev_config.send_times[2]; }
int get_send_time4(void)
{ return dev_config.send_times[3]; }

//----------------------------------------

uint32_t get_send_period(void)
{
    return dev_config.send_period;
}

void set_send_period(uint32_t send_period)
{
    dev_config.send_period= send_period;
}

uint32_t get_deep_sleep_period(void)
{
    return dev_config.deep_sleep_period;
}

void set_deep_sleep_period(uint32_t deep_sleep_period)
{
    dev_config.deep_sleep_period= deep_sleep_period;
}

char * get_date(void)
{
	xSemaphoreTake(myMutex, portMAX_DELAY);
    time_t now;
    struct tm timeinfo; 
    time(&now);
    setenv("TZ", "GMT+3", 1);
    tzset();

    localtime_r(&now, &timeinfo);

    strftime(dev_config.date, sizeof(dev_config.date), "%d/%m/%Y", &timeinfo);
 xSemaphoreGive(myMutex);
	return dev_config.date;
}

void set_date(char* date)
{
	xSemaphoreTake(myMutex, portMAX_DELAY);
    strcpy(dev_config.date, date);
    ESP_LOGI(TAG, "Configurando data: %s", date);
    xSemaphoreGive(myMutex);
}

char * get_time(void)
{ 
	xSemaphoreTake(myMutex, portMAX_DELAY);
    time_t now;
    struct tm timeinfo; 
    time(&now);
    setenv("TZ", "GMT+3", 1);
    tzset();

    localtime_r(&now, &timeinfo);

    strftime(dev_config.time, sizeof(dev_config.time), "%T", &timeinfo);
    xSemaphoreGive(myMutex);
	return dev_config.time;
}

void set_time(char* time)
{
	xSemaphoreTake(myMutex, portMAX_DELAY);
    strcpy(dev_config.time, time);
    ESP_LOGI(TAG, "Configurando hora: %s", time);
    xSemaphoreGive(myMutex);
}

//----------------------

int get_hour_minutes_seconds_in_seconds(void)
{
	 xSemaphoreTake(myMutex, portMAX_DELAY);
    time_t now;
    struct tm *now_tm;
    setenv("TZ", "GMT+3", 1);  // Define o fuso horário como GMT+3
    tzset();
    now = time(NULL);
    now_tm = localtime(&now);
     xSemaphoreGive(myMutex);
    // Converte horas para segundos (tm_hour * 3600) + minutos para segundos (tm_min * 60) + segundos
    return (now_tm->tm_hour * 3600) + (now_tm->tm_min * 60) + now_tm->tm_sec;
}

int get_time_hour(void) {
    time_t now;
    struct tm *now_tm;
    setenv("TZ", "GMT+3", 1); // Corrigido
    tzset();
    now = time(NULL);
    now_tm = localtime(&now);
    return now_tm->tm_hour;
}

int get_time_minute(void) {
    time_t now;
    struct tm *now_tm;
    setenv("TZ", "GMT+3", 1); // Corrigido
    tzset();
    now = time(NULL);
    now_tm = localtime(&now);
    return now_tm->tm_min;
}

int get_time_second(void) {
    time_t now;
    struct tm *now_tm;
    setenv("TZ", "GMT+3", 1); // Corrigido
    tzset();
    now = time(NULL);
    now_tm = localtime(&now);
    return now_tm->tm_sec;
}
//----------------------------
uint32_t get_scale(void)
{
    return dev_config.scale;
}

void set_scale(uint32_t volume)
{
    dev_config.scale = volume;
}

float get_flow_rate(void)
{
    return dev_config.flow_rate;
}

void set_flow_rate(float vazao)
{
    dev_config.flow_rate = vazao;
}

bool has_factory_config(void)
{
    return dev_config.finished_factory;
}

void set_factory_config(bool factory_config)
{
    dev_config.finished_factory = factory_config;
}

bool has_always_on(void)
{
    return dev_config.always_on;
}

void set_always_on(bool always_on)
{
    dev_config.always_on = always_on;
}

bool has_device_active(void)
{
    return dev_config.device_active;
}

void set_device_active(bool turn_on_off)
{
    dev_config.device_active = turn_on_off;
}

bool should_save_pulse_zero(void)
{
    return dev_config.save_pulse_zero;
}

void save_pulse_zero(bool save_pulse_zero)
{
    dev_config.save_pulse_zero = save_pulse_zero;
}
//+++++++++++++++++++++++++++++++++++++
void set_last_pulse_count(uint32_t counter)
{
	pulse.last_pulse_count = counter;
	save_record_pulse_config(&pulse);
}
uint32_t get_last_pulse_count(void)
{
    get_record_pulse_config(&pulse);
    return pulse.last_pulse_count;
}

void set_current_pulse_count(uint32_t counter)
{
	pulse.current_pulse_count = counter;
}
uint32_t get_current_pulse_count(void)
{
 //   get_record_pulse_config(&pulse);
    return pulse.current_pulse_count;
}

//+++++++++++++++++++++++++++++++++++++
/*bool should_send_value(void)
{
    return dev_config.send_value;
}

void send_value(bool send_value)
{
    dev_config.send_value = send_value;
}*/

void get_keep_alive_time(struct tm* timeinfo)
{
    time_t now;
    time(&now);
    setenv("TZ", "GMT+3", 1);
    tzset();

    localtime_r(&now, timeinfo);

    timeinfo->tm_hour = (op_config.keep_alive[0] - '0')*10 + (op_config.keep_alive[1] - '0');
    timeinfo->tm_min = (op_config.keep_alive[3] - '0')*10 + (op_config.keep_alive[4] - '0');
    timeinfo->tm_sec = (op_config.keep_alive[6] - '0')*10 + (op_config.keep_alive[7] - '0');

}

char * get_serial_number(void)
{
    return op_config.serial_number;
}

void set_serial_number(char* serial_number)
{
    strcpy(op_config.serial_number, serial_number);
}

char * get_company(void)
{
    return op_config.company;
}

void set_company(char* company)
{
    strcpy(op_config.company, company);
}

char * get_deep_sleep_start(void)
{
    return op_config.deep_sleep_start;
}

void set_deep_sleep_start(char* deep_sleep_start)
{
    strcpy(op_config.deep_sleep_start, deep_sleep_start);
}
char * get_deep_sleep_end(void)
{
    return op_config.deep_sleep_end;
}

void set_deep_sleep_end(char* deep_sleep_end)
{
    strcpy(op_config.deep_sleep_end, deep_sleep_end);
}
bool has_reset_count(void)
{
    return op_config.reset_count;
}

void enable_reset_count(bool enable)
{
    op_config.reset_count = enable;
}
char * get_keep_alive(void)
{
    return op_config.keep_alive;
}

void set_keep_alive(char* keep_alive)
{
    strcpy(op_config.keep_alive, keep_alive);
}
bool has_log_level_1(void)
{
    return op_config.log_level_1;
}

void enable_log_level_1(bool enable)
{
    op_config.log_level_1 = enable;
}
bool has_log_level_2(void)
{
    return op_config.log_level_2;
}

void enable_log_level_2(bool enable)
{
    op_config.log_level_2 = enable;
}
//--------------------------------

bool has_calibration(void)
{
    return maint_config.calibration;
}

void enable_calibration(bool enable)
{
    maint_config.calibration = enable;
}

char * get_analog_data1(void)
{
    return maint_config.analog_data1;
}

void set_analog_data1(char* analog_data)
{
    strcpy(maint_config.analog_data1, analog_data);
}

char * get_analog_data2(void)
{
    return maint_config.analog_data2;

}

void set_analog_data2(char* analog_data)
{
    strcpy(maint_config.analog_data2, analog_data);
}

char * get_pressure_data1(void)
{
	return pressure_data.pressure1;

}

void set_pressure_data1(char* pressure_data_value)
{
    strcpy(pressure_data.pressure1, pressure_data_value);
}

char * get_pressure_data2(void)
{
	return pressure_data.pressure2;

}

void set_pressure_data2(char* pressure_data_value)
{
    strcpy(pressure_data.pressure2, pressure_data_value);
}
//-----------------------
bool has_enable_post(void)
{
    return op_config.enable_post;
}

void enable_post(bool enable)
{
    op_config.enable_post = enable;
}
bool has_enable_get(void)
{
    return op_config.enable_get;
}

void enable_get(bool enable)
{
    op_config.enable_get = enable;
}

char * get_config_server_url(void)
{
    return op_config.config_server_url;
}

void set_config_server_url(char* config_server_url)
{
    strcpy(op_config.config_server_url, config_server_url);
}
uint32_t get_config_server_port(void)
{
    return op_config.config_server_port;
}

void set_config_server_port(uint32_t config_server_port)
{
    op_config.config_server_port = config_server_port;
}
char * get_config_server_path(void)
{
    return op_config.config_server_path;
}

void set_config_server_path(char* config_server_path)
{
    strcpy(op_config.config_server_path, config_server_path);
}

uint32_t get_level_min(void)
{
    return op_config.level_min;
}

void set_level_min(uint32_t level_min)
{
    op_config.level_min = level_min;
}

uint32_t get_level_max(void)
{
    return op_config.level_max;
}

void set_level_max(uint32_t level_max)
{
    op_config.level_max = level_max;
}

//**************************
uint16_t get_connection_gsm_network_error_count(void)
{
    return system_config.connection_gsm_network_error_count;
}

void set_connection_gsm_network_error_count(uint16_t count)
{
    system_config.connection_gsm_network_error_count = count;
}

uint16_t get_connection_server_error_count(void)
{
    return system_config.connection_server_error_count;
}

void set_connection_server_error_count(uint16_t count)
{
    system_config.connection_server_error_count = count;
}

uint32_t get_last_minute(void)
{
    return record_last_unit_time.last_minute;
}

void set_last_minute(uint32_t count)
{
    record_last_unit_time.last_minute = count;
}

uint32_t get_csq(void)
{
    return self_monitoring_data.csq;
}

void set_csq(uint32_t csq)
{
    self_monitoring_data.csq=csq;
}

uint32_t get_power_source(void)
{
    return self_monitoring_data.power_source;
}

float get_power_source_volts(void)
{
    uint32_t mv = get_power_source(); // em mV
    return ((float)mv) / 1000.0f;
}

void set_power_source(uint32_t power_source)
{
    self_monitoring_data.power_source=power_source;
}

uint32_t get_battery(void)
{
    return self_monitoring_data.battery;
}

void set_battery(uint32_t battery)
{
    self_monitoring_data.battery=battery;
}

//**************************

time_t get_last_data_sent(void)
{
    return system_config.last_data_sent;
}

void set_last_data_sent(time_t date)
{
    system_config.last_data_sent = date;
}

time_t get_last_sys_time(void)
{
    return system_config.last_sys_time;
}

void set_last_sys_time(time_t date)
{
    system_config.last_sys_time = date;
}

bool is_modem_enabled(void)
{
    return system_config.modem_enabled;
}

void enable_modem(bool en)
{
    system_config.modem_enabled = en;
}

bool check_enable_modem(void)
{
    time_t now, dp_start, dp_end;
    struct tm deep_sleep_start, deep_sleep_end; 
    bool ret = false;

    time(&now);
    setenv("TZ", "GMT+3", 1);
    tzset();

    localtime_r(&now, &deep_sleep_start);
    localtime_r(&now, &deep_sleep_end);

    deep_sleep_start.tm_hour = (op_config.deep_sleep_start[0] - '0')*10 + (op_config.deep_sleep_start[1] - '0');
    deep_sleep_start.tm_min = (op_config.deep_sleep_start[3] - '0')*10 + (op_config.deep_sleep_start[4] - '0');
    deep_sleep_start.tm_sec = (op_config.deep_sleep_start[6] - '0')*10 + (op_config.deep_sleep_start[7] - '0');

    deep_sleep_end.tm_hour = (op_config.deep_sleep_end[0] - '0')*10 + (op_config.deep_sleep_end[1] - '0');
    deep_sleep_end.tm_min = (op_config.deep_sleep_end[3] - '0')*10 + (op_config.deep_sleep_end[4] - '0');
    deep_sleep_end.tm_sec = (op_config.deep_sleep_end[6] - '0')*10 + (op_config.deep_sleep_end[7] - '0');

    dp_start = mktime(&deep_sleep_start);
    dp_end = mktime(&deep_sleep_end);

    if((dp_start > now) && (dp_end < now) )
    {
        ret = true;
    }

    return ret;
}

bool is_led_enabled(void)
{
    return system_config.led_enabled;
}

void enable_led(bool en)
{
    system_config.led_enabled = en;
}

bool check_enable_led(void)
{
    time_t now, start, end;
    struct tm led_start, led_end; 
    bool ret = false;

    time(&now);
    setenv("TZ", "GMT+3", 1);
    tzset();

    localtime_r(&now, &led_start);
    localtime_r(&now, &led_end);

/*    led_start.tm_hour = (op_config.led_start[0] - '0')*10 + (op_config.led_start[1] - '0');
    led_start.tm_min = (op_config.led_start[3] - '0')*10 + (op_config.led_start[4] - '0');
    led_start.tm_sec = (op_config.led_start[6] - '0')*10 + (op_config.led_start[7] - '0');

    led_end.tm_hour = (op_config.led_end[0] - '0')*10 + (op_config.led_end[1] - '0');
    led_end.tm_min = (op_config.led_end[3] - '0')*10 + (op_config.led_end[4] - '0');
    led_end.tm_sec = (op_config.led_end[6] - '0')*10 + (op_config.led_end[7] - '0');*/

    start = mktime(&led_start);
    end = mktime(&led_end);

    if((start < now) && (end > now) )
    {
        ret = true;
    }

    return ret;
}

// ===== WEG payload mode =====
// 0 = energia ; 1 = água
uint8_t get_weg_payload_mode(void)
{
    return g_weg_payload_mode;
}

void set_weg_payload_mode(uint8_t mode)
{
    g_weg_payload_mode = (mode ? 1 : 0);
    // se você já tem esquema de persistência em NVS para outros campos,
    // salve aqui (ex.: nvs_set_u8(...,"weg_mode", g_weg_payload_mode))
}


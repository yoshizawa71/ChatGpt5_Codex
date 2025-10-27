
#include <datalogger_control.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "ads1015_reader.h"
#include "pressure_meter.h"
#include "sdmmc_driver.h"
#include "oled_display.h"
#include "pressure_calibrate.h"

const static char *TAG = "PRESSURE_METER";
#define TASK_STACK_SIZE    (10000)
xTaskHandle Pressure_TaskHandle;
//static SemaphoreHandle_t Mutex_pressure_meter=NULL;

static uint32_t current_total_counter = 0;

static void deinit_pressure_task (void);

//------------------------------------------------------------------------------

const float Pmax = 20, Vmax=4.5, Fmca = 10.1974;

float Pbar,Pmca;
bool finish_display = false;

extern bool key_press_task_on;


esp_err_t save_pressure_measurement(int channel) // função para salvar na tabela
{

int error;

    struct pressure_data press = {0};
    get_pressure_data(&press);

   if (channel == 0)
      {
		ESP_LOGI(TAG,">>>>>Pressure 1 = %s<<<<\n", press.pressure1);
             if (save_record_sd(channel,press.pressure1)==0)
                {
				 error=ESP_OK; 
			    } 
	            else {
                    error = ESP_FAIL;  
                     }
               ESP_LOGI(TAG,">>>>>Save Record SD = %s\n",error==0 ? "ESP OK" : "ESP Fail");    
       }
   else {
         ESP_LOGI(TAG,">>>>>Pressure 1 = %s<<<<\n", press.pressure2);
             if (save_record_sd(channel,press.pressure2)==0)
                {
				 error=ESP_OK; 
			    } 
	            else {
                    error = ESP_FAIL;  
                     }
               ESP_LOGI(TAG,">>>>>Save Record SD = %s\n",error==0 ? "ESP OK" : "ESP Fail");    
       }

   ESP_LOGI(TAG,">>>>>Save Measurement error = %s\n",error==0 ? "ESP OK" : "ESP Fail");
   return error;
}

//-------------------------------------------------------------------
//            Pressure Sensor
//-------------------------------------------------------------------
void pressure_sensor_read(bool *sensor_1, bool *sensor_2)
{
	struct pressure_data saved_data;

	bool sensor_ok_1 = false;
	bool sensor_ok_2 = false;
	
//	saved_data = get_saved_pressure_data();
	
float p_1 = get_calibrated_pressure(analog_1, PRESSURE_UNIT_MCA, &sensor_ok_1);	   
if (sensor_ok_1) {
    int channel = 0;
    *sensor_1 = true;

    p_1 = roundf(p_1 * 10) / 10.0f; // arredonda para 1 casa decimal
    snprintf(saved_data.pressure1, sizeof(saved_data.pressure1), "%0.3f", p_1);
    save_pressure_data(&saved_data);

    if (has_calibration()) {
        scroll_down_display(saved_data, channel);
    }
} else {
    ESP_LOGI(TAG,"Sensor de pressão canal 0 não está instalado ou leitura inválida\n");
}
	   
float p_2 = get_calibrated_pressure(analog_2, PRESSURE_UNIT_MCA, &sensor_ok_2);
     	        
	if (sensor_ok_2) {
    int channel = 2;
    *sensor_2 = true;

    p_2 = roundf(p_2 * 10) / 10.0f;
    snprintf(saved_data.pressure2, sizeof(saved_data.pressure2), "%0.3f", p_2);
    save_pressure_data(&saved_data);

    if (has_calibration()) {
        scroll_down_display(saved_data, channel);
    }
} else {
    ESP_LOGI(TAG,"Sensor de pressão canal 2 não está instalado ou leitura inválida\n");
     }
   
}





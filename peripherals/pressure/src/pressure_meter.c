
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


/*void init_pressure_meter_config(void)
{
//	Mutex_pressure_meter = xSemaphoreCreateMutex();
	
//	struct pressure_dataset rec_pressure_config = {0};
//    struct pressure_index_control index_config= {0};

 //   get_pressure_index_control(&index_config);  

    if(!has_record_pressure_index_config())
    {
        save_default_pressure_index_control();
    }
 //   get_pressure_index_control(&index_config);
 
    current_total_counter = index_config.total_counter;
}
*/
esp_err_t save_pressure_measurement(int channel) // função para salvar na tabela
{
//xSemaphoreTake(Mutex_pressure_meter,portMAX_DELAY);

int error;

    struct pressure_data press = {0};
    get_pressure_data(&press);

   if (channel == 0)
      {
		printf(">>>>>Pressure 1 = %s<<<<\n", press.pressure1);
             if (save_record_sd(channel,press.pressure1)==0)
                {
				 error=ESP_OK; 
			    } 
	            else {
                    error = ESP_FAIL;  
                     }
               printf(">>>>>Save Record SD = %s\n",error==0 ? "ESP OK" : "ESP Fail");    
       }
   else {
         printf(">>>>>Pressure 1 = %s<<<<\n", press.pressure2);
             if (save_record_sd(channel,press.pressure2)==0)
                {
				 error=ESP_OK; 
			    } 
	            else {
                    error = ESP_FAIL;  
                     }
               printf(">>>>>Save Record SD = %s\n",error==0 ? "ESP OK" : "ESP Fail");    
       }

//   xSemaphoreGive(Mutex_pressure_meter);
   printf(">>>>>Save Measurement error = %s\n",error==0 ? "ESP OK" : "ESP Fail");
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
    printf("Sensor de pressão canal 0 não está instalado ou leitura inválida\n");
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
    printf("Sensor de pressão canal 2 não está instalado ou leitura inválida\n");
}
   
}

/*static void pressure_task(void *arg)
{
bool just_once = false;
uint8_t status = 0;
static bool passed = false;

bool pressure_sensor_1 = false;
bool pressure_sensor_2 = false;

	 while(1)
	 {
	  vTaskDelay(pdMS_TO_TICKS(1000));
      printf(">>>>>>>>>>>Calibração ligado\n");
		    switch(status)
            {
             case 0: if(has_calibration())
		                {
	                     pressure_sensor_read(&pressure_sensor_1, &pressure_sensor_2);
		                 }
		             else{
							status =1;
						  }
             break;

             case 1 : clear_display();
            	      status =2;
            	      key_press_task_on=true;
  
              break;

             case 2 : deinit_pressure_task ();
             
              break;

              default : printf("Erro pressure task\n");
              break;
             }
        }
		 	 
 }
*/

//-------------------------------------------------------------------
//
/*void init_pressure_task(void)
{
 //A uart read/write example without event queue;

	    xTaskCreate(pressure_task, "Pressure_task",TASK_STACK_SIZE, NULL, 2, &Pressure_TaskHandle);

}


static void deinit_pressure_task (void)
{
	if( Pressure_TaskHandle != NULL )
  {
     vTaskDelete( Pressure_TaskHandle );
  }
}*/




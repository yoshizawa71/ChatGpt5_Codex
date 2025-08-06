/*
 * energy_meter.c
 *
 *  Created on: 20 de nov. de 2024
 *      Author: geopo
 */

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

#include "datalogger_driver.h"
#include "ads1015_reader.h"
#include "sdmmc_driver.h"
#include "oled_display.h"
#include "energy_meter.h"

const static char *TAG = "Energy_Meter";
#define TASK_STACK_SIZE    (10000)
xTaskHandle energy_TaskHandle;
static SemaphoreHandle_t Mutex_energy_meter=NULL;


void init_energy_meter_config(void)
{
	Mutex_energy_meter = xSemaphoreCreateMutex();
	
//	struct energy_measured rec_energy_config = {0};
    struct energy_index_control index_config= {0};

 //   get_energy_index_control(&index_config);
  

    if(!has_record_energy_index_config())
    {
        save_default_energy_index_control();
    }
    get_energy_index_control(&index_config);
 
    current_total_counter = index_config.total_counter;
}

void save_default_energy_index_control(void)
{
	xSemaphoreTake(Mutex_energy_meter,portMAX_DELAY);
	
    struct energy_index_control index_config= {0};

    index_config.last_write_energy_idx = UNSPECIFIC_RECORD;
    index_config.last_read_energy_idx = UNSPECIFIC_RECORD;
    index_config.total_counter = 0;

    save_energy_index_control(&index_config);
    xSemaphoreGive(Mutex_energy_meter);
}

bool has_energy_data_to_send(void)
{
    bool ret = false;
 
   struct energy_index_control index_config= {0};
 
    get_energy_index_control(&index_config);

    if(index_config.total_counter > 0)
    {
        if(index_config.last_write_energy_idx == UNSPECIFIC_RECORD)
        {
            if (index_config.last_read_energy_idx != (index_config.total_counter - 1) ){
                ret = true;
            }
        }
        else{
            if (index_config.last_write_energy_idx != index_config.last_read_energy_idx) {
                ret = true;
            }
        }
    }

    return ret;
}

/*static void save_energy_data(int channel)
{
    uint32_t write_idx = UNSPECIFIC_RECORD;
    struct record_energy record = {0};
    struct energy_index_control index_config= {0};
    struct energy_measured pwr = {0};
    get_energy_measured(&pwr);
//    int channel =0; //implementar o envio do canal
	
    // Verificar se o arquivo excel foi deletado
       	 if (has_SD_FILE_Deleted_energy())
       		 {
       		  save_default_energy_index_control();
       		  printf("+++ Arquivo excel havia sido deletado +++\n");
       		  }
   if (channel == 0)
      {
		  printf("======>energy 1 =%s\n", pwr.energy1);  
		      strcpy(record.date, get_date());
              strcpy(record.time, get_time());
              strncpy(record.power_1, pwr.energy1,6);
//		  printf("}}}}}}>energy1  =%s\n", record.energy_1);
		   ESP_LOGI(TAG, "Record %s %s %d %s\n", record.date, record.time, channel, record.power_1);
	  }
	  else {
            printf("======>energy 2 =%s\n", pwr.energy2);
            strcpy(record.date, get_date());
            strcpy(record.time, get_time());
            strncpy(record.power_2, pwr.energy2,6);
 //           printf("}}}}}}>energy2  =%s\n", record.energy_2);
            ESP_LOGI(TAG, "Record %s %s %d %s\n", record.date, record.time, channel, record.power_2); 
           }
           
    get_energy_index_control(&index_config);


    if(index_config.last_write_energy_idx != write_idx)
    {
        write_idx = (index_config.last_write_energy_idx + 1)%index_config.total_counter;
    }

    save_record_energy(&write_idx, record, channel);

    index_config.last_write_energy_idx = write_idx;
    if(write_idx == UNSPECIFIC_RECORD)
    {
        index_config.total_counter = index_config.total_counter + 1;
        current_total_counter = index_config.total_counter;
    }
    
save_energy_index_control(&index_config);

}*/

/*float get_energy_sensor_read(enum sensor leitor, float *Vmin, float *Fcorr, bool no_energy, bool correction_cali, float P_ref,bool *dev_energy)
{
static float voltage;


if ((has_calibration())&&(no_energy))
   {
   *Vmin = (oneshot_analog_read(leitor));    
    printf(">>>>>>ZERADO <<<<<<< \n");
    }
    
printf(">>>(1)>>>Vmin = %.03f***** \n",*Vmin);

    voltage = (oneshot_analog_read(leitor));
    if (voltage>0)
    {
     *dev_energy=true;
	}
 //   printf(">>>(1)>>>Voltage -> %f e  Pref = %f \n", voltage, P_ref);
 printf(">>>(2)>>>Voltage -> %f\n", voltage);
 float Diff_Volt_Vmin = voltage-*Vmin; //Não pode ser negativo
 if (Diff_Volt_Vmin<0)
 {Diff_Volt_Vmin =0;}
 
 printf(">>>(3)>>>Diff_Volt_Vmin -> %f\n", Diff_Volt_Vmin);

         Pbar = ((Pmax*(Diff_Volt_Vmin))/(Vmax-*Vmin));
 printf(">>>(4)>>> Pbar puro -> %f\n", Pbar);
//         printf("))))) pwrão BAR = %f \n", Pbar);

         Pmca = Pbar * Fmca;
printf(">>>(5)>>> Pmca puro -> %f\n", Pmca);

 //       if(Pmca>0.01 && P_ref>0 && *fcorr_cali&& !*zerado && P_ref>1)//divisão por zero
printf(">>>(6)>>> Correction Calibration Enabled -> %d\n", correction_cali);
         if(has_calibration()&&Pmca>0.01 && correction_cali&& !no_energy && P_ref>1)
         {
           *Fcorr= (P_ref/Pmca);
 //          *fcorr_cali = false;
           printf(">>>(7)>>>Fator de correção calculado na condição = %f \n", *Fcorr);
           printf(">>>(8)>>>pwrao de referencia = %f \n", P_ref);
         }

         Pmca=Pmca*(*Fcorr);
printf(">>>(9)>>> Pmca multiplicado pelo fator -> %f\n\n", Pmca);
         
return Pmca;

}*/

//-------------------------------------------------------------------
//            energy Sensor
//-------------------------------------------------------------------

/*void energy_sensor_read(void)
{
//	i2c_master_init(); //Inicialisa o I2C
	struct energy_measured saved_dataset;
	struct record_energy saved_time;
	
	bool zerar = false;
	bool correction_enabled = false;
	char * pEnd1;
	char * pEnd2;
	bool dev_energy_1= false;
    bool dev_energy_2= false;
    
    float Vref_pwr1 = strtof(get_reference_cali1(), &pEnd1);//Converte char em float
//    printf("###########REFERENCE 1 = %.3f\n", Vref_pwr1);
    float Vref_pwr2 = strtof(get_reference_cali2(), &pEnd2);//Converte char em float
//    printf("###########REFERENCE 2 = %.3f\n", Vref_pwr2);
	saved_dataset = get_saved_energy_measured();
    printf("+++++++++++++++++Get energy 1 = %s\n", get_energy_data1());
    printf("+++++++++++++++++Get Analog 1 =  %s\n", get_analog_data1());
	if(has_calibration())
	  {
	   zerar = has_level_zero();
	   correction_enabled = has_correction();
	   set_analog_data1(get_energy_data1());
	   set_analog_data2(get_energy_data2());
	  }

	printf("energy_measured.vmin1= %f\n", saved_dataset.vmin1);
	printf("energy_measured.vmin2= %f\n", saved_dataset.vmin2);
	printf("energy_measured.fcorr1= %f\n", saved_dataset.fcorr1);
	printf("energy_measured.fcorr2= %f\n", saved_dataset.fcorr2);

	printf("energy_measured.energy1= %s\n", saved_dataset.energy1);
	printf("energy_measured.energy2= %s\n", saved_dataset.energy2);
	printf("energy_measured.last_energy1= %s\n", saved_dataset.last_energy1);
	printf("energy_measured.last_energy2= %s\n", saved_dataset.last_energy2);
	printf("energy_measured.no_energy1= %s\n", saved_dataset.no_energy1 ? "true" : "false");
	printf("energy_measured.no_energy2= %s\n", saved_dataset.no_energy2 ? "true" : "false");
//	printf("energy_measured.cali_fcorr1= %s\n", saved_dataset.cali_fcorr1 ? "true" : "false");

//	float p_1 = get_energy_sensor_read(pwrao_1, &saved_dataset.vmin1, &saved_dataset.fcorr1, &saved_dataset.no_energy1, &saved_dataset.cali_fcorr1,Vref_pwr1);
	float p_1 = get_energy_sensor_read(analog_1, &saved_dataset.vmin1, &saved_dataset.fcorr1, zerar, correction_enabled,Vref_pwr1,&dev_energy_1);
//	float p_2 = get_energy_sensor_read(pwrao_2, &saved_dataset.vmin2, &saved_dataset.fcorr2, &saved_dataset.no_energy2, Vref_pwr2);
    float p_2 = get_energy_sensor_read(analog_2, &saved_dataset.vmin2, &saved_dataset.fcorr2, zerar, correction_enabled,Vref_pwr2,&dev_energy_2);
   
//	i2c_driver_delete(I2C_MASTER_PORT_0);

  if (dev_energy_1)
     {
	   int channel=0;
	   p_1=roundf(p_1*10)/10; //arredondamento com 3 casas depois da vÃ­gula
//	   printf("pwrÃ£o 1<<<<<<< = %.3f vmim = %f Zerado = %s \n", p_1, saved_dataset.vmin1, saved_dataset.no_energy1 ? "true" : "false");
//	   printf("<<<<<<Data energy 1 = %f\n",p_1 );
	   snprintf(saved_dataset.energy1, 8, "%0.3f",p_1);
//	   printf(">>>>>>Data energy 1 = %s\n",saved_dataset.energy1 );
	   save_energy_measurement(saved_dataset);
       save_energy_data(channel);
       if(has_calibration())
         {
			 scroll_down_display(saved_dataset, channel);

		 }
       }
       else{
		    printf(" O sensor de pwrão canal 0 não está instalado\n");
		   }
       
       if(dev_energy_2)
         {
		  int channel=2;
		  p_2=roundf(p_2*10)/10; //arredondamento com 3 casas depois da vÃ­gula
//	      printf("pwrÃ£o 2<<<<<<< = %.3f vmim = %f Zerado = %d \n", p_2, saved_dataset.vmin2, saved_dataset.no_energy2);
//          printf("<<<<<<Data energy 2 = %f\n",p_2 );
	      snprintf(saved_dataset.energy2, 8, "%0.3f",p_2);
//          printf(">>>>>>Data energy 2 = %s\n",saved_dataset.energy2 );
          save_energy_measurement(saved_dataset);
          save_energy_data(channel);
          
          if(has_calibration())
             {
			 
			  scroll_down_display(saved_dataset, channel);
			 
		     }
	     }
	   else
	   {
		  printf(" O sensor de pwrão canal 2 não está instalado\n");
	   }

}

static void energy_task(void *arg)
{
bool just_once = false;
uint8_t status = 0;
static bool passed = false;

	
 }*/


//-------------------------------------------------------------------
//
/*void init_energy_task(void)
{


	    xTaskCreatePinnedToCore(energy_task, "energy_task",TASK_STACK_SIZE, NULL, 2, &energy_TaskHandle,1);

}


static void deinit_energy_task (void)
{
	if( energy_TaskHandle != NULL )
  {
     vTaskDelete( energy_TaskHandle );
  }
}*/

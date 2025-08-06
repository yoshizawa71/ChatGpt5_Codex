/*
 * screen_view.c
 *
 *  Created on: 16 de out. de 2024
 *      Author: geopo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "ssd1306.h"
#include "font8x8_basic.h"
//#include "oled_display.h"
#include "pressure_meter.h"
#include "datalogger_control.h"
#include "oled_display.h"

const static char *TAG = "Screen View";
SSD1306_t dev;

#define STACK_SIZE 1700
TaskHandle_t xHandle_Display = NULL;
#define tskIDLE_PRIORITY  1

#define ENABLE_DISPLAY       0

static void init_config_display(void)
{
#if ENABLE_DISPLAY
	//i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_RESET_GPIO);
	i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, -1);// -1
	#if CONFIG_FLIP
	dev._flip = true;
	ESP_LOGW(TAG, "Flip upside down");
#endif

#if CONFIG_SSD1306_128x64
	ESP_LOGI(TAG, "Panel is 128x64");
	ssd1306_init(&dev, 128, 64);
#endif // CONFIG_SSD1306_128x64
#if CONFIG_SSD1306_128x32
	ESP_LOGI(TAG, "Panel is 128x32");
	ssd1306_init(&dev, 128, 32);
#endif // CONFIG_SSD1306_128x32	
 ssd1306_clear_screen(&dev, false);
 
 #endif
}


void write_logo_LWS(void)
{
#if ENABLE_DISPLAY
init_config_display();

//	ssd1306_clear_screen(&dev, false);
	ssd1306_contrast(&dev, 0xff);
	ssd1306_display_text_x3(&dev, 1, " LWS", 4, false);

	ssd1306_display_text(&dev, 4, "   Solucoes ",13, false);
	vTaskDelay(3000 / portTICK_PERIOD_MS);
	ssd1306_clear_screen(&dev, false);
#endif
	return;
}


void scroll_down_display(struct pressure_data p, int channel)
{
char canal[2];
char lineChar[20];
static int dontfinish = 0;
char contador[4];
char pressure[8];

#if ENABLE_DISPLAY

   if(channel==0)
     {
	  strncpy(pressure, p.pressure1,8);
	  sprintf(canal,"%d",channel);
      }
   else {
          strncpy(pressure, p.pressure2,8);
          sprintf(canal,"%d",channel);
        }

//printf("]]]]] Finish ---->%d  hora ==== %s    Pressao ====> %s\n",finish, get_time(), p.pressure1);


if (dontfinish ==0)
  {
//i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_RESET_GPIO);
i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, -1);// -1 para reset, ajuste conforme necessário
init_config_display();

}
dontfinish=dontfinish+1;
  printf("}}}}}}}}}}}====>Dont finish = %d\n", dontfinish);
 sprintf(contador,"%d",dontfinish);
  ssd1306_contrast(&dev, 0xff);
//  ssd1306_display_text(&dev, 0, " Hora  canal MCA ", 16, true);
ssd1306_display_text(&dev, 0, "Num Canal  Valor ", 16, true);
  ssd1306_software_scroll(&dev, 1, (dev._pages - 1) );

//  sprintf(&lineChar[0], "%s %s %s ", get_time(),canal, pressure);
 sprintf(&lineChar[0], "%s    %s   %s ", contador, canal, pressure);
  ssd1306_scroll_text(&dev, lineChar, strlen(lineChar), false);
#endif
return;
}

void sensor_data_on_display(void)
{
	char timedateChar[20];
	char channel[10];
	char p[20];
	char pressao1[8];
	char pressao2[8];
	char strpulse[4];
	
	struct pressure_data value = {0};
	struct record_pulse_config pulse = {0};
#if ENABLE_DISPLAY
	get_pressure_data_set(&value);
	strncpy(pressao1, value.pressure1,8);
    strncpy(pressao2, value.pressure2,8);
    printf(">>>>Pressao 1 = %s<<<<\n", value.pressure1);
    printf(">>>>Pressao 2 = %s<<<<\n", value.pressure2);
    get_record_pulse_config(&pulse);

     sprintf(strpulse, "%lu", pulse.current_pulse_count);
		
init_config_display();

	ssd1306_clear_screen(&dev, false);
	ssd1306_contrast(&dev, 0xff);
	sprintf(&timedateChar[0], "%s %s ", get_date(), get_time());
    ssd1306_display_text(&dev, 1,timedateChar , 20, false);
    ssd1306_display_text(&dev, 3," CH0  CH1  CH2" , 18, false);
    sprintf(&p[0], "%s %s %s",pressao1,strpulse, pressao2);

    ssd1306_display_text(&dev, 5,p , 20, false);
#endif
    return;

}

void clear_display (void)
{
#if ENABLE_DISPLAY	
 ssd1306_clear_screen(&dev, false);	
 return;
 #endif
}


// Task to be created.
void vTask_Display( void * pvParameters )
{
	/* Inspect our own high water mark on entering the task. */
/*	UBaseType_t uxHighWaterMark;
    uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );*/
	
for( ;; )
  {
vTaskDelay(pdMS_TO_TICKS(1000));



/*uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
printf("uxHighWaterMark= %d\n",uxHighWaterMark);*/
  }
}


void init_Display_task (void)
{
	  if (xHandle_Display == NULL)
	   {
		xTaskCreate(vTask_Display, "Task_Display", STACK_SIZE, NULL, tskIDLE_PRIORITY, &xHandle_Display);
        }
	

}

void deinit_Display_task (void)
{
	if( xHandle_Display != NULL )
  {
     vTaskDelete( xHandle_Display );
  }
}



/*
 * oled_display.h
 *
 *  Created on: 8 de out. de 2024
 *      Author: geopo
 */

#ifndef PERIPHERALS_OLED_DISPLAY_INCLUDE_OLED_DISPLAY_H_
#define PERIPHERALS_OLED_DISPLAY_INCLUDE_OLED_DISPLAY_H_


void show_image(void);
void write_logo_LWS(void);
void scroll_down_display(struct pressure_data p, int channel);
void sensor_data_on_display(void);
void clear_display (void);

void vTask_Display( void * pvParameters );
void init_Display_task (void);
void deinit_Display_task (void);

#endif /* PERIPHERALS_OLED_DISPLAY_INCLUDE_OLED_DISPLAY_H_ */

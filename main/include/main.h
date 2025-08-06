/*
 * main.h
 *
 *  Created on: 18 de out. de 2024
 *      Author: geopo
 */
#include "stdbool.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef MAIN_INCLUDE_MAIN_H_
#define MAIN_INCLUDE_MAIN_H_



/*struct Send_task_Status
    {
        bool    send_factory_wifi_status;
        bool    send_cellnetconnect_status;
        bool    send_mqtt_status;
    };
//xQueueHandle xQueue_send_status;

struct Receive_task_status
    {
        bool    receive_factory_wifi_status;
        bool    receive_cellnetconnect_status;
        bool    receive_mqtt_status;
    };*/

//xQueueHandle xQueue_receive_status;


void init_timemanager_task(void);

void init_queue_notification(void);

int get_wakeup_cause(void);

void init_sensor_pwr_supply(void);



#endif /* MAIN_INCLUDE_MAIN_H_ */

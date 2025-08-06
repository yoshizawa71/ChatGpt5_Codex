#include "esp_timer.h"
#include "sdkconfig.h"
#include "datalogger_driver.h"

uint64_t get_timestamp_ms(void)
{
        return esp_timer_get_time()/1000;
}
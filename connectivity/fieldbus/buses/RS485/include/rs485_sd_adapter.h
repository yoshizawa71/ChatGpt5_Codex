#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rs485_sd_adapter_save_record(const char *key, float value);

#if defined(RS485_SD_ADAPTER_ENABLE_ALIAS)
static inline esp_err_t save_record_sd_rs485(const char *key, float value)
{
    return rs485_sd_adapter_save_record(key, value);
}
#endif

#ifdef __cplusplus
}
#endif

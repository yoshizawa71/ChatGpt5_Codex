#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rs485_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

bool rs485_registry_get_channel_addr(uint8_t channel, uint8_t *out_addr);
int rs485_registry_get_channel_phase_count(uint8_t channel);
int rs485_registry_iterate_configured(bool (*cb)(uint8_t, uint8_t, void *), void *user);
int rs485_registry_get_snapshot(rs485_sensor_t *out, size_t max);

#ifdef __cplusplus
}
#endif

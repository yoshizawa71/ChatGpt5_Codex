#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void rs485_central_poll_and_save(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

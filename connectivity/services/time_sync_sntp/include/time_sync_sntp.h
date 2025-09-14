/*
 * time_sync_sntp.h
 *
 *  Created on: 14 de set. de 2025
 *      Author: geopo
 */

#ifndef CONNECTIVITY_SERVICES_TIME_SYNC_SNTP_INCLUDE_TIME_SYNC_SNTP_H_
#define CONNECTIVITY_SERVICES_TIME_SYNC_SNTP_INCLUDE_TIME_SYNC_SNTP_H_

#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t time_sync_sntp_now(uint32_t timeout_ms);
/* servers: até 3 FQDNs; se num_servers==0 usa defaults */
esp_err_t time_sync_sntp_with_servers(const char *const *servers, int num_servers, uint32_t timeout_ms);
/* Ex.: "UTC-3", "UTC", "<-03>3" */
void time_sync_set_timezone(const char *tz_string);

#ifdef __cplusplus
}
#endif


#endif /* CONNECTIVITY_SERVICES_TIME_SYNC_SNTP_INCLUDE_TIME_SYNC_SNTP_H_ */

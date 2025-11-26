/*
 * lte_payload_builder.h
 *
 *  Created on: 22 de nov. de 2025
 *      Author: geopo
 */

#ifndef CONNECTIVITY_IP_ACCESS_NETWORK_4G_INCLUDE_LTE_PAYLOAD_BUILDER_H_
#define CONNECTIVITY_IP_ACCESS_NETWORK_4G_INCLUDE_LTE_PAYLOAD_BUILDER_H_

#include "esp_err.h"
#include "datalogger_driver.h"

esp_err_t lte_json_data_payload(char *buf,
                            size_t bufSize,
                            struct record_index_config rec_index,
                            uint32_t *counter_out,
                            uint32_t *cursor_position);



#endif /* CONNECTIVITY_IP_ACCESS_NETWORK_4G_INCLUDE_LTE_PAYLOAD_BUILDER_H_ */

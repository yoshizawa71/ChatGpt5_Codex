/*
 * modbus_slaves.h
 *
 *  Created on: 3 de set. de 2025
 *      Author: geopo
 */

#ifndef CONNECTIVITY_FIELDBUS_PROTOCOLS_MODBUS_INCLUDE_MODBUS_SLAVES_H_
#define CONNECTIVITY_FIELDBUS_PROTOCOLS_MODBUS_INCLUDE_MODBUS_SLAVES_H_

#include "modbus_rtu_master.h"

// Funções públicas
void modbus_slaves_init(void);
modbus_slave_t* modbus_slaves_get_list(void);
uint16_t modbus_slaves_get_count(void);


#endif /* CONNECTIVITY_FIELDBUS_PROTOCOLS_MODBUS_INCLUDE_MODBUS_SLAVES_H_ */

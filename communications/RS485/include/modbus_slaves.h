/*
 * modbus_slaves.h
 *
 *  Created on: 26 de mar. de 2025
 *      Author: geopo
 */

#ifndef MODBUS_SLAVES_H
#define MODBUS_SLAVES_H

#include "modbus_master.h"

// Funções públicas
void modbus_slaves_init(void);
modbus_slave_t* modbus_slaves_get_list(void);
uint16_t modbus_slaves_get_count(void);

#endif // MODBUS_SLAVES_H
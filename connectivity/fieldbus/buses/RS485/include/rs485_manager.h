/*
 * rs485_manager.h
 *
 *  Created on: 9 de ago. de 2025
 *      Author: geopo
 */


#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif
// “Gerência” mínima: apenas um ping “genérico”
// (lista/flash fica no seu módulo de config já existente)
esp_err_t rs485_manager_ping(uint8_t addr, TickType_t tmo);

#ifdef __cplusplus
}
#endif /* COMMUNICATIONS_RS485_INCLUDE_RS485_MANAGER_H_ */

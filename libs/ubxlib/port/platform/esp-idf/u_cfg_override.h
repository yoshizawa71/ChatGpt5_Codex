/*
 * u_cfg_override.h
 *
 *  Created on: 17 de jun. de 2025
 *      Author: geopo
 */

#ifndef LIBS_UBXLIB_PORT_PLATFORM_ESP_IDF_U_CFG_OVERRIDE_H_
#define LIBS_UBXLIB_PORT_PLATFORM_ESP_IDF_U_CFG_OVERRIDE_H_

// Aumenta a pilha da eventTask para 16 KB (você pode ir ainda mais alto, se quiser)

#define U_CFG_OS_TASK_STACK_SIZE_BYTES 16384

#undef U_AT_CLIENT_URC_TASK_STACK_SIZE_BYTES
#define U_AT_CLIENT_URC_TASK_STACK_SIZE_BYTES 8192

#endif /* LIBS_UBXLIB_PORT_PLATFORM_ESP_IDF_U_CFG_OVERRIDE_H_ */

/*
 * portal_state.h
 *
 *  Created on: 7 de out. de 2025
 *      Author: geopo
 */

#ifndef CONNECTIVITY_SERVICES_PORTAL_STATE_INCLUDE_PORTAL_STATE_H_
#define CONNECTIVITY_SERVICES_PORTAL_STATE_INCLUDE_PORTAL_STATE_H_

#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Seta/consulta o estado do portal (HTTP server/front) em execução.
void portal_state_set_active(bool on);
bool portal_state_is_active(void);

// Implementação "forte": usada como fonte da verdade para o gancho fraco.
bool factory_portal_active(void);

#ifdef __cplusplus
}
#endif




#endif /* CONNECTIVITY_SERVICES_PORTAL_STATE_INCLUDE_PORTAL_STATE_H_ */

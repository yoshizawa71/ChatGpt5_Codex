/*
 * ifdef_features.h
 *
 *  Created on: 18 de set. de 2025
 *      Author: geopo
 */

#ifndef DATALOGGER_DATALOGGER_CONTROL_INCLUDE_IFDEF_FEATURES_H_
#define DATALOGGER_DATALOGGER_CONTROL_INCLUDE_IFDEF_FEATURES_H_

// dl_features.h  —  sempre incluído ANTES de qualquer outro header
#pragma once
#include "sdkconfig.h"   // pega CONFIG_* do menuconfig, se existirem

// ======= Toggles do projeto (defaults seguros; podem ser sobrescritos no build) =======

// Modbus
#ifndef CONFIG_MODBUS_ENABLE
#define CONFIG_MODBUS_ENABLE 0
#endif
#ifndef CONFIG_MODBUS_GUARD_ENABLE
#define CONFIG_MODBUS_GUARD_ENABLE 0
#endif

// Trace de interação no factory
#ifndef FC_TRACE_INTERACTION
#define FC_TRACE_INTERACTION 0
#endif

// Política de sono (1=dorme; 0=debug nunca dorme)
#ifndef CONFIG_ALLOW_DEEP_SLEEP
#define CONFIG_ALLOW_DEEP_SLEEP 1
#endif

// HTTP no STA (subir httpd quando o STA ganha IP)
#ifndef CONFIG_REMOTE_HTTP_ON_STA
#define CONFIG_REMOTE_HTTP_ON_STA 1
#endif

// Proteção simples (Basic Auth) nos endpoints sensíveis
#ifndef CONFIG_REMOTE_BASIC_AUTH
#define CONFIG_REMOTE_BASIC_AUTH 1
#endif

// mDNS no STA (opcional; consome RAM)
#ifndef CONFIG_REMOTE_MDNS
#define CONFIG_REMOTE_MDNS 0
#endif

// ======= Sanity checks (compilação falha se incoerente) =======
#if CONFIG_MODBUS_ENABLE && !CONFIG_MODBUS_GUARD_ENABLE
# error "CONFIG_MODBUS_ENABLE=1 exige CONFIG_MODBUS_GUARD_ENABLE=1"
#endif

// ======= Helpers (macros práticos) =======
#if FC_TRACE_INTERACTION
  #define FC_TRACE()  factory_dump_last_interaction_origin()
#else
  #define FC_TRACE()  (void)0
#endif




#endif /* DATALOGGER_DATALOGGER_CONTROL_INCLUDE_IFDEF_FEATURES_H_ */

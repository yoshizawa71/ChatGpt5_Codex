/*
 * u_cell_registration.c
 *
 *  Created on: 3 de set. de 2024
 *      Author: geopo
 */


/*
 * Copyright 2019-2024 u-blox
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/** @brief This example demonstrates how to configure the settings
 * in a u-blox cellular module related to getting network service.
 *
 * The choice of module and the choice of platform on which this
 * code runs is made at build time, see the README.md for
 * instructions.
 */

#include "u_cfg_test_platform_specific.h"
#include "u_cell_test_cfg.h"

#define TIME_REFERENCE         3439756800 // Tempo definido para 0 horas de Janeiro de 2079

#define U_CFG_TEST_CELL_MODULE_TYPE U_CELL_MODULE_TYPE_SARA_R422

#ifdef U_CFG_TEST_CELL_MODULE_TYPE

// Bring in all of the ubxlib public header files
# include "ubxlib.h"

// Bring in the application settings
# include "u_cfg_app_platform_specific.h"

#include "driver/gpio.h"

#include "stdlib.h"    // rand()
#include "stddef.h"    // NULL, size_t etc.
#include "stdint.h"    // int32_t etc.
#include "stdbool.h"
#include "string.h"    // memset()

#include "u_cfg_sw.h"
#include "u_cfg_os_platform_specific.h"


#include "u_error_common.h"

#include "u_port.h"
#include "u_device.h"
#include "u_port_os.h"   // Required by u_cell_private.h
#include "u_port_heap.h"
#include "u_port_debug.h"
#include "u_port_uart.h"
#include "u_test_util_resource_check.h"
#include "u_timeout.h"
#include "u_at_client.h"
#include "u_cell_module_type.h"
#include "u_cell.h"
#include "u_cell_file.h"
#include "u_cell_net.h"     // Required by u_cell_private.h
#include "u_cell_private.h" // So that we can get at some innards
#include "u_cell_pwr.h"
#include "u_cell_cfg.h"
#include "u_cell_info.h"    // For uCellInfoTime()
#include "sara_r422.h"
#include "esp_log.h"

#ifdef U_CELL_TEST_MUX_ALWAYS
# include "u_cell_mux.h"
#endif

#include "u_cell_test_private.h"

#include "time.h"
#include <sys/time.h>
#include "datalogger_control.h"
#include "connectivity.h"    // cell_connect()


/** Tenta abrir o PDP context via UBX-API, fazendo fallback para GPRS caso NB-IoT falhe.
 *  Deve estar implementada em connectivity.c */
extern bool cell_connect_ubx(uDeviceHandle_t devHandle);


static void cellCfgTime (uDeviceHandle_t devHandle);

// TAG para logging
static const char TAG[] = "DevLteConfig";

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

# ifndef MY_MNO_PROFILE
// Replace U_CELL_TEST_CFG_MNO_PROFILE with the MNO profile number
// you require: consult the u-blox AT command manual for your module
// to find out the possible values; 100, for example, is "Europe",
// 90 is "global".
//#  define MY_MNO_PROFILE U_CELL_TEST_CFG_MNO_PROFILE
#define MY_MNO_PROFILE 90
# endif

// The RATs you want the module to use, in priority order.
// Set the value of MY_RAT0 to the RAT you want to use
// first (see the definition of uCellNetRat_t in cell/api/u_cell_net.h
// for the possibilities); for SARA-U201 you might chose
// U_CELL_NET_RAT_UTRAN or U_CELL_NET_RAT_GSM_GPRS_EGPRS, for
// SARA-R41x you might chose U_CELL_NET_RAT_CATM1, for
// for SARA-R412M you might chose U_CELL_NET_RAT_CATM1 or
// U_CELL_NET_RAT_GSM_GPRS_EGPRS and for SARA-R5 you might
// chose U_CELL_NET_RAT_CATM1.
// If your module supports more than one RAT at the same time
// (consult the data sheet for your module to find out how many
// it supports at the same time), add secondary and tertiary
// RATs by setting the values for MY_RAT1 and MY_RAT2 as
// required.
# ifndef MY_RAT0
#  define MY_RAT0 U_CELL_NET_RAT_NB1
# endif

# ifndef MY_RAT1
#  define MY_RAT1 U_CELL_NET_RAT_GSM_GPRS_EGPRS
# endif

# ifndef MY_RAT2
#  define MY_RAT2 U_CELL_NET_RAT_UNKNOWN_OR_NOT_USED
# endif

// Set the values of MY_xxx_BANDMASKx to your chosen band masks
// for the Cat M1 and NB1 RATs; see cell/api/u_cell_cfg.h for some
// examples.  This is definitely the ADVANCED class: not all
// modules support all bands and a module will reject a band mask
// if one bit in one bit-position is not supported.  If you make a
// band selection that does not include a band that the network
// broadcasts at your location you will never obtain coverage,
// so take care.
// When in doubt, set an MNO profile and rely on that to configure
// the bands that your modules _does_ support.
# ifndef MY_CATM1_BANDMASK1
#  define MY_CATM1_BANDMASK1 U_CELL_CFG_BAND_MASK_1_NORTH_AMERICA_CATM1_DEFAULT
# endif
# ifndef MY_CATM1_BANDMASK2
#  define MY_CATM1_BANDMASK2 U_CELL_CFG_BAND_MASK_2_NORTH_AMERICA_CATM1_DEFAULT
# endif
# ifndef MY_NB1_BANDMASK1
#  define MY_NB1_BANDMASK1   U_CELL_CFG_BAND_MASK_1_BRAZIL_NB1_DEFAULT
# endif
# ifndef MY_NB1_BANDMASK2
#  define MY_NB1_BANDMASK2   U_CELL_CFG_BAND_MASK_2_BRAZIL_NB1_DEFAULT
# endif

#define U_CELL_PRIVATE_DEFAULTS {-1, NULL, NULL}


#ifndef U_CELL_CFG_TEST_TIME_OFFSET_SECONDS
/** How far ahead to adjust the time when testing.
 */
# define U_CELL_CFG_TEST_TIME_OFFSET_SECONDS 75
#endif

#ifndef U_CELL_CFG_TEST_TIME_MARGIN_SECONDS
/** The permitted margin between reading time several times during
 * testing, in seconds.
 */
# define U_CELL_CFG_TEST_TIME_MARGIN_SECONDS 10
#endif

#ifndef U_CELL_CFG_TEST_FIXED_TIME
/** A time value to use if the module doesn't have one: should be no
 * less than #U_CELL_INFO_TEST_MIN_TIME (i.e. 21 July 2021 13:40:36)
 * plus any timezone offset.
 */
# define U_CELL_CFG_TEST_FIXED_TIME (1626874836 + (3600 * 24))
#endif

#define CELL_APN_USER      NULL
#define CELL_APN_PASS      NULL
// timeout para abertura de PDP (em segundos)
#define DATA_TIMEOUT_S 240
/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */
/*typedef struct {
    int32_t uartHandle; *< The handle returned by uPortUartOpen(). 
    uAtClientHandle_t atClientHandle; *< The handle returned by uAtClientAddExt(). 
    uDeviceHandle_t devHandle;  *< The device handle returned by uCellAdd(). 
} uCellTestPrivate_t;*/


/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// The RATs as an array.
uCellNetRat_t gMyRatList[] = {MY_RAT0, MY_RAT1, MY_RAT2};

/** Used for keepGoingCallback() timeout.
 */
static uTimeoutStop_t gTimeoutStop;

static int32_t gCallbackErrorCode = 0;

/** Handles.
 */
static uCellTestPrivate_t gHandles = U_CELL_PRIVATE_DEFAULTS;

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */
/** The string to put at the start of all prints from this test.
 */
#define U_TEST_PREFIX "U_CELL_CFG_TEST: "
/** Print a whole line, with terminator, prefixed for this test file.
 */
#define U_TEST_PRINT_LINE(format, ...) uPortLog(U_TEST_PREFIX format "\n", ##__VA_ARGS__)




// Cellular configuration.
// Set U_CFG_TEST_CELL_MODULE_TYPE to your module type,
// chosen from the values in cell/api/u_cell_module_type.h
//
// Note that the pin numbers are those of the MCU: if you
// are using an MCU inside a u-blox module the IO pin numbering
// for the module is likely different to that of the MCU: check
// the data sheet for the module to determine the mapping.

// DEVICE i.e. module/chip configuration: in this case a cellular
// module connected via UART
/*static const uDeviceCfg_t gDeviceCfg = {
    .deviceType = U_DEVICE_TYPE_CELL,
    .deviceCfg = {
        .cfgCell = {
            .moduleType = U_CFG_TEST_CELL_MODULE_TYPE,
            .pSimPinCode = NULL, // SIM pin 
            .pinEnablePower = U_CFG_APP_PIN_CELL_ENABLE_POWER,
            .pinPwrOn = U_CFG_APP_PIN_CELL_PWR_ON,
            .pinVInt = U_CFG_APP_PIN_CELL_VINT,
            .pinDtrPowerSaving = U_CFG_APP_PIN_CELL_DTR
        },
    },
    .transportType = U_DEVICE_TRANSPORT_TYPE_UART,
    .transportCfg = {
        .cfgUart = {
            .uart = U_CFG_APP_CELL_UART,
            .baudRate = U_CELL_UART_BAUD_RATE,
            .pinTxd = U_CFG_APP_PIN_CELL_TXD,  // Use -1 if on Zephyr or Linux or Windows
            .pinRxd = U_CFG_APP_PIN_CELL_RXD,  // Use -1 if on Zephyr or Linux or Windows
            .pinCts = U_CFG_APP_PIN_CELL_CTS,  // Use -1 if on Zephyr
            .pinRts = U_CFG_APP_PIN_CELL_RTS,  // Use -1 if on Zephyr

        },
    },
};*/
// NETWORK configuration for cellular
/*static uNetworkCfgCell_t gNetworkCfg = {
    .type = U_NETWORK_TYPE_CELL,
    .pApn = modem_apn,   //APN: NULL to accept default.  If using a Thingstream SIM enter "tsiot" here
    .timeoutSeconds = 240, // Connection timeout in seconds 
    // There are five additional fields here which we do NOT set,
    // we allow the compiler to set them to 0 and all will be fine.
    // The fields are:
    //
    // - "pKeepGoingCallback": you may set this field to a function
    //   of the form "bool keepGoingCallback(uDeviceHandle_t devHandle)",
    //   e.g.:
    //.pKeepGoingCallback = keepGoingCallback(devHandle);
    //   .pKeepGoingCallback = keepGoingCallback;
    //
    //   ...and your function will be called periodically during an
    //   abortable network operation such as connect/disconnect;
    //   if it returns true the operation will continue else it
    //   will be aborted, allowing you immediate control.  If this
    //   field is set, timeoutSeconds will be ignored.
    //
    .pUsername = modem_usr,
    .pPassword = modem_pwr,
    // - "pUsername" and "pPassword": if you are required to set a
    //   user name and password to go with the APN value that you
    //   were given by your service provider, set them here.
    //
    // - "authenticationMode": if you MUST give a user name and
    //   password and your cellular module does NOT support figuring
    //   out the authentication mode automatically (e.g. SARA-R4xx,
    //   LARA-R6 and LENA-R8 do not) then you must populate this field
    //   with the authentication mode that should be used, see
    //   #uCellNetAuthenticationMode_t in u_cell_net.h; there is no
    //   harm in populating this field even if the module _does_ support
    //   figuring out the authentication mode automatically but
    //   you ONLY NEED TO WORRY ABOUT IT if you were given that user
    //   name and password with the APN (which is thankfully not usual).
    //.pMccMnc = "72432"
    // - "pMccMnc": ONLY required if you wish to connect to a specific
    //   MCC/MNC rather than to the best available network; should point
    //   to the null-terminated string giving the MCC and MNC of the PLMN
    //   to use (for example "23410").
};*/


/* ----------------------------------------------------------------
 * STATIC FUNCTIONS CONFIGURATION
 * -------------------------------------------------------------- */

// Print out an address structure.
static void printAddress(const uSockAddress_t *pAddress,
                         bool hasPort)
{
    switch (pAddress->ipAddress.type) {
        case U_SOCK_ADDRESS_TYPE_V4:
            uPortLog("IPV4");
            break;
        case U_SOCK_ADDRESS_TYPE_V6:
            uPortLog("IPV6");
            break;
        case U_SOCK_ADDRESS_TYPE_V4_V6:
            uPortLog("IPV4V6");
            break;
        default:
            uPortLog("unknown type (%d)", pAddress->ipAddress.type);
            break;
    }

    uPortLog(" ");

    if (pAddress->ipAddress.type == U_SOCK_ADDRESS_TYPE_V4) {
        for (int32_t x = 3; x >= 0; x--) {
            uPortLog("%u",
                     (pAddress->ipAddress.address.ipv4 >> (x * 8)) & 0xFF);
            if (x > 0) {
                uPortLog(".");
            }
        }
        if (hasPort) {
            uPortLog(":%u", pAddress->port);
        }
    } else if (pAddress->ipAddress.type == U_SOCK_ADDRESS_TYPE_V6) {
        if (hasPort) {
            uPortLog("[");
        }
        for (int32_t x = 3; x >= 0; x--) {
            uPortLog("%x:%x", pAddress->ipAddress.address.ipv6[x] >> 16,
                     pAddress->ipAddress.address.ipv6[x] & 0xFFFF);
            if (x > 0) {
                uPortLog(":");
            }
        }
        if (hasPort) {
            uPortLog("]:%u", pAddress->port);
        }
    }
}

// Read and then set the band mask for a given RAT.
static void readAndSetBand(uDeviceHandle_t devHandle, uCellNetRat_t rat,
                           uint64_t bandMask1, uint64_t bandMask2)
{
    uint64_t readBandMask1;
    uint64_t readBandMask2;

    // Read the current band mask for information
    if (uCellCfgGetBandMask(devHandle, rat,
                            &readBandMask1, &readBandMask2) == 0) {
        uPortLog("### Band mask for RAT %s is 0x%08x%08x %08x%08x.\n", gpRatStr[rat],
                 (uint32_t) (readBandMask2 >> 32), (uint32_t) readBandMask2,
                 (uint32_t) (readBandMask1 >> 32), (uint32_t) readBandMask1);
        if ((readBandMask1 != bandMask1) || (readBandMask2 != bandMask2)) {
            // Set the band mask
            uPortLog("### Setting band mask for RAT %s to 0x%08x%08x %08x%08x...\n",
                     gpRatStr[rat],
                     (uint32_t) (bandMask2 >> 32), (uint32_t) (bandMask2),
                     (uint32_t) (bandMask1 >> 32), (uint32_t) (bandMask1));
            if (uCellCfgSetBandMask(devHandle, rat,
                                    bandMask1, bandMask2) != 0) {
                uPortLog("### Unable to change band mask for RAT %s, it is"
                         " likely your module does not support one of those"
                         " bands.\n", gpRatStr[rat]);
            }
        }
    } else {
        uPortLog("### Unable to read band mask for RAT %s.\n", gpRatStr[rat]);
    }
}

// Callback function for certain cellular network processes.
static bool keepGoingCallback(uDeviceHandle_t devHandle)
{
    bool keepGoing = true;

    if (devHandle != gHandles.cellHandle) {
        gCallbackErrorCode = 1;
    }

    if (uTimeoutExpiredMs(gTimeoutStop.timeoutStart,
                          gTimeoutStop.durationMs)) {
        keepGoing = false;
    }

    return keepGoing;
}


/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS:
 * -------------------------------------------------------------- */

// The entry point, main(): before this is called the system
// clocks must have been started and the RTOS must be running;
// we are in task space.

// Application entry point.

void DevLteConfig_init(void)
{
	int64_t timeLocal;
	int32_t timeZoneOffsetOriginalSeconds;
    uDeviceHandle_t devHandle = NULL;
    int32_t x;

    // 0) Inicializações básicas
    ESP_LOGI(TAG, "Inicializando uPort...");
    uPortInit();
    ESP_LOGI(TAG, "Inicializando uDevice...");
    uDeviceInit();

    // 1) Abre o dispositivo celular
    ESP_LOGI(TAG, "Abrindo dispositivo celular...");
    x = uDeviceOpen(&gDeviceCfg, &devHandle);
    if (x != 0) {
        ESP_LOGE(TAG, "uDeviceOpen falhou: %d", x);
        goto cleanup;
    }

    // 2) MNO profile
    {
        int32_t mno = uCellCfgGetMnoProfile(devHandle);
        if (mno >= 0 && mno != MY_MNO_PROFILE) {
            if (uCellCfgSetMnoProfile(devHandle, MY_MNO_PROFILE) == 0 &&
                uCellPwrRebootIsRequired(devHandle)) {
                ESP_LOGI(TAG, "Reboot para aplicar MNO profile...");
                uCellPwrReboot(devHandle, NULL);
            }
        }
    }

    // 3) Definir preferência de RAT (rank 0,1,2)
    for (int rank = 0; rank < 3; rank++) {
        uCellCfgSetRatRank(devHandle, gMyRatList[rank], rank);
    }
    if (uCellPwrRebootIsRequired(devHandle)) {
        ESP_LOGI(TAG, "Reboot para aplicar mudança de RAT...");
        uCellPwrReboot(devHandle, NULL);
    }

    // 4) Ajustar band masks para CAT-M1 / NB-IoT se usados
    for (int rank = 0; rank < 3; rank++) {
        uCellNetRat_t rat = uCellCfgGetRat(devHandle, rank);
        if (rat == U_CELL_NET_RAT_CATM1) {
            readAndSetBand(devHandle, rat,
                           MY_CATM1_BANDMASK1, MY_CATM1_BANDMASK2);
        } else if (rat == U_CELL_NET_RAT_NB1) {
            readAndSetBand(devHandle, rat,
                           MY_NB1_BANDMASK1, MY_NB1_BANDMASK2);
        }
    }
    if (uCellPwrRebootIsRequired(devHandle)) {
        ESP_LOGI(TAG, "Reboot para aplicar band masks...");
        uCellPwrReboot(devHandle, NULL);
    }
    
    // 6) Conectar dados (pelo método definido em connectivity.c)
    ESP_LOGI(TAG, "Tentando conexão de dados...");
    if (cell_connect(devHandle)) {
        ESP_LOGI(TAG, "Conexão de dados estabelecida!");
    } else {
        ESP_LOGE(TAG, "Falha na conexão de dados.");
    }
    

    // 7) Fecha o handle mas mantém o módulo ligado
    uDeviceClose(devHandle, false);

cleanup:
    // Finalização das APIs
    uDeviceDeinit();
    uPortDeinit();
    ESP_LOGI(TAG, "DevLteConfig_init concluído.");
}
    
    
#endif // #ifdef U_CFG_TEST_CELL_MODULE_TYPE


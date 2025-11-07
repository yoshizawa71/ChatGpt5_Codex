/*
 * u_cell_net_reg.c
 *
 *  Created on: 6 de set. de 2024
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

/* Only #includes of u_* and the C standard library are allowed here,
 * no platform stuff and no OS stuff.  Anything required from
 * the platform/OS must be brought in through u_port* to maintain
 * portability.
 */

/** @file
 * @brief Tests for the cellular network API: these should pass on all
 * platforms that have a cellular module connected to them.  They
 * are only compiled if U_CFG_TEST_CELL_MODULE_TYPE is defined.
 * IMPORTANT: see notes in u_cfg_test_platform_specific.h for the
 * naming rules that must be followed when using the U_PORT_TEST_FUNCTION()
 * macro.
 */
#include "esp_log.h"
#include "led_blink_control.h"
#include <stdint.h>

# ifdef U_CFG_OVERRIDE
#  include "u_cfg_override.h" // For a customer's configuration override
# endif

#include "stddef.h"    // NULL, size_t etc.
#include "stdint.h"    // int32_t etc.
#include "stdbool.h"
#include "string.h"    // memset(), strncpy()
#include "stdio.h"     // snprintf()

#include "u_cfg_sw.h"
#include "u_cfg_os_platform_specific.h"
#include "u_cfg_app_platform_specific.h"
#include "u_cfg_test_platform_specific.h"

#include "u_error_common.h"

#include "u_port_clib_platform_specific.h" /* Integer stdio, must be included
                                              before the other port files if
                                              any print or scan function is used. */
#include "u_port.h"
#include "u_port_debug.h"
#include "u_port_os.h"   // Required by u_cell_private.h
#include "u_port_uart.h"

#include "u_test_util_resource_check.h"

#include "u_timeout.h"

#include "u_at_client.h"

#include "u_cell_module_type.h"
#include "u_cell.h"
#include "u_cell_file.h"
#include "u_cell_net.h"     // Required by u_cell_private.h
#include "u_cell_private.h" // So that we can get at some innards
#include "u_cell_power_strategy.h"
#include "u_cell_pwr.h"

#include "u_cell_test_cfg.h"
#include "u_cell_test_private.h"
#include "u_cell_cfg.h"
#include "sara_r422.h"

#include "datalogger_control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "main.h"
#include "u_cell_info.h"

#ifdef U_CFG_TEST_CELL_MODULE_TYPE


/*xTaskHandle Cell_NetConnect_TaskHandle = NULL;

extern QueueHandle_t xQueue_NetConnect;
static bool NetConnect_Task_ON;*/

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/** The string to put at the start of all prints from this test.
 */
#define U_TEST_PREFIX "U_CELL_NET_TEST: "

/** Print a whole line, with terminator, prefixed for this test file.
 */
#define U_TEST_PRINT_LINE(format, ...) uPortLog(U_TEST_PREFIX format "\n", ##__VA_ARGS__)

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

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

/** Used for keepGoingCallback() timeout.
 */
static uTimeoutStop_t gTimeoutStop;

/** Handles.
 */
static uCellTestPrivate_t gHandles = U_CELL_TEST_PRIVATE_DEFAULTS;

/** The last status value passed to registerCallback.
 */
static uCellNetStatus_t gLastNetStatus = U_CELL_NET_STATUS_UNKNOWN;

/** Flag to show that connectCallback has been called.
 */
static bool gConnectCallbackCalled = false;

/** Whether gConnectCallbackCalled has been called with isConnected
 * true.
 */
static bool gHasBeenConnected = false;

/** A variable to track errors in the callbacks.
 */
static int32_t gCallbackErrorCode = 0;

static char modem_apn[30];
static char modem_usr[30];
static char modem_pwr[30];

static void deinit_cellNetConnect(void);

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// Callback function for certain cellular network processes.
static bool keepGoingCallback(uDeviceHandle_t cellHandle)
{
    bool keepGoing = true;

    if (cellHandle != gHandles.cellHandle) {
        gCallbackErrorCode = 1;
    }

    if (uTimeoutExpiredMs(gTimeoutStop.timeoutStart,
                          gTimeoutStop.durationMs)) {
        keepGoing = false;
    }

    return keepGoing;
}

// Callback for registration status.
static void registerCallback(uCellNetRegDomain_t domain,
                             uCellNetStatus_t status,
                             void *pParameter)
{

    // Note: not using asserts here as, when they go
    // off, the seem to cause stack overruns
    if (domain >= U_CELL_NET_REG_DOMAIN_MAX_NUM) {
        gCallbackErrorCode = 2;
    }

    if (status < U_CELL_NET_STATUS_UNKNOWN) {
        gCallbackErrorCode = 3;
    }
    if (status >= U_CELL_NET_STATUS_MAX_NUM) {
        gCallbackErrorCode = 4;
    }

    if (pParameter == NULL) {
        gCallbackErrorCode = 5;
    } else {
        if (strcmp((char *) pParameter, "Boo!") != 0) {
            gCallbackErrorCode = 6;
        }
    }

    if (domain == U_CELL_NET_REG_DOMAIN_PS) {
        gLastNetStatus = status;
    }
}

// Callback for base station connection status.
static void connectCallback(bool isConnected, void *pParameter)
{
    // Note: not using asserts here as, when they go
    // off, the seem to cause stack overruns
    if (pParameter == NULL) {
        gCallbackErrorCode = 7;
    } else {
        if (strcmp((char *) pParameter, "Bah!") != 0) {
            gCallbackErrorCode = 8;
        }
    }

    gConnectCallbackCalled = true;
    if (isConnected) {
        gHasBeenConnected = true;
    }
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

/** Test connecting and disconnecting and most things in-between.
 *
 * IMPORTANT: see notes in u_cfg_test_platform_specific.h for the
 * naming rules that must be followed when using the
 * U_PORT_TEST_FUNCTION() macro.
 */
//void cellNetConnect(uDeviceHandle_t devHandle)
esp_err_t esp_modem_set_apn(char* apn)
{
    strcpy(modem_apn, apn);
    return ESP_OK;
}

esp_err_t esp_modem_set_psw(char* psw)
{
    strcpy(modem_pwr, psw);
    return ESP_OK;
}

esp_err_t esp_modem_set_usr(char* usr)
{
    strcpy(modem_usr, usr);
    return ESP_OK;
}
  
 int32_t cell_Net_Register_Connect(uDeviceHandle_t *pDevHandle)                  
{
    uCellPrivateInstance_t *pInstance;
    const uCellPrivateModule_t *pModule;
    
    uCellNetStatus_t status = U_CELL_NET_STATUS_UNKNOWN;
    gLastNetStatus = U_CELL_NET_STATUS_UNKNOWN;
 
    uCellNetRat_t rat = U_CELL_NET_RAT_UNKNOWN_OR_NOT_USED;
    int32_t x, y,z;
    char buffer[U_CELL_NET_IP_ADDRESS_SIZE * 2];
    int32_t mcc = 0;
    int32_t mnc = 0;
    char parameter1[5]; // enough room for "Boo!"
    char parameter2[5]; // enough room for "Bah!"
    int32_t resourceCount;
    int32_t networkCause;
    int32_t errorCode;
    
    strncpy(parameter1, "Boo!", sizeof(parameter1));
    strncpy(parameter2, "Bah!", sizeof(parameter2));

     //#########################
esp_modem_set_apn(get_apn());
esp_modem_set_usr(get_lte_user());
esp_modem_set_psw(get_lte_pw());

    // In case a previous test failed
    uCellTestPrivateCleanup(&gHandles);
    
   // Initialise the APIs we will need
    uPortInit();
    uDeviceInit();

    // Open the device
errorCode = uDeviceOpen(&gDeviceCfg, pDevHandle);
    uPortLog("## Opened device with return code %d.\n", errorCode);
    

if (errorCode == 0) {
        ESP_LOGI("LTE_UART_PWR", "U_CFG_APP_PIN_CELL_DTR=%d", U_CFG_APP_PIN_CELL_DTR);
        int32_t runtimeDtrPin = uCellPwrGetDtrPowerSavingPin(*pDevHandle);
        if (runtimeDtrPin >= 0) {
            ESP_LOGI("LTE_UART_PWR", "ubxlib DTR pin (runtime)=%ld", (long) runtimeDtrPin);
        } else {
            ESP_LOGW("LTE_UART_PWR", "ubxlib DTR pin não configurado (ret=%ld)", (long) runtimeDtrPin);
        }
            // Força NB‑IoT (prioridade 0) e só depois GSM/EDGE (prioridade 1)
    uCellCfgSetRatRank(*pDevHandle, U_CELL_NET_RAT_NB1,              0);
    uCellCfgSetRatRank(*pDevHandle, U_CELL_NET_RAT_GSM_GPRS_EGPRS,   1);
    uCellCfgSetRatRank(*pDevHandle, U_CELL_NET_RAT_UNKNOWN_OR_NOT_USED, 2);

//---------------------------------------------------------
// Obter cliente AT existente
        uAtClientHandle_t atHandle;
        errorCode = uCellAtClientHandleGet(*pDevHandle, &atHandle);
       if (errorCode == 0 && atHandle != NULL) {
        int32_t uartPsmErr = lte_uart_psm_enable(*pDevHandle, true, NULL);
        if (uartPsmErr < 0) {
            ESP_LOGW("LTE_UART_PWR", "Falha ao configurar UPSV/DTR (err=%ld)", (long) uartPsmErr);
        }
        // Desativar URCs de registro para evitar inundação
        uPortLog("U_CELL: desativando URCs antes de registrar\n");
        uAtClientLock(atHandle);
        // Desativa URCs de CREG, CGREG, CEREG
        uAtClientCommandStart(atHandle, "AT+CREG=0");
        uAtClientCommandStopReadResponse(atHandle);
        uAtClientCommandStart(atHandle, "AT+CGREG=0");
        uAtClientCommandStopReadResponse(atHandle);
        uAtClientCommandStart(atHandle, "AT+CEREG=0");
        uAtClientCommandStopReadResponse(atHandle);
        // Opcional: desativa CSCON URC
        uAtClientCommandStart(atHandle, "AT+CSCON=0");
        uAtClientCommandStopReadResponse(atHandle);
        uAtClientUnlock(atHandle);
        } else {
            uPortLog("U_CELL: falha ao obter cliente AT, errorCode=%d\n", errorCode);
        }

//---------------------------------------------------------	
             // Obtain the initial resource count
             resourceCount = uTestUtilGetDynamicResourceCount();

             // Get the private module data as we need it for testing
             pModule = pUCellPrivateGetModule(*pDevHandle);
             U_PORT_TEST_ASSERT(pModule != NULL);
             //lint -esym(613, pModule) Suppress possible use of NULL pointer
             // for pModule from now on
             // Set a registration status callback
             U_PORT_TEST_ASSERT(uCellNetSetRegistrationStatusCallback(*pDevHandle,
                                                             registerCallback,
                                                             (void *) parameter1) == 0);
             errorCode= uCellNetSetRegistrationStatusCallback(*pDevHandle,
                                                             registerCallback,
                                                             (void *) parameter1) == 0;   
                                                                                                                   
              uPortLog(">>>> registration status callback with return code %d\n", errorCode);                                                        
 
                                                             
             // Set a connection status callback, if possible
             gCallbackErrorCode = 0;
             errorCode = uCellNetSetBaseStationConnectionStatusCallback(*pDevHandle,
                                                       connectCallback,
                                                       (void *) parameter2);
              uPortLog("BaseStationConnectionStatusCallback Return Error code %d.\n", errorCode);      
     
                                            
  
             if (U_CELL_PRIVATE_HAS(pModule, U_CELL_PRIVATE_FEATURE_CSCON)) {
                 U_PORT_TEST_ASSERT(errorCode == 0);
             } else {
                 U_PORT_TEST_ASSERT(errorCode < 0);
             }

             U_PORT_TEST_ASSERT(gLastNetStatus == U_CELL_NET_STATUS_UNKNOWN);
 
             // Read the authentication mode for PDP contexts
             errorCode = uCellNetGetAuthenticationMode(*pDevHandle);
             U_PORT_TEST_ASSERT(errorCode >= 0);
             U_PORT_TEST_ASSERT(errorCode < U_CELL_NET_AUTHENTICATION_MODE_MAX_NUM);
             if (U_CELL_PRIVATE_HAS(pModule, U_CELL_PRIVATE_FEATURE_AUTHENTICATION_MODE_AUTOMATIC)) {
                 U_PORT_TEST_ASSERT(errorCode == U_CELL_NET_AUTHENTICATION_MODE_AUTOMATIC);
             } else {
                 U_PORT_TEST_ASSERT(errorCode == U_CELL_NET_AUTHENTICATION_MODE_NOT_SET);
             }
    
              uPortLog(" Authentication MODE with return code %d\n", errorCode);
    
             // Try setting all of the permitted authentication modes
             U_PORT_TEST_ASSERT(uCellNetSetAuthenticationMode(*pDevHandle,
                                                     U_CELL_NET_AUTHENTICATION_MODE_PAP) == 0);
             U_PORT_TEST_ASSERT(uCellNetGetAuthenticationMode(*pDevHandle) == U_CELL_NET_AUTHENTICATION_MODE_PAP);
             errorCode= uCellNetGetAuthenticationMode(*pDevHandle);
                uPortLog(">>>> Authentication MODE with return code %d\n", errorCode);
     
             // Register with the cellular network and activate a PDP context.
             gTimeoutStop.timeoutStart = uTimeoutStart();
             gTimeoutStop.durationMs = U_CELL_TEST_CFG_CONTEXT_ACTIVATION_TIMEOUT_SECONDS * 1000;
                        
            errorCode = uCellNetConnect(*pDevHandle, NULL,modem_apn,modem_usr,
                                modem_pwr, keepGoingCallback);                                        
                       
        if (uCellNetIsRegistered(*pDevHandle)){                    
              uPortLog(">>>>>>> Net is Registered <<<<<<<<\n"); 
            
            cellSyncTime(*pDevHandle);//Atualiza data e hora
		     // Check that the status is registered

             status = uCellNetGetNetworkStatus(*pDevHandle, U_CELL_NET_REG_DOMAIN_PS);
             U_PORT_TEST_ASSERT((status == U_CELL_NET_STATUS_REGISTERED_HOME) ||
                                (status == U_CELL_NET_STATUS_REGISTERED_ROAMING) ||
                                (status == U_CELL_NET_STATUS_REGISTERED_NO_CSFB_HOME) ||
                                (status == U_CELL_NET_STATUS_REGISTERED_NO_CSFB_ROAMING));
             U_TEST_PRINT_LINE("gLastNetStatus is %d.", gLastNetStatus);
             U_PORT_TEST_ASSERT(gLastNetStatus == status);	
//=======================================================================			
			 if(status == U_CELL_NET_STATUS_REGISTERED_HOME||status == U_CELL_NET_STATUS_REGISTERED_ROAMING){
			 blink_set_profile(BLINK_PROFILE_COMM_REGISTERED);
			 
int32_t csq = cellGetCsqRaw(*pDevHandle);
if (csq >= 0) {
    uPortLog("CSQ raw = %d\n", csq);
} else {
    uPortLog("Não foi possível obter CSQ válido\n");
}
  
uPortLog("======>>> CSQ estimado: %d\n", csq);
            set_csq(csq);
            }
 //=======================================================================    
              
  // Reativar URCs após registro
    errorCode = uCellAtClientHandleGet(*pDevHandle, &atHandle);
    if (errorCode == 0 && atHandle != NULL) {
        uPortLog("U_CELL: reativando URCs após registrar\n");
        uAtClientLock(atHandle);
        // Reativa URCs de CREG, CGREG, CEREG
        uAtClientCommandStart(atHandle, "AT+CREG=2");
        uAtClientCommandStopReadResponse(atHandle);
        uAtClientCommandStart(atHandle, "AT+CGREG=2");
        uAtClientCommandStopReadResponse(atHandle);
        uAtClientCommandStart(atHandle, "AT+CEREG=2");
        uAtClientCommandStopReadResponse(atHandle);
        // Opcional: reativa CSCON URC
        uAtClientCommandStart(atHandle, "AT+CSCON=1");
        uAtClientCommandStopReadResponse(atHandle);
        uAtClientUnlock(atHandle);
        } else {
                uPortLog("U_CELL: falha ao obter cliente AT para reativação, errorCode=%d\n", errorCode);
               }
//-------------------------------------------------------------      

             // Get the IP address
               // Check that the RAT we're registered on
             rat = uCellNetGetActiveRat(*pDevHandle);
             U_PORT_TEST_ASSERT((rat > U_CELL_NET_RAT_UNKNOWN_OR_NOT_USED) &&
                                (rat < U_CELL_NET_RAT_MAX_NUM));
                       
             printf("=====>THE RAT is %d\n",rat);

             // Get the network cause
             networkCause = uCellNetGetLastEmmRejectCause(*pDevHandle);
             U_TEST_PRINT_LINE("network cause is now %d.", networkCause);
             U_PORT_TEST_ASSERT((networkCause == (int32_t) U_ERROR_COMMON_NOT_SUPPORTED) ||
                                (networkCause == 0));
      
             // Get the MCC/MNC
             U_PORT_TEST_ASSERT(uCellNetGetMccMnc(*pDevHandle, &mcc, &mnc) == 0);
             U_PORT_TEST_ASSERT(mcc > 0);
             U_PORT_TEST_ASSERT(mnc > 0);
             U_TEST_PRINT_LINE(">>>>MCC %d.\n", mcc);
    
             U_TEST_PRINT_LINE(">>>>MNC %d.\n", mnc);
    
             memset(buffer, '|', sizeof(buffer));
             y = uCellNetGetIpAddressStr(*pDevHandle, buffer);
             uPortLog("## uCellNetGetIpAddressStr %d.\n", y);
             
            status = uCellNetGetNetworkStatus(*pDevHandle, U_CELL_NET_REG_DOMAIN_PS);
             printf(">>>>>STATUS PS ===>> %d\n", status);
             
             status = uCellNetGetNetworkStatus(*pDevHandle,
                                          U_CELL_NET_REG_DOMAIN_CS);
             printf(">>>>>STATUS CS ===>> %d\n", status);

        }else {
                printf(" ----->>>NAO FOI POSSIVEL REGISTRAR<<<-----\n");
                U_TEST_PRINT_LINE("deactivating PDP context...");
                uCellNetDeactivate(*pDevHandle, NULL);
                uPortLog("### Unable to bring up the device###!\n");
                cellNet_Close_CleanUp(*pDevHandle);
         //       uDeviceClose(*pDevHandle, false);
                return errorCode;
                }
     
} else {
        uPortLog("### Unable to bring up the device###!\n");
    //    uDeviceClose(*pDevHandle, false);
        cellNet_Close_CleanUp(*pDevHandle);
        return errorCode;
        }
    return errorCode;

}

/** Clean-up to be run at the end of this round of tests, just
 * in case there were test failures which would have resulted
 * in the deinitialisation being skipped.*/
 
int32_t cellNet_Close_CleanUp(uDeviceHandle_t devHandle)
{
    int32_t errorCode;

    U_TEST_PRINT_LINE("deactivating PDP context...");
    errorCode = uCellNetDeactivate(devHandle, NULL);
    printf("!!!!!Deactivate the PDP context!!!!! ErrorCode=  %ld\n", errorCode);

    // ---------------------------------------------
    // Desativar URCs antes de fechar
    uAtClientHandle_t atHandle = NULL;
    errorCode = uCellAtClientHandleGet(devHandle, &atHandle);
    if (errorCode == 0 && atHandle != NULL) {
        uPortLog("U_CELL: desativando URCs antes de fechar\n");

        const char *urc_cmds[] = { "AT+CREG=0", "AT+CGREG=0", "AT+CEREG=0" };
        for (size_t i = 0; i < sizeof(urc_cmds) / sizeof(urc_cmds[0]); ++i) {
            uAtClientLock(atHandle);
            uAtClientCommandStart(atHandle, urc_cmds[i]);
            uAtClientCommandStop(atHandle);
            uAtClientResponseStart(atHandle, NULL);
            uAtClientResponseStop(atHandle);
            uAtClientUnlock(atHandle);
            vTaskDelay(pdMS_TO_TICKS(50)); // pausa curta para evitar bloqueio longo
        }

        // Verificar estado do modem via CPAS
        uAtClientLock(atHandle);
        uAtClientCommandStart(atHandle, "AT+CPAS");
        uAtClientCommandStop(atHandle);
        uAtClientResponseStart(atHandle, "+CPAS:");
        int32_t status = uAtClientReadInt(atHandle);
        uAtClientResponseStop(atHandle);
        uAtClientUnlock(atHandle);
        uPortLog("U_CELL: modem status (CPAS) = %d\n", status);

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // Substitui uPortTaskBlock(500) por vários yields curtos para não travar watchdog
    for (int i = 0; i < 10; ++i) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // ---------------------------------------------
    // Desliga o SARA corretamente (passando o handle AT)
    if (atHandle != NULL) {
        turn_off_sara(atHandle); // agora usa o handle correto
    } else {
        ESP_LOGW("4G_SYSTEM_CONTROL_TAG", "AT handle inválido ao desligar SARA");
    }

    // Fecha o device depois do turn_off_sara
    errorCode = uDeviceClose(devHandle, false);
    uPortLog("## Close device with return code %d.\n", errorCode);

    uCellTestPrivateCleanup(&gHandles);
    uPortDeinit();
    uDeviceDeinit();
    // Printed for information: asserting happens in the postamble
    uTestUtilResourceCheck(U_TEST_PREFIX, NULL, true);

    return errorCode;
}


#endif // #ifdef U_CFG_TEST_CELL_MODULE_TYPE

// End of file

/*void init_cellNetConnect(void)
{
	if (Cell_NetConnect_TaskHandle == NULL)
	   {
        xTaskCreatePinnedToCore( Cell_NetConnect_Task, "Cell_NetConnect_Task", 10000, NULL, 1, &Cell_NetConnect_TaskHandle,1);
       }
}

static void deinit_cellNetConnect(void)
{
  vTaskDelete(Cell_NetConnect_TaskHandle);
  Cell_NetConnect_TaskHandle = NULL;

}*/
 


/*
 * include.h
 *
 *  Created on: 2 de set. de 2024
 *      Author: geopo
 */

#ifndef MAIN_U_CELL_TEST_H_
#define MAIN_U_CELL_TEST_H_
#include "u_cell_module_type.h"

#define U_CFG_TEST_CELL_MODULE_TYPE U_CELL_MODULE_TYPE_SARA_R422

#define TIME_REFERENCE_1         1735689601 // Tempo definido para 0 horas de Janeiro de 2025
#define TIME_REFERENCE_2         1893456000 // Tempo definido para 0 horas de Janeiro de 2030

#include "esp_err.h"
#include "esp_netif_types.h"
#include "stdio.h"
#include "u_device_handle.h"
#include "stdbool.h"
#include "u_cell_net.h"
#include "u_cfg_os_platform_specific.h"
#include "u_cfg_app_platform_specific.h"
#include "u_cfg_test_platform_specific.h"
//================
#include "u_cfg_sw.h"
#include "u_error_common.h"
#include "u_port_clib_platform_specific.h" /* Integer stdio, must be included
                                              before the other port files if
                                              any print or scan function is used. */
#include "u_at_client.h"
#include "u_cell.h"

// The names for each RAT, for debug purposes
static const char *const gpRatStr[] = {"unknown or not used",
                                       "GSM/GPRS/EGPRS",
                                       "GSM Compact",
                                       "UTRAN",
                                       "EGPRS",
                                       "HSDPA",
                                       "HSUPA",
                                       "HSDPA/HSUPA",
                                       "LTE",
                                       "EC GSM",
                                       "CAT-M1",
                                       "NB1"
                                      };
                                      
static const uDeviceCfg_t gDeviceCfg = {
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
};                                      

esp_err_t turn_off_sara(uAtClientHandle_t handle);

esp_err_t init_ppp(void);
void set_ppp_device_handle(void *devHandle);
void set_ppp_netif(esp_netif_t *netif);

void DevLteConfig_init (void);

int32_t cell_Net_Register_Connect(uDeviceHandle_t *pDevHandle);
int32_t cellNet_Close_CleanUp(uDeviceHandle_t devHandle);
void init_LTE_System(void);
//void Cell_NetConnect_Task (void* pvParameters);
bool NetConnect_task_status(void);

void init_cellNetConnect(void);
void Cell_NetConnect_Task (void* pvParameters);


void cellNetScanRegAct_GPRS(uDeviceHandle_t devHandle);
//void cell_Http_com(uDeviceHandle_t devHandle);
bool ucell_Http_connection(uDeviceHandle_t devHandle);

bool ucell_MqttClient_connection (uDeviceHandle_t devHandle);

//void SARA_R422_init_ON (void);

void CellLteCfg (void);
//void HttpClient(void);

//--------------------
// Module information
//--------------------

void cellInfoImeiEtc(void);
void cellInfoRadioParameters(void);
void cell_get_local_time(void);
void cellSyncTime(uDeviceHandle_t devHandle);
int32_t cellGetCsqRaw(uDeviceHandle_t devHandle);

void PppEspIdfSockets(void);


/*void cellInfoImeiEtc(void);
void nbiot_registration(void);
void cellCfgGreeting(void);

void cellPppBasic(void);

void atClientInitialisation(void);
void atClientConfiguration(void);

void AT_busca_operadora(void);*/

/*void atClientCommandSet1(void);
void atClientCommandSet2(void);
void atClientCleanUp(void);*/

int32_t uCellFileDelete(uDeviceHandle_t devHandle,const char *pFileName);

#endif /* MAIN_U_CELL_TEST_H_ */

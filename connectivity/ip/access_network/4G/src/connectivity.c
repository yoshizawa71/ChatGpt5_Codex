/*
 * connectivity.c
 *
 *  Created on: 4 de jul. 2025
 *      Author: geopo
 */

#include "connectivity.h"

#include "u_at_client.h"
#include "u_cell.h"  
#include "esp_log.h"
#include "u_cell_cfg.h"
#include "u_cell_net.h"
#include "u_cell_pwr.h"
 
#include "datalogger_control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char TAG[] = "CONNECTIVITY";

#define USE_AT_CONNECT 0
// Declaração interna
static bool cellular_setup(uDeviceHandle_t devHandle);

/**
 * @brief Tenta abrir PDP context (NB-IoT) e, em caso de falha,
 *        faz fallback para GPRS/EDGE via UBX-LIB.
 */
static bool cellular_setup(uDeviceHandle_t devHandle)
{
    int32_t err;
    char ipStr[16];  // xxx.xxx.xxx.xxx\0

    // 0) Registrar PS (modo automático, MCC/MNC NULL)
    err = uCellNetRegister(devHandle,
                           NULL,   // pMccMnc NULL → seleção automática
                           NULL);  // sem callback de progresso
    if (err < 0) {
        ESP_LOGW(TAG, "Registro PS inicial falhou (%d)", err);
    }

    // 1) Tentar abrir PDP context via NB-IoT
    err = uCellNetConnect(devHandle,
                          NULL,                  // pMccMnc NULL → automático
                          get_apn(),             // APN
                          has_network_user_enabled() ? get_network_user() : NULL,
                          has_network_pw_enabled()   ? get_network_pw()   : NULL,
                          NULL);                 // sem callback de progresso
    if (err != 0) {
        ESP_LOGW(TAG, "NB-IoT PDP falhou (%d), fazendo fallback para GPRS/EDGE", err);
        // 1.1) Desconectar sessão anterior
        uCellNetDisconnect(devHandle,
                           NULL);  // sem callback :contentReference[oaicite:6]{index=6}

        // 1.2) Forçar 2G (GPRS+EDGE) como RAT rank 0
        uCellCfgSetRatRank(devHandle,
                           U_CELL_NET_RAT_GSM_GPRS_EGPRS,
                           0);
        if (uCellPwrRebootIsRequired(devHandle)) {
            ESP_LOGI(TAG, "Reboot para aplicar RAT 2G");
            uCellPwrReboot(devHandle, NULL);
        }

        // 1.3) Re-registrar PS em 2G
        err = uCellNetRegister(devHandle,
                               NULL,
                               NULL);  // automático :contentReference[oaicite:7]{index=7}
        if (err < 0) {
            ESP_LOGW(TAG, "Registro PS GPRS falhou (%d)", err);
        }

        // 1.4) Tentar abrir PDP em GPRS
        err = uCellNetConnect(devHandle,
                              NULL,
                              get_apn(),
                              has_network_user_enabled() ? get_network_user() : NULL,
                              has_network_pw_enabled()   ? get_network_pw()   : NULL,
                              NULL);
        if (err != 0) {
            ESP_LOGE(TAG, "PDP em GPRS falhou (%d)", err);
            return false;
        }
    }

    // 2) Obter o IP atribuído
    err = uCellNetGetIpAddressStr(devHandle, ipStr);
    if (err < 0) {
        ESP_LOGE(TAG, "Falha ao obter IP (%d)", err);
        return false;
    }
    ESP_LOGI(TAG, "Conectado com IP: %s", ipStr);

    return true;
}


// fallback via UBX-API (Cat-NB1 / GPRS automático)
bool cell_connect_ubx(uDeviceHandle_t devHandle)
{
    int32_t err;
    char ipStr[64]; // buffer para o IP (IPv4 cabe em 16, mas deixei generoso)

    // 1) Tenta NB-IoT
    ESP_LOGI(TAG, "1) Tentando PDP via NB-IoT...");
    err = uCellNetConnect(devHandle,
                          get_apn(),
                          get_lte_user(),
                          get_lte_pw(),
                          NULL,
                          NULL);
    if (err != 0) {
        ESP_LOGW(TAG, "   NB-IoT PDP falhou (%d), iniciando fallback para GPRS/EDGE", err);

        // 1.1) Desconecta qualquer sessão anterior
        ESP_LOGI(TAG, "   Desconectando sessão anterior (se existir)...");
        uCellNetDisconnect(devHandle, NULL);

        // 1.2) Força 2G como rank0
        ESP_LOGI(TAG, "   Definindo GPRS/EDGE como RAT rank 0...");
        uCellCfgSetRatRank(devHandle, U_CELL_NET_RAT_GSM_GPRS_EGPRS, 0);

        // 1.3) Tenta novamente
        ESP_LOGI(TAG, "2) Tentando PDP via GPRS/EDGE...");
        err = uCellNetConnect(devHandle,
                              get_apn(),
                              get_lte_user(),
                               get_lte_pw(),
                              NULL,
                              NULL);
        if (err != 0) {
            ESP_LOGE(TAG, "   GPRS/EDGE PDP falhou (%d), abortando conexão de dados", err);
            return false;
        }
    }

    // 2) Se chegou aqui, PDP foi aberto com sucesso
    ESP_LOGI(TAG, "   PDP aberto com sucesso!");

    // 3) Obter IP atribuído
    ESP_LOGI(TAG, "3) Obtendo IP atribuído ao PDP...");
    err = uCellNetGetIpAddressStr(devHandle, ipStr);
    if (err != 0) {
        ESP_LOGE(TAG, "   Falha ao obter IP (%d)", err);
        return false;
    }
    ESP_LOGI(TAG, "   Endereço IP obtido: %s", ipStr);

    return true;
}
// implementação via AT
bool cell_connect_at(uDeviceHandle_t devHandle) {
    uAtClientHandle_t atHandle;
    int32_t err;
    int32_t regState = 0;
    char ipStr[U_CELL_NET_IP_ADDRESS_SIZE];

    // 1) Obter o handle do AT client
    err = uCellAtClientHandleGet(devHandle, &atHandle);
    if (err != 0 || atHandle == NULL) {
        ESP_LOGE(TAG, "Failed to get AT client");
        return false;
    }

    // 2) Desativar URCs antes de tudo
    uAtClientLock(atHandle);
    uAtClientCommandStart(atHandle, "AT+CREG=0");
    uAtClientCommandStopReadResponse(atHandle);
    uAtClientCommandStart(atHandle, "AT+CGREG=0");
    uAtClientCommandStopReadResponse(atHandle);
    uAtClientCommandStart(atHandle, "AT+CEREG=0");
    uAtClientCommandStopReadResponse(atHandle);
    uAtClientUnlock(atHandle);

    // 3) Habilitar URCs de CGREG para podermos checar registro
    uAtClientLock(atHandle);
    uAtClientCommandStart(atHandle, "AT+CGREG=1");
    uAtClientCommandStopReadResponse(atHandle);
    uAtClientUnlock(atHandle);

    // 4) Poll “AT+CGREG?” até “+CGREG: <n>,1” ou timeout
    for (int i = 0; i < 10; i++) {
        uAtClientLock(atHandle);
        uAtClientCommandStart(atHandle, "AT+CGREG?");
        uAtClientCommandStop(atHandle);
        uAtClientResponseStart(atHandle, "+CGREG:");
        (void)uAtClientReadInt(atHandle);        // pula <n>
        regState = uAtClientReadInt(atHandle);   // lê <stat>
        uAtClientResponseStop(atHandle);
        err = uAtClientUnlock(atHandle);
        if (err == 0 && regState == 1) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (regState != 1) {
        ESP_LOGE(TAG, "PS registration failed");
        return false;
    }

    // 5) Definir o PDP context (APN)
    uAtClientLock(atHandle);
    uAtClientCommandStart(atHandle, "AT+CGDCONT=1");
    uAtClientWriteString(atHandle, ",\"IP\",\"", false);
    uAtClientWriteString(atHandle, get_apn(), true);
    uAtClientCommandStopReadResponse(atHandle);
    uAtClientUnlock(atHandle);

    // 6) Ativar o PDP context
    uAtClientLock(atHandle);
    uAtClientCommandStart(atHandle, "AT+CGACT=1,1");
    uAtClientCommandStopReadResponse(atHandle);
    uAtClientUnlock(atHandle);

    // 7) Ler o IP atribuído via +CGPADDR
    uAtClientLock(atHandle);
    uAtClientCommandStart(atHandle, "AT+CGPADDR=1");
    uAtClientCommandStop(atHandle);
    uAtClientResponseStart(atHandle, "+CGPADDR:");
    (void)uAtClientReadInt(atHandle);                      // pula o “1,”
    int bytes = uAtClientReadString(atHandle,
                                    ipStr,
                                    sizeof(ipStr),
                                    false);                // lê o IP
    uAtClientResponseStop(atHandle);
    uAtClientUnlock(atHandle);

    if (bytes <= 0) {
        ESP_LOGE(TAG, "No IP in +CGPADDR");
        return false;
    }
    ESP_LOGI(TAG, "IP obtido via AT: %s", ipStr);

    // 8) Reativar URCs
    uAtClientLock(atHandle);
    uAtClientCommandStart(atHandle, "AT+CREG=2");
    uAtClientCommandStopReadResponse(atHandle);
    uAtClientCommandStart(atHandle, "AT+CGREG=2");
    uAtClientCommandStopReadResponse(atHandle);
    uAtClientCommandStart(atHandle, "AT+CEREG=2");
    uAtClientCommandStopReadResponse(atHandle);
    uAtClientUnlock(atHandle);

    return true;
}
// wrapper público
#if USE_AT_CONNECT
bool cell_connect(uDeviceHandle_t devHandle) {
    return cell_connect_at(devHandle);
}
#else
bool cell_connect(uDeviceHandle_t devHandle) {
    return cell_connect_ubx(devHandle);
}
#endif

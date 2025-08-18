/*
 * u_cell_sms.c
 *
 *  SMS feature for u-blox ubxlib (SARA-R422):
 *  - cellSmsInit: configure text mode and URC indications
 *  - cellSmsSend: send an SMS
 *  - cellSmsRead: read an SMS by index (stub)
 *  - cellSmsDelete: delete an SMS by index
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "u_cell_sms.h"         // Header for SMS functions
#include "u_at_client.h"        // AT client API
#include "u_port.h"             // uPort definitions
#include "u_error_common.h"     // Common error codes
#include "u_cell.h"             // Public cell API types
#include "u_cell_private.h"     // Private instance, atHandle member

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "u_device.h"

//extern U_PORT_MUTEX_DEF(gUCellPrivateMutex);
//---------------------------------------------------------------------------
// Internals

uAtClientHandle_t getAtHandle(uDeviceHandle_t devHandle)
{
    // devHandle é, na verdade, um ponteiro para uCellPrivateInstance_t
    const uCellPrivateInstance_t *pInstance =
        (const uCellPrivateInstance_t *) devHandle;
    return pInstance->atHandle;
}

//---------------------------------------------------------------------------
// Inicializa o serviço de SMS (text mode, URCs, etc)
int32_t cellSmsInit(uDeviceHandle_t devHandle)
{
    int32_t errorCode = (int32_t) U_ERROR_COMMON_NOT_INITIALISED;
    uCellPrivateInstance_t *pInstance;
    uAtClientHandle_t atHandle;
    int32_t service, mt, mo, bc;

    if (devHandle == NULL) {
        return (int32_t) U_ERROR_COMMON_INVALID_PARAMETER;
    }

    // Protege todo acesso interno ao atHandle
    U_PORT_MUTEX_LOCK(gUCellPrivateMutex);

    pInstance = pUCellPrivateGetInstance(devHandle);
    if (pInstance == NULL) {
        goto exit;
    }
    atHandle = pInstance->atHandle;
    if (atHandle == NULL) {
        goto exit;
    }

    // 1) Lê o perfil SMS atual (AT+CSMS?)
    uAtClientLock(atHandle);
    uAtClientCommandStart(atHandle, "AT+CSMS?");
    uAtClientCommandStop(atHandle);
    uAtClientResponseStart(atHandle, "+CSMS:");
    service = uAtClientReadInt(atHandle);
    mt      = uAtClientReadInt(atHandle);
    mo      = uAtClientReadInt(atHandle);
    bc      = uAtClientReadInt(atHandle);
    uAtClientResponseStop(atHandle);
    errorCode = uAtClientErrorGet(atHandle);
    uAtClientUnlock(atHandle);
    if (errorCode < 0) {
        goto exit;
    }
    uPortLog("U_CELL_SMS: perfil SMS lido: service=%d, MT=%d, MO=%d, BC=%d\n",
             service, mt, mo, bc);

    // 2) Ativa o serviço CS (AT+CSMS=1)
    uAtClientLock(atHandle);
    uAtClientCommandStart(atHandle, "AT+CSMS=");
    uAtClientWriteInt(atHandle, 1);
    uAtClientCommandStopReadResponse(atHandle);
    errorCode = uAtClientErrorGet(atHandle);
    uAtClientUnlock(atHandle);
    if (errorCode < 0) {
        goto exit;
    }

    // 3) Modo texto (AT+CMGF=1)
    uAtClientLock(atHandle);
    uAtClientCommandStart(atHandle, "AT+CMGF=");
    uAtClientWriteInt(atHandle, 1);
    uAtClientCommandStopReadResponse(atHandle);
    errorCode = uAtClientErrorGet(atHandle);
    uAtClientUnlock(atHandle);
    if (errorCode < 0) {
        goto exit;
    }

    // 4) Notificações URC de SMS (AT+CNMI=2,1,0,0,0)
    uAtClientLock(atHandle);
    uAtClientCommandStart(atHandle, "AT+CNMI=");
    uAtClientWriteInt(atHandle, 2);
    uAtClientWriteInt(atHandle, 1);
    uAtClientWriteInt(atHandle, 0);
    uAtClientWriteInt(atHandle, 0);
    uAtClientWriteInt(atHandle, 0);
    uAtClientCommandStopReadResponse(atHandle);
    errorCode = uAtClientErrorGet(atHandle);
    uAtClientUnlock(atHandle);
    if (errorCode < 0) {
        goto exit;
    }

    // Tudo OK
    errorCode = 0;

exit:
    U_PORT_MUTEX_UNLOCK(gUCellPrivateMutex);
    return errorCode;
}

//================================================================
// Envia um SMS: número no formato "+55XXXXXXXXX", texto é message body
int32_t cellSmsSend(uDeviceHandle_t devHandle,
                    const char *number,
                    const char *text)
{
    uCellPrivateInstance_t *pInstance;
    uAtClientHandle_t atHandle;
    const char ctrlZ = 0x1A;
    int32_t errorCode = (int32_t) U_ERROR_COMMON_UNKNOWN;
    char commandBuffer[64]; // Buffer para o comando AT+CMGS
    char scaBuffer[32];    // Buffer para o SMSC
    char responseBuffer[64]; // Buffer para resposta bruta
    int messageReference = -1;

    // 1) Validação de parâmetros
    if ((devHandle == NULL) || (number == NULL) || (text == NULL)) {
        return (int32_t) U_ERROR_COMMON_INVALID_PARAMETER;
    }

    // 2) Extrai a instância privada e o atHandle
    pInstance = pUCellPrivateGetInstance(devHandle);
    if (pInstance == NULL) {
        return (int32_t) U_ERROR_COMMON_NOT_INITIALISED;
    }
    atHandle = pInstance->atHandle;
    if (atHandle == NULL) {
        return (int32_t) U_ERROR_COMMON_NOT_INITIALISED;
    }

    // 3) Protege com mutex de instância
    uPortMutexLock(gUCellPrivateMutex);

    // 4) Aumenta timeout para aguardar respostas (60 segundos)
    uAtClientTimeoutSet(atHandle, 60000);

    // 5) Ativa erros verbosos (AT+CMEE=2)
    uAtClientLock(atHandle);
    uAtClientCommandStart(atHandle, "AT+CMEE=");
    uAtClientWriteInt(atHandle, 2);
    uAtClientCommandStopReadResponse(atHandle);
    errorCode = uAtClientErrorGet(atHandle);
    uAtClientUnlock(atHandle);
    if (errorCode < 0) {
        uPortLog("U_CELL_SMS: erro ao configurar AT+CMEE=2, errorCode = %ld\n", (long) errorCode);
        goto exit;
    }
    uPortLog("U_CELL_SMS: AT+CMEE=2 configurado com sucesso\n");

    // 6) Verifica o SMSC (AT+CSCA?)
    uAtClientLock(atHandle);
    uAtClientCommandStart(atHandle, "AT+CSCA?");
    uAtClientCommandStop(atHandle);
    uAtClientResponseStart(atHandle, "+CSCA:");
    uAtClientReadString(atHandle, scaBuffer, sizeof(scaBuffer), true); // Remove aspas
    uAtClientResponseStop(atHandle);
    errorCode = uAtClientErrorGet(atHandle);
    uAtClientUnlock(atHandle);
    if (errorCode < 0) {
        uPortLog("U_CELL_SMS: erro ao ler SMSC, errorCode = %ld\n", (long) errorCode);
        goto exit;
    }
    uPortLog("U_CELL_SMS: SMSC lido: %s\n", scaBuffer);

    // 7) Formata o comando AT+CMGS="+número"
    snprintf(commandBuffer, sizeof(commandBuffer), "AT+CMGS=\"%s\"", number);
    uPortLog("U_CELL_SMS: enviando comando: %s\n", commandBuffer);

    // 8) Inicia o comando AT+CMGS
    uAtClientLock(atHandle);
    uAtClientCommandStart(atHandle, commandBuffer);
    uAtClientCommandStop(atHandle);

    // 9) Aguarda prompt '>'
    if (uAtClientWaitCharacter(atHandle, '>') < 0) {
        errorCode = (int32_t) U_ERROR_COMMON_TIMEOUT;
        uPortLog("U_CELL_SMS: timeout esperando prompt '>'\n");
        uAtClientUnlock(atHandle);
        goto exit;
    }

    // 10) Envia o texto e o Ctrl-Z
    uAtClientWriteString(atHandle, text, false);
    uAtClientWriteBytes(atHandle, &ctrlZ, 1, true);

    // 11) Lê resposta final
    uAtClientCommandStopReadResponse(atHandle);
    errorCode = uAtClientErrorGet(atHandle);

    // Captura +CMGS: <mr> para confirmar envio
    uAtClientLock(atHandle);
    uAtClientResponseStart(atHandle, "+CMGS:");
    messageReference = uAtClientReadInt(atHandle);
    uAtClientResponseStop(atHandle);
    if (messageReference >= 0) {
        uPortLog("U_CELL_SMS: SMS enviado, Message Reference = %d\n", messageReference);
        errorCode = 0; // Sucesso
    } else {
        // Captura erro detalhado (+CMS ERROR)
        uAtClientResponseStart(atHandle, "+CMS ERROR:");
        int cmsError = uAtClientReadInt(atHandle);
        uAtClientResponseStop(atHandle);
        if (cmsError >= 0) {
            uPortLog("U_CELL_SMS: +CMS ERROR: %d\n", cmsError);
            errorCode = (int32_t) U_ERROR_COMMON_DEVICE_ERROR; // -10
        }
    }

    // Captura resposta bruta para depuração
    uAtClientResponseStart(atHandle, NULL);
    uAtClientReadString(atHandle, responseBuffer, sizeof(responseBuffer), false);
    uAtClientResponseStop(atHandle);
    uAtClientUnlock(atHandle);
    uPortLog("U_CELL_SMS: resposta bruta: %s\n", responseBuffer);
    uPortLog("U_CELL_SMS: resposta AT+CMGS, errorCode = %ld\n", (long) errorCode);

exit:
    uPortMutexUnlock(gUCellPrivateMutex);
    return errorCode;
}

// Lê SMS por índice (stub)
int32_t cellSmsRead(uDeviceHandle_t devHandle,
                    size_t index,
                    char *outNumber, size_t numberMaxLen,
                    char *outText,   size_t textMaxLen)
{
    (void) devHandle;
    (void) index;
    (void) outNumber;
    (void) numberMaxLen;
    (void) outText;
    (void) textMaxLen;
    return (int32_t) U_ERROR_COMMON_NOT_SUPPORTED;
}

// Apaga SMS por índice
int32_t cellSmsDelete(uDeviceHandle_t devHandle,
                      size_t index)
{
    uCellPrivateInstance_t *pInstance;
    uAtClientHandle_t atHandle;
    int32_t errorCode = (int32_t) U_ERROR_COMMON_INVALID_PARAMETER;

    if (devHandle == NULL) {
        return errorCode;
    }

    if (gUCellPrivateMutex != NULL) {
        U_PORT_MUTEX_LOCK(gUCellPrivateMutex);
    }

    // instância e atHandle
    pInstance = pUCellPrivateGetInstance(devHandle);
    if (pInstance == NULL) {
        errorCode = (int32_t) U_ERROR_COMMON_NOT_INITIALISED;
        goto exit;
    }
    atHandle = pInstance->atHandle;

    // AT+CMGD
    uAtClientLock(atHandle);
    uAtClientCommandStart(atHandle, "AT+CMGD=");
    uAtClientWriteInt(atHandle, (int32_t) index);
    uAtClientCommandStopReadResponse(atHandle);
    errorCode = uAtClientErrorGet(atHandle);
    uAtClientUnlock(atHandle);

exit:
    if (gUCellPrivateMutex != NULL) {
        U_PORT_MUTEX_UNLOCK(gUCellPrivateMutex);
    }
    return errorCode;
}
/* u_cell_sms.h
 *
 *  SMS feature for u-blox ubxlib (SARA-R422).
 */

#ifndef U_CELL_SMS_H_
#define U_CELL_SMS_H_

# include "ubxlib.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "u_device.h"    // para uDeviceHandle_t

/**
 * Initialise SMS in text mode and disable URC notifications.
 *
 * @param devHandle   the handle returned by uCellOpen().
 * @return            0 on success or a negative error code.
 */
int32_t cellSmsInit(uDeviceHandle_t devHandle);

/**
 * Send an SMS.
 *
 * @param devHandle   the handle returned by uCellOpen().
 * @param number      null-terminated phone number in international format,
 *                    e.g. "+5511999998888".
 * @param text        null-terminated message body (up to ~160 chars).
 * @return            0 on success or a negative error code.
 */
 
 uAtClientHandle_t getAtHandle(uDeviceHandle_t devHandle);
 
 
int32_t cellSmsSend(uDeviceHandle_t devHandle,
                    const char *number,
                    const char *text);

/**
 * Read an SMS by index (stubbed out).
 *
 * @param devHandle      the handle returned by uCellOpen().
 * @param index          zero-based message index.
 * @param outNumber      buffer in which to put the sender number.
 * @param numberMaxLen   length of outNumber.
 * @param outText        buffer in which to put the message text.
 * @param textMaxLen     length of outText.
 * @return               0 on success (not implemented) or negative error.
 */
int32_t cellSmsRead(uDeviceHandle_t devHandle,
                    size_t index,
                    char *outNumber, size_t numberMaxLen,
                    char *outText,   size_t textMaxLen);

/**
 * Delete an SMS by index.
 *
 * @param devHandle   the handle returned by uCellOpen().
 * @param index       zero-based message index.
 * @return            0 on success or negative error code.
 */
int32_t cellSmsDelete(uDeviceHandle_t devHandle,
                      size_t index);

#endif // U_CELL_SMS_H_

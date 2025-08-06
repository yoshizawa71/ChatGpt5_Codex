#ifndef CONNECTIVITY_H
#define CONNECTIVITY_H
#include "ubxlib.h"
#include "u_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Conecta usando UBX-LIB (cellular_setup)  
 *  @param devHandle  handle retornado por uDeviceOpen()  
 *  @return true se obteve IP, false caso contrário  
 */
bool cell_connect_ubx(uDeviceHandle_t devHandle);

/** @brief Conecta via AT commands (fallback NB-IoT → GPRS)  
 *  @param devHandle  handle retornado por uDeviceOpen()  
 *  @return true se obteve IP, false caso contrário  
 */
bool cell_connect_at(uDeviceHandle_t devHandle);

/** @brief Wrapper: escolhe uma das duas implementações  
 *  @return true se obteve IP, false caso contrário  
 */
bool cell_connect(uDeviceHandle_t devHandle);

#ifdef __cplusplus
}
#endif

#endif // CONNECTIVITY_H

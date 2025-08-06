#ifndef PERIPHERALS_I2C_INCLUDE_I2C_DEV_MASTER_H_
#define PERIPHERALS_I2C_INCLUDE_I2C_DEV_MASTER_H_

#include "esp_err.h"
#include "driver/i2c_master.h"

//------------------------------------------------------------------
//                      I2C Configuration
//------------------------------------------------------------------
#define I2C_MASTER_PORT_0                        0
#define I2C_MASTER_PORT_1                        1
#define I2C_MASTER_SDA                           21
#define I2C_MASTER_SCL                           22

#define I2C_FREQ_HZ                              400000  /*!< I2C master clock frequency */
//#define I2C_FREQ_HZ                              100000  /*!< I2C master clock frequency (reduzido para 100 kHz) */
#define I2C_MASTER_NUM                           I2C_NUM_0  /*!< I2C port number for master dev */

#define SAMPLE_PERIOD_MS                         1000

#define I2C_MASTER_SDA_IO                        I2C_MASTER_SDA  /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_SCL_IO                        I2C_MASTER_SCL  /*!< GPIO number used for I2C master clock */

// Handle global para o barramento I2C
extern i2c_master_bus_handle_t bus_handle;

esp_err_t init_i2c_master(void);

// Função para obter o handle do barramento I2C
i2c_master_bus_handle_t get_i2c_bus_handle(void);

void deinit_i2c_bus(void);

#endif /* PERIPHERALS_I2C_INCLUDE_I2C_DEV_MASTER_H_ */
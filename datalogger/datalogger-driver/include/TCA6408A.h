/*
 * TCA6408A.h
 *
 *  Created on: 5 de ago. de 2024
 *      Author: geopo
 */

#ifndef DATALOGGER_DATALOGGER_DRIVER_INCLUDE_TCA6408A_H_
#define DATALOGGER_DATALOGGER_DRIVER_INCLUDE_TCA6408A_H_

#include "stdint.h"
#include "esp_err.h"
#include "driver/i2c_master.h"


/**
--------------------------------------------
Table Command Byte
-------------------------------------------
|CONTROL REGISTER BITS                 |COMMAND BYTE (HEX)|       REGISTER     |     PROTOCOL    |POWER-UP DEFAULT|
|______________________________________|__________________|____________________|_________________|________________|
| B7 | B6 | B5 | B4 | B3 | B2 | B1 | B0 |                 |                    |                 |                |
|  0 |  0 |  0 |  0 |  0 |  0 |  0 |  0 |        00       | Input Port         |    Read byte    |   xxxx xxxx    |
|  0 |  0 |  0 |  0 |  0 |  0 |  0 |  1 |        01       | Output Port        | Read/write byte |   1111 1111    |
|  0 |  0 |  0 |  0 |  0 |  0 |  1 |  0 |        02       | Polarity Inversion | Read/write byte |   0000 0000    |
|  0 |  0 |  0 |  0 |  0 |  0 |  1 |  1 |        03       | Configuration      | Read/write byte |   1111 1111    |
------------------------------------------------------------------------------------------------------------------
*/

/*Register 0 (Input Port Register)
 * The Input Port Register (register 0) reflects the incoming logic levels of the pins, regardless of whether the pin
is defined as an input or an output by the Configuration Register. They act only on read operation. Writes to this
register have no effect. The default value (X) is determined by the externally applied logic level. Before a read
operation, a write transmission is sent with the command byte to indicate to the I2C device that the Input Port
Register will be accessed next.
 *
 * Register 1 (Output Port Register)
 * The Output Port Register (register 1) shows the outgoing logic levels of the pins defined as outputs by the
Configuration Register. Bit values in this register have no effect on pins defined as inputs. In turn, reads from this
register reflect the value that is in the flip-flop controlling the output selection, not the actual pin value.
 *
 * Register 2 (Polarity Inversion Register)
 * The Polarity Inversion Register (register 2) allows polarity inversion of pins defined as inputs by the
Configuration Register. If a bit in this register is set (written with 1), the polarity of the corresponding port pin is
inverted. If a bit in this register is cleared (written with a 0), the original polarity of the corresponding port pin is
retained.
 *
 * Register 3 (Configuration Register)
 * The Configuration Register (register 3) configures the direction of the I/O pins. If a bit in this register is set to 1,
the corresponding port pin is enabled as an input with a high-impedance output driver. If a bit in this register is
cleared to 0, the corresponding port pin is enabled as an output.
 */

/* The TCA6408 has valid Addresses 0x20 and 0x21 */
#define TCA6408_ADDR1                           0x20
#define TCA6408_ADDR2                           0x21

/* Command bytes */
#define TCA6408_INPUT                           0x00
#define TCA6408_OUTPUT                          0x01
#define TCA6408_POLARITY_INVERSION              0x02
#define TCA6408_CONFIGURATION                   0x03

// Structure to hold register data
typedef struct tca6408 {
    int8_t INPUT;
    int8_t OUTPUT;
    int8_t POLARITY_INVERSION;
    int8_t CONFIGURATION;
} TCA6408_DATA_t;

#define SET_OUTPUT                              0xFF
#define SET_POLARITY_ENABLE_SENSORS             0x98  // 10011000
#define ZERO_ALL_CONFIGURATION                  0x00
#define SET_PORT_OUTPUT                         0x67  // 01100111

enum mosfet {enable_analog_sensors,disable_analog_sensors, enable_sara, disable_sara, enable_interface, enable_pulse_analog, disable_sensors};

esp_err_t init_tca6408a(void);
esp_err_t activate_mosfet(enum mosfet expander_port);
void deinit_tca6408a(void);

#endif /* DATALOGGER_DATALOGGER_DRIVER_INCLUDE_TCA6408A_H_ */
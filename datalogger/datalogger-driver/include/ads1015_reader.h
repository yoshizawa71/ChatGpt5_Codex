#ifdef __cplusplus
extern "C" {
#endif

#ifndef ADS1015_H
#define ADS1015_H

/**------------------------------------------------------------------------------------------------------------------------
The ADS1015-Q1 (ADS101x-Q1) are precision, low-power, 12-bit, I2C-compatible, analog-to-digital converters (ADCs)
offered in VSSOP-10 and UQFN-10 packages. The ADS101x-Q1 incorporate a low-drift voltage reference and an oscillator.
The ADS1014-Q1 and ADS1015-Q1 also incorporate a programmable gain amplifier (PGA) and a digital comparator. These features,
along with a wide operating supply range, are useful for power- and space-constrained, sensor measurement applications.
*/
//-------------------------------------------------------------------------------------------------------------------------

#include <stdio.h>
#include "driver/i2c_master.h"  // Nova API do I2C
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"  // Para TickType_t
#include "freertos/queue.h"     // Para QueueHandle_t
#//include "pressure_meter.h"

#define ADS1015_ADDR_GND 0x48  //!< I2C device address with ADDR pin connected to ground

typedef enum {  // Register addresses
    ADS1015_CONVERSION_REGISTER_ADDR = 0,
    ADS1015_CONFIG_REGISTER_ADDR,
    ADS1015_LO_THRESH_REGISTER_ADDR,
    ADS1015_HI_THRESH_REGISTER_ADDR,
    ADS1015_MAX_REGISTER_ADDR
} ads1015_register_addresses_t;

typedef enum {  // Multiplex options
    ADS1015_MUX_0_1 = 0,
    ADS1015_MUX_0_3,
    ADS1015_MUX_1_3,
    ADS1015_MUX_2_3,
    ADS1015_MUX_0_GND,
    ADS1015_MUX_1_GND,
    ADS1015_MUX_2_GND,
    ADS1015_MUX_3_GND,
} ads1015_mux_t;

typedef enum {  // Full-scale resolution options
    ADS1015_FSR_6_144 = 0,
    ADS1015_FSR_4_096,
    ADS1015_FSR_2_048,
    ADS1015_FSR_1_024,
    ADS1015_FSR_0_512,
    ADS1015_FSR_0_256,
} ads1015_fsr_t;

typedef enum {  // Samples per second
    ADS1015_SPS_128 = 0,   //!< 128 samples per second
    ADS1015_SPS_250,       //!< 250 samples per second
    ADS1015_SPS_490,       //!< 490 samples per second
    ADS1015_SPS_920,       //!< 920 samples per second
    ADS1015_SPS_1600,      //!< 1600 samples per second (default)
    ADS1015_SPS_2400,      //!< 2400 samples per second
    ADS1015_SPS_3300,      //!< 3300 samples per second
    ADS1015_SPS_3300_SPS   //!< 3300 samples per second
} ads1015_sps_t;

typedef enum {
    ADS1015_MODE_CONTINUOUS = 0,
    ADS1015_MODE_SINGLE
} ads1015_mode_t;

typedef enum {
	          analog_1=1, 
	          analog_2,
	          fonte, 
	          bateria}sensor_t;

typedef union {  // Configuration register
    struct {
        uint16_t COMP_QUE:2;  // bits 0..1  Comparator queue and disable
        uint16_t COMP_LAT:1;  // bit  2     Latching Comparator
        uint16_t COMP_POL:1;  // bit  3     Comparator Polarity
        uint16_t COMP_MODE:1; // bit  4     Comparator Mode
        uint16_t DR:3;        // bits 5..7  Data rate
        uint16_t MODE:1;      // bit  8     Device operating mode
        uint16_t PGA:3;       // bits 9..11 Programmable gain amplifier configuration
        uint16_t MUX:3;       // bits 12..14 Input multiplexer configuration
        uint16_t OS:1;        // bit  15    Operational status or single-shot conversion start
    } bit;
    uint16_t reg;
} ADS1015_CONFIG_REGISTER_Type;


typedef struct {
    bool in_use;              // GPIO is used
    gpio_num_t pin;           // Ready pin
    QueueHandle_t gpio_evt_queue;  // Pin triggered queue
} ads1015_rdy_pin_t;

typedef struct {
    ADS1015_CONFIG_REGISTER_Type config;
    i2c_master_dev_handle_t dev_handle;  // Handle para o dispositivo I2C (nova API)
    int address;                         // Endereço I2C do dispositivo
    ads1015_rdy_pin_t rdy_pin;
    ads1015_register_addresses_t last_reg;  // Último registrador acessado
    bool changed;                           // Indica se a configuração foi alterada
    TickType_t max_ticks;                   // Tempo máximo de espera para operações I2C
} ads1015_t;

// Funções
void adc_init(int adc_pin);
void adc_del_init(void);

//float oneshot_analog_read(enum sensor tipo);
float oneshot_analog_read(sensor_t tipo);
void voltage_calibrated(int adc_channel, int *voltage);

// Inicializar dispositivo
ads1015_t ads1015_config(uint8_t address);  // Configuração do dispositivo

// Configurar dispositivo
void ads1015_set_rdy_pin(ads1015_t* ads, gpio_num_t gpio);  // Configurar pino de ready
void ads1015_set_mux(ads1015_t* ads, ads1015_mux_t mux);    // Configurar multiplexador
void ads1015_set_pga(ads1015_t* ads, ads1015_fsr_t fsr);    // Configurar FSR
void ads1015_set_mode(ads1015_t* ads, ads1015_mode_t mode); // Configurar modo
void ads1015_set_sps(ads1015_t* ads, ads1015_sps_t sps);    // Configurar taxa de amostragem
void ads1015_set_max_ticks(ads1015_t* ads, TickType_t max_ticks);  // Configurar tempo máximo de espera

int16_t ads1015_get_raw(ads1015_t* ads);    // Obter valor bruto
double ads1015_get_voltage(ads1015_t* ads);  // Obter tensão em volts

void ads1015_deinit(ads1015_t* ads);  // Desinicializar dispositivo

#endif // ADS1015_H

#ifdef __cplusplus
}
#endif
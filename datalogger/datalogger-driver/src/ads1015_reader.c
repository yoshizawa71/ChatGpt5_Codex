#include "ads1015_reader.h"

#include "i2c_dev_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#define GPIO_PIN_INTR_NEGEDGE 2

#define MOVING_AVG_WINDOW 10

static float raw_buffer_ch1[MOVING_AVG_WINDOW] = {0};
static float raw_buffer_ch2[MOVING_AVG_WINDOW] = {0};
static int buffer_idx_ch1 = 0, buffer_count_ch1 = 0;
static int buffer_idx_ch2 = 0, buffer_count_ch2 = 0;

static const char* TAG = "ads1015";

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    const bool ret = 1;  // Dummy value to pass to queue
    QueueHandle_t gpio_evt_queue = (QueueHandle_t)arg;  // Find which queue to write
    xQueueSendFromISR(gpio_evt_queue, &ret, NULL);
}

static esp_err_t ads1015_write_register(ads1015_t* ads, ads1015_register_addresses_t reg, uint16_t data) {
    uint8_t buffer[3];
    buffer[0] = reg;              // Endereço do registrador
    buffer[1] = data >> 8;        // 8 bits mais significativos
    buffer[2] = data & 0xFF;      // 8 bits menos significativos

    esp_err_t ret = i2c_master_transmit(ads->dev_handle, buffer, 3, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao escrever no registrador 0x%02x: %s", reg, esp_err_to_name(ret));
        return ret;
    }

    ads->last_reg = reg;  // Atualiza o último registrador acessado
    return ESP_OK;
}

static esp_err_t ads1015_read_register(ads1015_t* ads, ads1015_register_addresses_t reg, uint8_t* data, uint8_t len) {
    if (ads->last_reg != reg) {  // Se o registrador atual não é o desejado, seleciona-o
        uint8_t reg_addr = (uint8_t)reg;  // Converte para uint8_t
        esp_err_t ret = i2c_master_transmit(ads->dev_handle, &reg_addr, 1, pdMS_TO_TICKS(1000));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Falha ao selecionar registrador 0x%02x: %s", reg, esp_err_to_name(ret));
            return ret;
        }
        ads->last_reg = reg;
    }

    // Lê os dados do registrador
    uint8_t reg_addr = (uint8_t)reg;  // Converte para uint8_t
    esp_err_t ret = i2c_master_transmit_receive(ads->dev_handle, &reg_addr, 1, data, len, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao ler registrador 0x%02x: %s", reg, esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

static float apply_moving_average(float new_sample, float *buffer, int *idx, int *count) {
    buffer[*idx] = new_sample;
    *idx = (*idx + 1) % MOVING_AVG_WINDOW;
    if (*count < MOVING_AVG_WINDOW) (*count)++;

    float sum = 0;
    for (int i = 0; i < *count; i++) {
        sum += buffer[i];
    }
    return sum / *count;
}

// Inicializa o dispositivo ADS1015
ads1015_t ads1015_config(uint8_t address) {
    ads1015_t ads = {0};  // Inicializa com zeros

    // Configuração padrão
    ads.config.bit.OS = 1;  // Sempre inicia conversão
    ads.config.bit.MUX = ADS1015_MUX_3_GND;
    ads.config.bit.PGA = ADS1015_FSR_4_096;
    ads.config.bit.MODE = ADS1015_MODE_SINGLE;
    ads.config.bit.DR = ADS1015_SPS_1600;
    ads.config.bit.COMP_MODE = 0;
    ads.config.bit.COMP_POL = 0;
    ads.config.bit.COMP_LAT = 0;
    ads.config.bit.COMP_QUE = 0b11;

    ads.address = address;  // Salva o endereço I2C
    ads.rdy_pin.in_use = 0;  // Indica que o pino de ready não está em uso
    ads.last_reg = ADS1015_MAX_REGISTER_ADDR;  // Último registrador inválido
    ads.changed = 1;  // Indica que a configuração foi alterada
    ads.max_ticks = 10 / portTICK_PERIOD_MS;

    // Adiciona o dispositivo ao barramento I2C
    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "Barramento I2C não inicializado. Chame init_i2c_master() primeiro.");
        return ads;  // Retorna com dev_handle nulo
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = I2C_FREQ_HZ,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &ads.dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao adicionar ADS1015 ao barramento I2C: %s", esp_err_to_name(ret));
        return ads;  // Retorna com dev_handle nulo
    }

    ESP_LOGI(TAG, "ADS1015 inicializado com sucesso no endereço 0x%02x", address);
    return ads;
}

void ads1015_set_mux(ads1015_t* ads, ads1015_mux_t mux) {
    ads->config.bit.MUX = mux;
    ads->changed = 1;
}

void ads1015_set_rdy_pin(ads1015_t* ads, gpio_num_t gpio) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,  // Borda de descida
        .pin_bit_mask = (1ULL << gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);  // Configura o GPIO

    ads->rdy_pin.gpio_evt_queue = xQueueCreate(1, sizeof(bool));
    gpio_install_isr_service(0);

    ads->rdy_pin.in_use = 1;
    ads->rdy_pin.pin = gpio;
    ads->config.bit.COMP_QUE = 0b00;  // Assert após uma conversão
    ads->changed = 1;

    esp_err_t err = ads1015_write_register(ads, ADS1015_LO_THRESH_REGISTER_ADDR, 0);  // Limite inferior mínimo
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Não foi possível definir limite inferior: %s", esp_err_to_name(err));
    }
    err = ads1015_write_register(ads, ADS1015_HI_THRESH_REGISTER_ADDR, 0xFFFF);  // Limite superior máximo
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Não foi possível definir limite superior: %s", esp_err_to_name(err));
    }
}

void ads1015_set_pga(ads1015_t* ads, ads1015_fsr_t fsr) {
    ads->config.bit.PGA = fsr;
    ads->changed = 1;
}

void ads1015_set_mode(ads1015_t* ads, ads1015_mode_t mode) {
    ads->config.bit.MODE = mode;
    ads->changed = 1;
}

void ads1015_set_sps(ads1015_t* ads, ads1015_sps_t sps) {
    ads->config.bit.DR = sps;
    ads->changed = 1;
}

void ads1015_set_max_ticks(ads1015_t* ads, TickType_t max_ticks) {
    ads->max_ticks = max_ticks;
}

int16_t ads1015_get_raw(ads1015_t* ads) {
    const static uint16_t sps[] = {128, 250, 490, 920, 1600, 2400, 3300, 3300};
    const static uint8_t len = 2;
    uint8_t data[2];
    esp_err_t err;
    bool tmp;  // Variável temporária para leitura da fila

    if (ads->rdy_pin.in_use) {
        gpio_isr_handler_add(ads->rdy_pin.pin, gpio_isr_handler, (void*)ads->rdy_pin.gpio_evt_queue);
        xQueueReset(ads->rdy_pin.gpio_evt_queue);
    }

    // Verifica se é necessário enviar dados de configuração
    if ((ads->config.bit.MODE == ADS1015_MODE_SINGLE) || (ads->changed)) {
        err = ads1015_write_register(ads, ADS1015_CONFIG_REGISTER_ADDR, ads->config.reg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Não foi possível escrever no dispositivo: %s", esp_err_to_name(err));
            if (ads->rdy_pin.in_use) {
                gpio_isr_handler_remove(ads->rdy_pin.pin);
                xQueueReset(ads->rdy_pin.gpio_evt_queue);
            }
            return 0;  // Retorna 0 em caso de erro
        }
        ads->changed = 0;  // Indica que os dados não foram alterados
    }

    if (ads->rdy_pin.in_use) {
        xQueueReceive(ads->rdy_pin.gpio_evt_queue, &tmp, portMAX_DELAY);
        gpio_isr_handler_remove(ads->rdy_pin.pin);
    } else {
        // Aguarda 1 ms a mais que a taxa de amostragem, com margem para arredondamento
        vTaskDelay((((1000 / sps[ads->config.bit.DR]) + 1) / portTICK_PERIOD_MS) + 1);
    }

    err = ads1015_read_register(ads, ADS1015_CONVERSION_REGISTER_ADDR, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Não foi possível ler do dispositivo: %s", esp_err_to_name(err));
        return 0;  // Retorna 0 em caso de erro
    }

    return ((uint16_t)data[0] << 8) | (uint16_t)data[1];  // Retorna o valor bruto
}

double ads1015_get_voltage(ads1015_t* ads) {
    const double fsr[] = {6.144, 4.096, 2.048, 1.024, 0.512, 0.256};
    const int16_t bits = (1L << 15) - 1;
    int16_t raw;

    raw = ads1015_get_raw(ads);
    return (double)raw * fsr[ads->config.bit.PGA] / (double)bits;
}

float oneshot_analog_read(sensor_t tipo) {
    ads1015_t adc;
    int16_t get_raw;
    float voltage = 0;

    switch (tipo) {
        case analog_1:
            adc = ads1015_config(ADS1015_ADDR_GND);
            ads1015_set_mux(&adc, ADS1015_MUX_3_GND);
            get_raw = ads1015_get_raw(&adc);
//            voltage = ads1015_get_voltage(&adc);
//média móvel
            voltage = apply_moving_average(ads1015_get_voltage(&adc), raw_buffer_ch1, &buffer_idx_ch1, &buffer_count_ch1);
            printf("###### Analog 1 #####\n");
            printf("Raw ADC value: %d voltage: %.04f volts\n", get_raw, voltage);
            ads1015_deinit(&adc);
            break;

        case analog_2:
            adc = ads1015_config(ADS1015_ADDR_GND);
            ads1015_set_mux(&adc, ADS1015_MUX_2_GND);
            get_raw = ads1015_get_raw(&adc);
//            voltage = ads1015_get_voltage(&adc);
//média móvel
            voltage = apply_moving_average(ads1015_get_voltage(&adc), raw_buffer_ch2, &buffer_idx_ch2, &buffer_count_ch2);
            printf("###### Analog 2 #####\n");
            printf("Raw ADC value: %d voltage: %.04f volts\n", get_raw, voltage);
            ads1015_deinit(&adc);
            break;

        case fonte:
            adc = ads1015_config(ADS1015_ADDR_GND);
            ads1015_set_mux(&adc, ADS1015_MUX_1_GND);
            get_raw = ads1015_get_raw(&adc);
            voltage = ads1015_get_voltage(&adc);
            printf("###### Analog 3 #####\n");
            printf("Raw ADC value: %d voltage: %.04f volts\n", get_raw, voltage);
            ads1015_deinit(&adc);
            break;

        case bateria:
            adc = ads1015_config(ADS1015_ADDR_GND);
            ads1015_set_mux(&adc, ADS1015_MUX_0_GND);
            get_raw = ads1015_get_raw(&adc);
            voltage = ads1015_get_voltage(&adc);
            printf("###### Analog 4 #####\n");
            printf("Raw ADC value: %d voltage: %.04f volts\n", get_raw, voltage);
            ads1015_deinit(&adc);
            break;

        default:
            printf("Sensor inexistente!\n");
            break;
    }
    return voltage;
}

void ads1015_deinit(ads1015_t* ads) {
    if (ads->dev_handle) {
        i2c_master_bus_rm_device(ads->dev_handle);
        ads->dev_handle = NULL;
        ESP_LOGI(TAG, "ADS1015 removido do barramento I2C");
    }
    if (ads->rdy_pin.in_use && ads->rdy_pin.gpio_evt_queue) {
        vQueueDelete(ads->rdy_pin.gpio_evt_queue);
        ads->rdy_pin.gpio_evt_queue = NULL;
        ads->rdy_pin.in_use = 0;
    }
}

// Funções não implementadas (placeholders)
void adc_init(int adc_pin) {
    // Implementar se necessário
}

void adc_del_init(void) {
    // Implementar se necessário
}

void voltage_calibrated(int adc_channel, int *voltage) {
    // Implementar se necessário
}

// Função para testar a comunicação com o ADS1015
/*
void test_ads1015(void) {
    esp_err_t ret;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x48,  // Endereço do ADS1015, conforme detectado pelo i2cdetect
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev_handle;

    ESP_LOGI(TAG, "Testando comunicação com o ADS1015 (endereço 0x48)...");

    // Adiciona o dispositivo ao barramento
    ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao adicionar ADS1015 ao barramento I2C: %s", esp_err_to_name(ret));
        return;
    }

    // Tenta realizar uma transação I2C (escrita de um byte dummy)
    uint8_t dummy_data = 0x00;
    ret = i2c_master_transmit(dev_handle, &dummy_data, 1, 50);  // Timeout de 50ms
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ADS1015 respondeu com sucesso!");
    } else {
        ESP_LOGE(TAG, "Erro ao se comunicar com o ADS1015: %s", esp_err_to_name(ret));
    }

    // Remove o dispositivo do barramento
    i2c_master_bus_rm_device(dev_handle);
}
*/
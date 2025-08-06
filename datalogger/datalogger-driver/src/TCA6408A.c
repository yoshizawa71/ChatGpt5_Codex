#include "TCA6408A.h"
#include "i2c_dev_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "tca6408a";
static i2c_master_dev_handle_t tca6408a_handle = NULL;  // Handle do dispositivo TCA6408A
static TCA6408_DATA_t tca6408_data = {0};  // Estrutura para armazenar os dados dos registradores

// Função interna para escrever em um registrador do TCA6408A
static esp_err_t tca6408a_write_reg(uint8_t reg, uint8_t data) {
    esp_err_t ret;
    uint8_t buffer[2] = {reg, data};

    // Verifica se o handle do dispositivo foi inicializado
    if (tca6408a_handle == NULL) {
        ESP_LOGE(TAG, "Handle do TCA6408A não inicializado");
        return ESP_ERR_INVALID_STATE;
    }

    // Escreve no registrador
    ESP_LOGI(TAG, "Escrevendo no registrador 0x%02x: 0x%02x", reg, data);
    ret = i2c_master_transmit(tca6408a_handle, buffer, 2, 50);  // Timeout de 50ms
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao escrever no registrador 0x%02x: %s", reg, esp_err_to_name(ret));
        return ret;
    }

    // Atualiza a estrutura com o valor escrito
    switch (reg) {
        case TCA6408_INPUT:
            tca6408_data.INPUT = data;
            break;
        case TCA6408_OUTPUT:
            tca6408_data.OUTPUT = data;
            break;
        case TCA6408_POLARITY_INVERSION:
            tca6408_data.POLARITY_INVERSION = data;
            break;
        case TCA6408_CONFIGURATION:
            tca6408_data.CONFIGURATION = data;
            break;
        default:
            ESP_LOGE(TAG, "Registrador inválido: 0x%02x", reg);
            return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

// Função interna para ler um registrador do TCA6408A
static esp_err_t tca6408a_read_reg(uint8_t reg, uint8_t* data) {
    esp_err_t ret;

    // Verifica se o handle do dispositivo foi inicializado
    if (tca6408a_handle == NULL) {
        ESP_LOGE(TAG, "Handle do TCA6408A não inicializado");
        return ESP_ERR_INVALID_STATE;
    }

    // Escreve o registrador a ser lido
    ESP_LOGI(TAG, "Lendo o registrador 0x%02x", reg);
    ret = i2c_master_transmit_receive(tca6408a_handle, &reg, 1, data, 1, 5000);  // Timeout de 50ms
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao ler registrador 0x%02x: %s", reg, esp_err_to_name(ret));
        return ret;
    }

    // Atualiza a estrutura com o valor lido
    switch (reg) {
        case TCA6408_INPUT:
            tca6408_data.INPUT = *data;
            break;
        case TCA6408_OUTPUT:
            tca6408_data.OUTPUT = *data;
            break;
        case TCA6408_POLARITY_INVERSION:
            tca6408_data.POLARITY_INVERSION = *data;
            break;
        case TCA6408_CONFIGURATION:
            tca6408_data.CONFIGURATION = *data;
            break;
        default:
            ESP_LOGE(TAG, "Registrador inválido: 0x%02x", reg);
            return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Registrador 0x%02x lido: 0x%02x", reg, *data);
    return ESP_OK;
}

esp_err_t init_tca6408a(void) {
    esp_err_t ret;

    // Verifica se o barramento I2C foi inicializado
    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "Barramento I2C não inicializado. Chame init_i2c_master() primeiro.");
        return ESP_ERR_INVALID_STATE;
    }

    // Reinicia o barramento I2C para garantir um estado limpo
    ESP_LOGI(TAG, "Reiniciando o barramento I2C do TCA6408A");
    i2c_master_bus_reset(bus_handle);

    // Configuração do dispositivo TCA6408A
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TCA6408_ADDR2,  // Usa o endereço 0x21, conforme detectado pelo i2cdetect
 //       .scl_speed_hz = I2C_FREQ_HZ,      // Frequência definida no i2c_dev_master.h
        .scl_speed_hz = 50000,            // Reduzir para 50 kHz para estabilidade a 80 MHz
    };

    // Adiciona o dispositivo ao barramento I2C
    ESP_LOGI(TAG, "Adicionando TCA6408A ao barramento I2C...");
    ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &tca6408a_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao adicionar TCA6408A ao barramento I2C: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "TCA6408A adicionado com sucesso. Handle: %p", tca6408a_handle);

    // Inicializa os registradores do TCA6408A
    ESP_LOGI(TAG, "Configurando registrador OUTPUT...");
    ret = tca6408a_write_reg(TCA6408_OUTPUT, SET_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao configurar o registrador OUTPUT");
        return ret;
    }

    ESP_LOGI(TAG, "Configurando registrador POLARITY_INVERSION...");
    ret = tca6408a_write_reg(TCA6408_POLARITY_INVERSION, ZERO_ALL_CONFIGURATION);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao configurar o registrador POLARITY_INVERSION");
        return ret;
    }

    ESP_LOGI(TAG, "Configurando registrador CONFIGURATION...");
    ret = tca6408a_write_reg(TCA6408_CONFIGURATION, ZERO_ALL_CONFIGURATION);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao configurar o registrador CONFIGURATION");
        return ret;
    }

    ESP_LOGI(TAG, "TCA6408A inicializado com sucesso no endereço 0x%02x", TCA6408_ADDR2);
    return ESP_OK;
}

static esp_err_t tca6408a_set_direction_output(uint8_t *config_reg_val, uint8_t bit)
{
    // direção: 0 = output, 1 = input. Para tornar output, limpa o bit.
    *config_reg_val &= ~(1 << bit);
    return tca6408a_write_reg(TCA6408_CONFIGURATION, *config_reg_val);
}

static esp_err_t tca6408a_set_output_level(uint8_t *output_reg_val, uint8_t bit, bool level_high)
{
    if (level_high) {
        *output_reg_val |= (1 << bit);  // nível alto
    } else {
        *output_reg_val &= ~(1 << bit); // nível baixo
    }
    return tca6408a_write_reg(TCA6408_OUTPUT, *output_reg_val);
}

esp_err_t activate_mosfet(enum mosfet expander_port) {
    esp_err_t ret;
    uint8_t config_val;
    uint8_t output_val;

    // Lê os registradores atuais (para fazer read-modify-write)
    if ((ret = tca6408a_read_reg(TCA6408_CONFIGURATION, &config_val)) != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao ler CONFIGURATION");
        return ret;
    }
    if ((ret = tca6408a_read_reg(TCA6408_OUTPUT, &output_val)) != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao ler OUTPUT");
        return ret;
    }

    switch (expander_port) {
        case enable_analog_sensors:
            ESP_LOGI(TAG, "+++ Ativando sensores analógicos +++");
            // P3: output, nível baixo (ativo)
            ret = tca6408a_set_direction_output(&config_val, 3);
            if (ret != ESP_OK) return ret;
            ret = tca6408a_set_output_level(&output_val, 3, false); // low = ativo
            break;

        case disable_analog_sensors:
            ESP_LOGI(TAG, "+++ Desativando sensores analógicos +++");
            // P3: output, nível alto (inativo)
            ret = tca6408a_set_direction_output(&config_val, 3);
            if (ret != ESP_OK) return ret;
            ret = tca6408a_set_output_level(&output_val, 3, true); // high = inativo
            break;

        case enable_sara:
            ESP_LOGI(TAG, "+++ Ativando SARA +++");
            // P4: output, nível baixo (ativo)
            ret = tca6408a_set_direction_output(&config_val, 4);
            if (ret != ESP_OK) return ret;
            ret = tca6408a_set_output_level(&output_val, 4, false); // low = ativo
            break;

        case disable_sara:
            ESP_LOGI(TAG, "+++ Desativando SARA +++");
            // P4: output, nível alto (inativo)
            ret = tca6408a_set_direction_output(&config_val, 4);
            if (ret != ESP_OK) return ret;
            ret = tca6408a_set_output_level(&output_val, 4, true); // high = inativo
            break;

        case enable_interface:
            ESP_LOGI(TAG, "+++ Ativando interface +++");
            // assume bit 7 (0x80) é interface como antes; mantém os outros bits
            // direção: output
            ret = tca6408a_set_direction_output(&config_val, 7);
            if (ret != ESP_OK) return ret;
            // nível alto como era no seu código original (output_val = 128)
            ret = tca6408a_set_output_level(&output_val, 7, true);
            break;

        case enable_pulse_analog:
            ESP_LOGI(TAG, "+++ Ativando pulse analog +++");
            // você usava 24 (0x18) antes: bits 3 e 4 em nível alto
            // torna ambos output e coloca high
            ret = tca6408a_set_direction_output(&config_val, 3);
            if (ret != ESP_OK) return ret;
            ret = tca6408a_set_output_level(&output_val, 3, true);
            if (ret != ESP_OK) return ret;
            ret = tca6408a_set_direction_output(&config_val, 4);
            if (ret != ESP_OK) return ret;
            ret = tca6408a_set_output_level(&output_val, 4, true);
            break;

        case disable_sensors:
            ESP_LOGI(TAG, "+++ Desativando todos os sensores +++");
            // Aqui faz reset completo como você tinha antes:
            // todos como output (config = 0) e todos os outputs em alto (inativos)
            ret = tca6408a_write_reg(TCA6408_CONFIGURATION, ZERO_ALL_CONFIGURATION);
            if (ret != ESP_OK) return ret;
            ret = tca6408a_write_reg(TCA6408_OUTPUT, SET_OUTPUT);
            // Atualiza os valores locais para manter coerência no log abaixo
            if (ret == ESP_OK) {
                config_val = ZERO_ALL_CONFIGURATION;
                output_val = SET_OUTPUT;
            }
            break;

        default:
            ESP_LOGE(TAG, "Modo MOSFET inválido: %d", expander_port);
            return ESP_ERR_INVALID_ARG;
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao aplicar configuração do MOSFET: %s", esp_err_to_name(ret));
        return ret;
    }

    // Se não foi um caso que já escreveu direto os registradores, atualiza os dois (read-modify-write)
    if (expander_port != disable_sensors) {
        // atualiza CONFIGURATION e OUTPUT com os valores modificados
        if ((ret = tca6408a_write_reg(TCA6408_CONFIGURATION, config_val)) != ESP_OK) {
            return ret;
        }
        if ((ret = tca6408a_write_reg(TCA6408_OUTPUT, output_val)) != ESP_OK) {
            return ret;
        }
    }

    ESP_LOGI(TAG, "MOSFET configurado: modo %d, configuration=0x%02x, output=0x%02x",
             expander_port, config_val, output_val);
    return ESP_OK;
}


void deinit_tca6408a(void) {
    if (tca6408a_handle != NULL) {
        i2c_master_bus_rm_device(tca6408a_handle);
        tca6408a_handle = NULL;
        ESP_LOGI(TAG, "TCA6408A removido do barramento I2C");
    }
}
#include "i2c_dev_master.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h" // Para esp_rom_delay_us

static const char* TAG = "i2c_dev_master";

// Handle global do barramento I2C
i2c_master_bus_handle_t bus_handle = NULL;

static void reset_i2c_bus(void) {
    ESP_LOGI(TAG, "Tentando resetar o barramento I2C manualmente...");
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << I2C_MASTER_SDA_IO) | (1ULL << I2C_MASTER_SCL_IO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    for (int i = 0; i < 9; i++) {
        gpio_set_level(I2C_MASTER_SCL_IO, 0);
        esp_rom_delay_us(5); // SubstituÃ­do ets_delay_us por esp_rom_delay_us
        gpio_set_level(I2C_MASTER_SCL_IO, 1);
        esp_rom_delay_us(5);
    }
    
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    int sda_level = gpio_get_level(I2C_MASTER_SDA_IO);
    int scl_level = gpio_get_level(I2C_MASTER_SCL_IO);
    ESP_LOGI(TAG, "ApÃ³s reset: SDA nÃ­vel: %d, SCL nÃ­vel: %d", sda_level, scl_level);
}

esp_err_t init_i2c_master(void) {
    esp_err_t ret;
    
    // Verifica se o barramento já foi inicializado
    if (bus_handle != NULL) {
        ESP_LOGW(TAG, "Barramento I2C já foi inicializado. Ignorando nova inicialização.");
        return ESP_OK;
    }
    
    // ------------------------------------------------------------
    //  CONFIGURAÇÃO OPEN‑DRAIN + PULL‑UP INTERNO PARA SDA/SCL
    // ------------------------------------------------------------
    gpio_config_t gpio_conf = {
        .pin_bit_mask   = (1ULL << I2C_MASTER_SDA_IO) | (1ULL << I2C_MASTER_SCL_IO),
        .mode           = GPIO_MODE_INPUT_OUTPUT_OD,  // open‑drain
        .pull_up_en     = GPIO_PULLUP_ENABLE,         // pull‑up interno
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,
        .intr_type      = GPIO_INTR_DISABLE
    };
    ESP_LOGI(TAG, "Configurando GPIO I2C em open‑drain com pull‑up interno...");
    gpio_config(&gpio_conf);
   

reset_i2c_bus();

    // Configuração do barramento I2C
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,        // Porto I2C definido no .h
        .sda_io_num = I2C_MASTER_SDA_IO,   // Pino SDA definido no .h
        .scl_io_num = I2C_MASTER_SCL_IO,   // Pino SCL definido no .h
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,  // Reabilita pull-ups internos
    };

    // Inicializa o barramento I2C
    ret = i2c_new_master_bus(&bus_config, &bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar o barramento I2C: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C inicializado com sucesso no porto %d", I2C_MASTER_NUM);
    // ------------------------------------------------------------
    //  DESABILITAR PULL‑UPS INTERNOS (economiza ~0.1 mA)
    // ------------------------------------------------------------
    gpio_set_pull_mode(I2C_MASTER_SDA_IO, GPIO_FLOATING);
    gpio_set_pull_mode(I2C_MASTER_SCL_IO, GPIO_FLOATING);
    ESP_LOGI(TAG, "Pull‑ups internos do I2C desabilitados.");
    
    return ESP_OK;
}

i2c_master_bus_handle_t get_i2c_bus_handle(void) {
    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "Barramento I2C não inicializado. Chame init_i2c_master() primeiro.");
    }
    return bus_handle;
}

void deinit_i2c_bus(void) {
    if (bus_handle != NULL) {
        i2c_del_master_bus(bus_handle);
        bus_handle = NULL;
        ESP_LOGI(TAG, "Barramento I2C desinicializado");
    }
}
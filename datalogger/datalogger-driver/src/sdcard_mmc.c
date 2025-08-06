#include "datalogger_control.h"
#include <stdint.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"

#include "datalogger_driver.h"
#include "sdmmc_driver.h"
#include "pulse_meter.h"
#include "pressure_meter.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_littlefs.h"

#define MOUNT_POINT "/sdcard"

static SemaphoreHandle_t sdMutex=NULL;

static const char *TAG = "SDMMC";

#define PLUVIOMETER 0
#define pluv_channel 1

#define RECORD_FILE_HEADER_SIZE  39
#define RECORD_FILE_DATA_SIZE 43

/*#define RECORD_FILE_HEADER_PLUV_SIZE  28
#define RECORD_FILE_DATA_PLUV_SIZE 31

#define RECORD_FILE_HEADER_FLOW_SIZE 39 
#define RECORD_FILE_DATA_FLOW_SIZE 41

#define RECORD_FILE_HEADER_PRESSURE_SIZE  46
#define RECORD_FILE_DATA_PRESSURE_SIZE 49*/

static sdmmc_card_t *card;

static uint32_t current_index = 0;

bool no_register = false;

const char *record_file = MOUNT_POINT"/registro.csv";
/*const char *record_file_pulse = MOUNT_POINT"/pulsos.csv";
const char *record_file_pressure = MOUNT_POINT"/pressao.csv";*/

#define CONFIG_PIN_CLK 14
#define CONFIG_PIN_CMD 15
#define CONFIG_PIN_D0  2
#define CONFIG_PIN_D1  4
#define CONFIG_PIN_D2  12
#define CONFIG_PIN_D3  13

const char* names[] = {"CLK", "CMD", "D0", "D1", "D2", "D3"};
const int pins[] = {CONFIG_PIN_CLK,
                    CONFIG_PIN_CMD,
                    CONFIG_PIN_D0,
                    CONFIG_PIN_D1,
                    CONFIG_PIN_D2,
                    CONFIG_PIN_D3
                    };

const int pin_count = sizeof(pins)/sizeof(pins[0]);

static void check_if_file_exists(const char* file);


void unmount_sd_card(void)
{
    const char mount_point[] = MOUNT_POINT;
    esp_vfs_fat_sdcard_unmount(mount_point, card);
    ESP_LOGI(TAG, "Card unmounted");
    sdmmc_host_deinit();
     
}

esp_err_t mount_sd_card(void) {
    if (sdMutex == NULL) {
        sdMutex = xSemaphoreCreateMutex();
        if (sdMutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_ERR_NO_MEM;
        }
    }
  //  xSemaphoreTake(sdMutex, portMAX_DELAY);
    esp_err_t ret;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    const char mount_point[] = MOUNT_POINT;
    ESP_LOGI(TAG, "Initializing SD card");

    // Reinicializar pinos explicitamente para compatibilidade com deep sleep
    for (int i = 0; i < pin_count; i++) {
        ESP_LOGI(TAG, "Resetting pin %s (GPIO%d)", names[i], pins[i]);
        gpio_reset_pin(pins[i]);
        ESP_ERROR_CHECK(gpio_set_direction(pins[i], GPIO_MODE_INPUT));
        ESP_ERROR_CHECK(gpio_pullup_dis(pins[i]));
        ESP_ERROR_CHECK(gpio_pulldown_dis(pins[i]));
        // Logar estado do pino após configuração
        ESP_LOGI(TAG, "Pin %s (GPIO%d) state: Level=%d", names[i], pins[i], gpio_get_level(pins[i]));
    }

    // Atraso para estabilizar os pinos
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "Using SDMMC peripheral");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

#if CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_INTERNAL_IO
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_IO_ID,
    };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;

    ESP_LOGI(TAG, "Configuring LDO power control");
    ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create a new on-chip LDO power control driver: %s", esp_err_to_name(ret));
        xSemaphoreGive(sdMutex);
        return ret;
    }
    host.pwr_ctrl_handle = pwr_ctrl_handle;
#endif

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

    ESP_LOGI(TAG, "Return of SDMMC -----------> %d", ret);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
   //     xSemaphoreGive(sdMutex);
        return ret;
    }
    ESP_LOGI(TAG, "Filesystem mounted");

    sdmmc_card_print_info(stdout, card);
    
    check_if_file_exists(record_file);
    index_config_init();
    
 //   xSemaphoreGive(sdMutex);
    return ret;
}

//static void save_default_record_pulse_config(void)
void save_default_record_idx_config(void)
{
	xSemaphoreTake(sdMutex,portMAX_DELAY);
    struct record_index_config idx_config = {0};

    idx_config.last_write_idx = UNSPECIFIC_RECORD;
    idx_config.last_read_idx = UNSPECIFIC_RECORD;
    idx_config.total_idx = 0;
    idx_config.cursor_position = 0;

    save_index_config(&idx_config);
    xSemaphoreGive(sdMutex);
}

void index_config_init(void)
{
    struct record_index_config idx_config = {0};
    
    if (!has_record_index_config())
    {
        save_default_record_idx_config();
    }

    get_index_config(&idx_config);
    current_index = idx_config.total_idx;
}

static bool has_enough_size(void)
{
    FILE *f = fopen(record_file,"r");

    if (f==NULL)
      {
    	printf("Falha ao abrir arquivo para verificar tamanho\n");
    	return false;
      }

    if(fseek(f, 0L, SEEK_END)<0)
    {
	 ESP_LOGE(TAG, "Falha ao buscar o final do arquivo");
     fclose(f);
     return false;
    }

    int64_t size = ftell(f);
    if (size < 0) {
        ESP_LOGE(TAG, "Falha ao obter tamanho do arquivo");
        fclose(f);
        return false;
    }
    rewind(f);
    fclose(f);
//    return size < 1024*1024*1024;
    return size < 15LL*1024*1024*1024;  // 15 GB, calculado em 64 bits
}

static void check_if_file_exists(const char* file)
{
    struct stat st;

    // Só faz algo se o arquivo NÃO existir
    if (stat(file, &st) != 0) {
        // Protege toda operação de SDMMC com mutex
        if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
            FILE *f = fopen(file, "w");
            if (f != NULL) {
			// Alinhe o cabeçalho com as mesmas larguras dos dados
                fprintf(f,"    DATA    |   HORA   | CANAL |  DADOS\n");
                no_register = true;
                fclose(f);
            } else {
                ESP_LOGE(TAG, "Falha ao criar arquivo '%s'", file);
            }
            xSemaphoreGive(sdMutex);
        }
    }
    // Se o arquivo já existir, não precisa fazer nada
}

esp_err_t has_SD_FILE_Created(void)
{
    check_if_file_exists(record_file);

    if (no_register)
    {
     no_register =false;
     return ESP_OK;
    }

   return ESP_FAIL;
}

bool has_measurement_to_send(void)
{
	xSemaphoreTake(sdMutex,portMAX_DELAY);
    bool ret = false;
    struct record_index_config idx_config = {0};
    
    get_index_config(&idx_config);

    if(idx_config.total_idx > 0)
    {
        if(idx_config.last_write_idx == UNSPECIFIC_RECORD)
        {
            if (idx_config.last_read_idx != (idx_config.total_idx - 1) ){
                ret = true;
            }
        }
        else{
            if (idx_config.last_write_idx != idx_config.last_read_idx) {
                ret = true;
            }
        }
    }
 xSemaphoreGive(sdMutex);
    return ret;
}


//=======================================================
// SAVE data to Flash with channel
//=======================================================
esp_err_t save_record_sd(int channel, char *data)
{
esp_err_t ret = ESP_OK;
 uint32_t write_idx = UNSPECIFIC_RECORD;
	
struct record_index_config idx_config = {0};
  FILE *f=NULL;
    // Protect critical section
    xSemaphoreTake(sdMutex, portMAX_DELAY);

    ESP_LOGI(TAG, "Obtendo configuração de índice...");
   ret = get_index_config(&idx_config);
   if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao obter configuração de índice: %d", ret);
        xSemaphoreGive(sdMutex);
        return ret;
    }
 
     if(idx_config.last_write_idx != write_idx)
    {
		printf(">>>>>>REC CONFIG TOTAL =%d\n", idx_config.total_idx);
		if (idx_config.total_idx > 0) {
            write_idx = (idx_config.last_write_idx + 1)%idx_config.total_idx;
            }
        else {write_idx = (idx_config.last_write_idx + 1)%1;
            }   
    }
  	
    if (write_idx == UNSPECIFIC_RECORD)
       { 
        if(has_enough_size())
        {
            f = fopen(record_file,"a");
            if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open file for add");
            xSemaphoreGive(sdMutex);
            ret = ESP_FAIL;
            }
        }
        else
        {
            f = fopen(record_file,"r+");
            if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open file for read");
            xSemaphoreGive(sdMutex);
            ret = ESP_FAIL;
            }
            write_idx = 0;
			  fseeko(f, RECORD_FILE_HEADER_SIZE, SEEK_SET);
              fseeko(f, 0, SEEK_CUR);
        } 
        
        fprintf(f," %s   %s     %1d       %s\n", get_date(), get_time(), channel, data);
        ftell(f);
        fclose(f);  
    }
    else
    {
        f = fopen(record_file,"r+");
            if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open file for read");
            ret = ESP_FAIL;
            }

            fseeko(f, RECORD_FILE_HEADER_SIZE, SEEK_SET);
            fseeko(f, 0, SEEK_CUR);
 
        fprintf(f," %s   %s     %1d       %s\n", get_date(), get_time(), channel, data);
      //  xSemaphoreGive(sdMutex);
      }   
        fclose(f);
         
         ESP_LOGI(TAG, "Record %s   %s     %1d       %s\n", get_date(), get_time(), channel, data);
 
   // Atualize a configuração de índice
    if (ret == ESP_OK) {     
        idx_config.last_write_idx = write_idx;
        if(write_idx == UNSPECIFIC_RECORD){
        idx_config.total_idx = idx_config.total_idx + 1;
        current_index = idx_config.total_idx;
        }
     }
 
 ESP_LOGI("SDMMC", "Salvando configuração de índice...");  
 
 ret = save_index_config(&idx_config);
   xSemaphoreGive(sdMutex);
   return ret;
}

esp_err_t read_record_sd(uint32_t *cursor_pos, struct record_data_saved* record_data)
{	
	xSemaphoreTake(sdMutex,portMAX_DELAY);
    FILE *f = fopen(record_file,"r");
    
    if(f == NULL)
      {
        ESP_LOGI(TAG, "FILE NULL %d", *cursor_pos);
        xSemaphoreGive(sdMutex);
        return ESP_FAIL;
      }

   if (*cursor_pos ==0)
   {
    fseeko(f, RECORD_FILE_HEADER_SIZE, SEEK_SET);
    }
    
    fseeko(f, *cursor_pos, SEEK_CUR);
    fscanf(f," %s   %s     %1d       %s", record_data->date, record_data->time, &(record_data->channel), record_data->data);

    *cursor_pos = ftell(f);
    fclose(f);
    xSemaphoreGive(sdMutex);
    return ESP_OK;
}

bool read_record_file_sd(uint32_t* byte_to_read, char* str)
{
	xSemaphoreTake(sdMutex,portMAX_DELAY);
//    const char *record_file = MOUNT_POINT"/registro.csv";
    FILE *f = fopen(record_file,"r");
    bool end_file = false;

    if(f == NULL){
        ESP_LOGE(TAG, "FILE NULL");
    }
    else{
        fseeko(f, *byte_to_read, SEEK_SET);

        size_t read = fread(str, sizeof(char), 255,f);
        fclose(f);

        (*byte_to_read) += read;

        if(read < 255)
        {
            str[read] = '\0';
            end_file = true;
        }
    }
    xSemaphoreGive(sdMutex);
    return end_file;
}

esp_err_t delete_record_sd(void)
{
    // Check if destination file exists before renaming
    struct stat st;
    if (stat(record_file, &st) == 0) {
        // Delete it if it exists
        unlink(record_file);
    }
   if (has_SD_FILE_Created()==ESP_OK)
      {
	   save_default_record_idx_config();
	   return ESP_OK;
	  }
   else
      {
	   return ESP_FAIL;
	  } 
   
}

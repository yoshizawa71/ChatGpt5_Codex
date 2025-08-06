
/*
 * filesystem.c
 *
 * Centraliza a inicialização e desalocação do LittleFS para o sistema.
 * Adaptado de init_server_fs (SPIFFS) para uso modular de LittleFS.
 */

//#include "esp_vfs_littlefs.h"      // [ALTERAÇÃO] trocado esp_vfs_spiffs.h
#include "esp_littlefs.h"
#include "esp_log.h"
#include "system.h"                // Definições globais (e.g. mount points)

static const char *TAG = "Filesystem";

/**
 * @brief Inicializa o LittleFS do sistema.
 *
 * Configura e registra a partição "littlefs" montando-a em "/littlefs".
 * Substitui a antiga init_server_fs com SPIFFS.
 */
void init_filesystem(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path             = "/littlefs",
        .partition_label       = "littlefs",
        .format_if_mount_failed= false,
        .dont_mount            = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LittleFS mount failed (%s), will format...",
                 esp_err_to_name(err));
        // formata a partição
        esp_err_t ferr = esp_littlefs_format(conf.partition_label);
        if (ferr != ESP_OK) {
            ESP_LOGE(TAG, "LittleFS format failed (%s)", esp_err_to_name(ferr));
            return;
        }
        // tenta montar novamente
        err = esp_vfs_littlefs_register(&conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LittleFS mount after format failed (%s)",
                     esp_err_to_name(err));
            return;
        }
        ESP_LOGI(TAG, "LittleFS formatted and mounted successfully");
    } else {
        ESP_LOGI(TAG, "LittleFS mounted successfully");
    }

    // exibe informações de uso do FS
    size_t total = 0, used = 0;
    err = esp_littlefs_info(conf.partition_label, &total, &used);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LittleFS info (%s)",
                 esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "LittleFS total: %u KB, used: %u KB",
                 (unsigned)(total / 1024), (unsigned)(used / 1024));
    }
}

/**
 * @brief Desmonta e desregistra o LittleFS do sistema.
 */
void deinit_filesystem(void)
{
    // [ALTERAÇÃO] desregistra o VFS do LittleFS
    esp_err_t ret = esp_vfs_littlefs_unregister("littlefs");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao desregistrar LittleFS (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "LittleFS desregistrado com sucesso");
    }
}


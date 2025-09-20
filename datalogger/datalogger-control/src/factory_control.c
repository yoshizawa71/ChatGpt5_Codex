#include "ifdef_features.h" 
#include "factory_control.h"
#include <string.h>
#include <fcntl.h>
#include <datalogger_control.h>
#include "datalogger_driver.h"
#include <time.h>
#include "esp_timer.h"
#include "sara_r422.h"
#include "sleep_preparation.h"
#include "tcp_log_server.h"
#include "modbus_rtu_master.h"
#include "log_mux.h"
#include "wifi_softap_sta.h"
#include "driver/sdmmc_host.h"
#include "esp_http_server.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_vfs.h"
#include "cJSON.h"
#include "mdns.h"

#include "esp_littlefs.h"
#include "lwip/apps/netbiosns.h"
#include "datalogger_driver.h"
#include "oled_display.h"
#include "sdmmc_driver.h"
#include "pressure_meter.h"
#include "pulse_meter.h"
#include "rele.h"
#include "esp_wifi.h"

#include "ff.h"
#include "sleep_control.h"

#include "pressure_calibrate.h"
#include "system.h"
#include "TCA6408A.h"

#include "rs485_registry.h"
#include "xy_md02_driver.h"
#include "rs485_manager.h"
#include "rs485_hw.h" 
#include "freertos/FreeRTOS.h"
#include "modbus_guard_session.h"
#include <stdatomic.h>

#define FACTORY_CONFIG_TIMER   180 //Tempo do factor config ficar ativo
#define SENSOR_DISCONNECTED_THRESHOLD 0.1   // Exemplo: menor que 0.1 é considerado desconectado

#ifndef AP_SILENCE_WINDOW_S
#define AP_SILENCE_WINDOW_S       20    // ajuste a gosto
#endif

#if FC_TRACE_INTERACTION
#warning "FC_TRACE_INTERACTION = 1 (este arquivo)"
#else
#warning "FC_TRACE_INTERACTION = 0 (este arquivo)"
#endif

#if CONFIG_MODBUS_ENABLE
  #include "modbus_rtu_master.h"
  #include "rs485_manager.h"
#endif


#if FC_TRACE_INTERACTION
  #define update_last_interaction() \
      update_last_interaction_tracked(__FILE__, __LINE__, __func__)
#else
  #define update_last_interaction() \
      update_last_interaction_real()
#endif

// Define a flag for shutdown request
//bool server_shutdown_requested = false;
static bool save_button_action = false;
bool user_initiated_exit = false;

extern bool first_factory_setup;
extern QueueHandle_t xQueue_Factory_Control;
bool Send_FactoryControl_Task_ON;
bool Receive_Response_FactoryControl = false;

//const uint32_t ap_silence_secs = 0; // Tempo de suspensão do Acess Point 

#define MDNS_HOST_NAME  "datalogger"
#define SERVER_BASE_PATH  "/esp_web_server"
#define SERVER_WEB_PARTITION     "esp_web_server"  // label idêntico ao CSV
#define SUPER_USER_KEY  "admin1234"

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
//#define SCRATCH_BUFSIZE (10240)
#define SCRATCH_BUFSIZE (16384)

#define NUM_CAL_POINTS 5

#define RS485_MAP_UI_PATH   "/littlefs/rs485_map_ui.json"

#ifndef ENABLE_PING_LOG
#define ENABLE_PING_LOG 0
#endif

typedef struct rest_server_context {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

typedef struct {
    TickType_t ts; bool alive; uint8_t fc; esp_err_t err;
    uint8_t fails; TickType_t backoff_until;
    bool inflight;
} ping_cache2_t;

static const char *TAG = "Factory Control";

static esp_err_t init_server_fs(void);
static void init_mdns(void);
static void start_factory_routine(void);
static void stop_factory_server(void);

static esp_err_t start_server(void);
static httpd_handle_t server = NULL;

//**************************************
//   Desativar wifi e servidor acess point
//**************************************
//static esp_err_t deinit_server_fs(void);
static void deinit_mdns(void);
static esp_err_t stop_server(httpd_handle_t server);

static void console_tcp_enable(uint16_t port);
static void console_tcp_disable(void);
//------------Tasks--------------
static void shutdown_task(void* pvParameters);
//-------------------------------
static esp_err_t rest_common_get_handler(httpd_req_t *req);
static esp_err_t config_device_get_handler(httpd_req_t *req);
static esp_err_t get_time_handler(httpd_req_t *req);
static esp_err_t config_device_post_handler(httpd_req_t *req);
static esp_err_t connect_sta_post_handler(httpd_req_t *req);
static esp_err_t status_sta_get_handler(httpd_req_t *req);
//static esp_err_t disconnect_ap_handler(httpd_req_t *req);
static esp_err_t config_network_get_handler(httpd_req_t *req);
static esp_err_t config_network_post_handler(httpd_req_t *req);
static esp_err_t config_login_post_handler(httpd_req_t *req);
static esp_err_t config_operation_get_handler(httpd_req_t *req);
static esp_err_t config_operation_post_handler(httpd_req_t *req);
static esp_err_t exit_device_post_handler(httpd_req_t *req);
static esp_err_t ping_handler(httpd_req_t *req);

static esp_err_t rs485_config_get_handler(httpd_req_t *req);
static esp_err_t rs485_config_post_handler(httpd_req_t *req);
static esp_err_t rs485_ping_get_handler(httpd_req_t *req);
static esp_err_t rs485_config_delete_handler(httpd_req_t *req);

//------------------------------------------------------------------

static esp_err_t config_maintenance_get_handler(httpd_req_t *req);
static esp_err_t config_maintenance_post_handler(httpd_req_t *req);

static esp_err_t rele_activate(httpd_req_t *req);

//------------------------------------------------------------------
static esp_err_t load_registers_get_handler(httpd_req_t *req);
static esp_err_t delete_registers_get_handler(httpd_req_t *req);

// Variável global para armazenar o último tick de interação
static TickType_t last_interaction_ticks;
static time_t last_interaction;
static bool super_user_logged = false;

static sensor_t sensor_em_calibracao = analog_1;
static pressure_unit_t unidade_em_calibracao = PRESSURE_UNIT_BAR;

extern bool time_manager_task_ON;


xTaskHandle Factory_Config_TaskHandle = NULL;


// Variáveis globais para cache do estado
static bool sta_connected = false;
static bool sta_status_task = false;
bool ap_active = true;
static char sta_ssid[32] = {0};
static char sta_password[64] = {0};

// Variáveis temporárias para calibração
static bool temp_ativar_cali = false;
static bool temp_zerar = false;
static bool temp_fcorr = false;
static bool rele_state = false; // Estado do relé


// ====== ESTADO ======
static volatile int64_t s_last_interaction_us = 0;  // monotônico em µs
static int64_t          s_last_exit_us        = 0;  // quando disparamos o EXIT
static bool             s_single_shot_armed   = true; // habilitado no boot

// “Quem chamou por último”:
static char     s_last_ui_file[48];
static char     s_last_ui_func[40];
static int      s_last_ui_line = 0;
static int64_t  s_last_ui_us   = 0;

static volatile bool fc_user_interacted_since_exit = false;
//-------------------------------------------
static ping_cache2_t s_ping2[248] = {0};
/*
static atomic_bool s_shutting_down = false;
static portMUX_TYPE s_shutdown_mux = portMUX_INITIALIZER_UNLOCKED;
*/
// Helper: extrai só o nome do arquivo (sem path)
static const char *basefile(const char *path)
{
    if (!path) return "?";
    const char *b = path;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') b = p + 1;
    }
    return b;
}

// Se STA ativo (high power), suspende AP por 30 s; senão, 0 (deep sleep cobre low power)
static inline uint32_t compute_ap_silence_secs(void) {
 //   return has_activate_sta() ? 20U : 0U;
 return has_always_on() ? 15U : 0U;
}

static inline bool now_before(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) < 0;
}

/*static inline bool shutdown_try_begin(void)
{
    bool allowed = false;
    portENTER_CRITICAL(&s_shutdown_mux);
    if (!s_shutting_down) {
        s_shutting_down = true;
        allowed = true;
    }
    portEXIT_CRITICAL(&s_shutdown_mux);
    return allowed;
}

static inline bool is_shutting_down(void)
{
    // leitura sem lock é ok para “read-mostly”
    return s_shutting_down;
}
*/
//-------------------------------------------
/*void update_last_interaction(void)
{
 
    s_last_interaction_us = esp_timer_get_time();
    // Log mais útil agora em segundos:
    ESP_LOGI(TAG, "Last interaction at %.3f s since boot",
             (double)s_last_interaction_us / 1e6);
}
*/
// ======= IMPLEMENTAÇÃO REAL (sem log): renomeada =======
void update_last_interaction_real(void)
{
    s_last_interaction_us = esp_timer_get_time();
     fc_user_interacted_since_exit = true;   // ← marca rearmamento permitido
    // Se quiser manter um log curto aqui, pode; eu prefiro deixar sem para não poluir:
    // ESP_LOGI(TAG, "Last interaction at %.3f s", (double)s_last_interaction_us/1e6);
}

// Interação "de fundo": não seta o token, só atualiza o relógio.
void update_last_interaction_background(void)
{
    s_last_interaction_us = esp_timer_get_time();
    // NÃO mexe em fc_user_interacted_since_exit
}

// ======= WRAPPER com log e origem =======
void update_last_interaction_tracked(const char *file, int line, const char *func)
{
    // Guarda a origem
    const char *bf = basefile(file);
    snprintf(s_last_ui_file, sizeof(s_last_ui_file), "%s", bf);
    snprintf(s_last_ui_func, sizeof(s_last_ui_func), "%s", func ? func : "?");
    s_last_ui_line = line;
    s_last_ui_us   = esp_timer_get_time();

#if FC_TRACE_INTERACTION
    // Log enxuto (ajuste o nível para I/W se quiser menos ruído)
    ESP_LOGW("FC/TRACE",
             "update_last_interaction() from %s:%d (%s) @ %.3f s",
             s_last_ui_file, s_last_ui_line, s_last_ui_func,
             (double)s_last_ui_us / 1e6);

#endif
    // Faz o que sempre fez:
    update_last_interaction_real();
}

// Getter novo em µs (se você ainda não tinha)
int64_t get_factory_routine_last_interaction_us(void)
{
    return s_last_interaction_us;
}

TickType_t get_factory_routine_last_interaction(void)
{
 //  return last_interaction_ticks;
    const int64_t us = s_last_interaction_us;
    // 1 tick = (1e6 / configTICK_RATE_HZ) µs
    const int64_t us_per_tick = (1000000LL / configTICK_RATE_HZ);
    return (TickType_t)(us / us_per_tick);
}

// Dump amigável do ÚLTIMO local que mexeu no “last interaction”
void factory_dump_last_interaction_origin(void)
{
    ESP_LOGW("FC/TRACE",
             "LAST caller of update_last_interaction(): %s:%d (%s) @ %.3f s",
             s_last_ui_file[0] ? s_last_ui_file : "<none>",
             s_last_ui_line,
             s_last_ui_func[0] ? s_last_ui_func : "<none>",
             (double)s_last_ui_us / 1e6);
}
/*int64_t get_factory_routine_last_interaction_us(void)
{
    return s_last_interaction_us;
}*/

static void start_factory_routine(void)
{
    update_last_interaction();
    init_mdns();
    init_server_fs();
    start_server();
}

static void stop_factory_server(void)
{
	deinit_mdns();
//	deinit_server_fs();
	stop_server(server);

	return;
	 
}

static void initialise_mdns(void)
{
    mdns_init();
    mdns_hostname_set(MDNS_HOST_NAME);
    mdns_instance_name_set(MDNS_HOST_NAME);

    mdns_txt_item_t serviceTxtData[] = {
        {"board", "esp32"},
        {"path", "/"}
    };

    ESP_ERROR_CHECK(mdns_service_add("ESP32-WebServer", "_http", "_tcp", 80, serviceTxtData,
                                     sizeof(serviceTxtData) / sizeof(serviceTxtData[0])));
}

static void deinitialise_mdns(void)
{
	mdns_free();
}

static void init_mdns(void)
{
    initialise_mdns();
    netbiosns_init();
    netbiosns_set_name(MDNS_HOST_NAME);
}

static void deinit_mdns(void)
{
    deinitialise_mdns();
    netbiosns_stop();
}

static esp_err_t init_server_fs(void)
{
      esp_vfs_littlefs_conf_t conf = {
        .base_path = SERVER_BASE_PATH,          // Ponto de montagem do FS (sistema)
        .partition_label = SERVER_WEB_PARTITION,     // Label definido no partition_table.csv
        .format_if_mount_failed = false,      // Formatar se falhar ao montar
        .dont_mount = false,
    };

    // Use settings defined above to initialize and mount LittleFS filesystem.
    // Note: esp_vfs_littlefs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
         return ESP_FAIL;
    }

    // Exibe informações de uso do FS
    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao obter infos do LittleFS (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "LittleFS total: %d KB, usado: %d KB", total / 1024, used / 1024);
    }
    
       return ESP_OK;
    
}

/*
static esp_err_t deinit_server_fs(void)
{   
        // [ALTERAÇÃO] desregistra o VFS do LittleFS
    esp_err_t ret = esp_vfs_littlefs_unregister("littlefs");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao desregistrar LittleFS (%s)", esp_err_to_name(ret));
        return ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "LittleFS desregistrado com sucesso");
        return ESP_OK;
    }
}
*/

static esp_err_t start_server(void)
{
    super_user_logged = false;

    rest_server_context_t *rest_context = calloc(1, sizeof(rest_server_context_t));
    if(!rest_context)
    {
        ESP_LOGE(TAG, "No memory for rest context");
        return ESP_FAIL;
    }
    strlcpy(rest_context->base_path, SERVER_BASE_PATH, sizeof(rest_context->base_path));

//    httpd_handle_t server = NULL;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 10240; // Aumenta a pilha para evitar falhas
    config.max_uri_handlers = 25;
    config.max_open_sockets = 7; // Mais sockets para múltiplas conexões
    config.lru_purge_enable = true; // Limpa sockets ociosos
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP Server");
    if(httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Start server failed");
        free(rest_context);
        return ESP_FAIL;
    }
    
    // silencia o /ping e garante ENERGY/SDMMC visíveis
esp_log_level_set("PING",   ESP_LOG_NONE);
esp_log_level_set("ENERGY", ESP_LOG_INFO);
esp_log_level_set("SDMMC",  ESP_LOG_INFO);
//--------------------------------------------    
// Registrar o novo endpoint

    httpd_uri_t get_time_uri = {
        .uri = "/get_time",
        .method = HTTP_GET,
        .handler = get_time_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &get_time_uri);    
    
//--------------------------------------------
    httpd_uri_t config_device_post_uri = {
        .uri = "/configDeviceSave",
        .method = HTTP_POST,
        .handler = config_device_post_handler,
        .user_ctx = rest_context
    };

    httpd_register_uri_handler(server, &config_device_post_uri);
//--------------------------------------------
    httpd_uri_t connect_sta_post_uri = {
        .uri       = "/connect_sta",
        .method    = HTTP_POST,
        .handler   = connect_sta_post_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &connect_sta_post_uri);
    
//--------------------------------------------
    httpd_uri_t exit_device_uri = {
        .uri = "/exitDevice",
        .method = HTTP_POST,
        .handler = exit_device_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &exit_device_uri);

//--------------------------------------------
        /* URI handler for light brightness control */
    httpd_uri_t config_network_post_uri = {
        .uri = "/configNetworkSave",
        .method = HTTP_POST,
        .handler = config_network_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &config_network_post_uri);

//--------------------------------------------
    httpd_uri_t config_login_post_uri = {
        .uri = "/configOpLogin",
        .method = HTTP_POST,
        .handler = config_login_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &config_login_post_uri);

//--------------------------------------------
    httpd_uri_t config_operation_post_uri = {
        .uri = "/configOpSave",
        .method = HTTP_POST,
        .handler = config_operation_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &config_operation_post_uri);
    
//----------------------------------------------------------
    httpd_uri_t config_maintenance_post_uri = {
        .uri = "/configMaintSave",
        .method = HTTP_POST,
        .handler = config_maintenance_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &config_maintenance_post_uri);

//--------------------------------------------  
 httpd_uri_t rele_get_uri = {
    .uri = "/rele_device",
    .method = HTTP_GET,
    .handler = rele_activate,
    .user_ctx = rest_context
};
httpd_register_uri_handler(server, &rele_get_uri);

httpd_uri_t rele_post_uri = {
    .uri = "/rele_device",
    .method = HTTP_POST,
    .handler = rele_activate,
    .user_ctx = rest_context
};
httpd_register_uri_handler(server, &rele_post_uri);
//--------------------------------------------

    httpd_uri_t config_device_get_uri = {
        .uri = "/configDeviceGet",
        .method = HTTP_GET,
        .handler = config_device_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &config_device_get_uri);

//--------------------------------------------
    httpd_uri_t status = {
        .uri       = "/status_sta",
        .method    = HTTP_GET,
        .handler   = status_sta_get_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &status);
    
//--------------------------------------------
    httpd_uri_t config_network_get_uri = {
        .uri = "/configNetworkGet",
        .method = HTTP_GET,
        .handler = config_network_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &config_network_get_uri);

//--------------------------------------------
    httpd_uri_t config_operation_get_uri = {
        .uri = "/configOpGet",
        .method = HTTP_GET,
        .handler = config_operation_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &config_operation_get_uri);
    
//----------------------------------------------------------
    httpd_uri_t config_maintenance_get_uri = {
        .uri = "/configMaintGet",
        .method = HTTP_GET,
        .handler = config_maintenance_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &config_maintenance_get_uri); 
//----------------------------------------------------------    

    httpd_uri_t load_registers_get_uri = {
        .uri = "/loadRegisters",
        .method = HTTP_GET,
        .handler = load_registers_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &load_registers_get_uri);
    
//----------------------------------------------------------
//           RS485 Config
#if CONFIG_MODBUS_ENABLE
// ---------------- RS485 CONFIG GET ----------------
    httpd_uri_t rs485_config_get_uri = {
        .uri      = "/rs485ConfigGet",
        .method   = HTTP_GET,
        .handler  = rs485_config_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &rs485_config_get_uri);

// ---------------- RS485 CONFIG SAVE (POST) ----------------
    httpd_uri_t rs485_config_post_uri = {
        .uri      = "/rs485ConfigSave",
        .method   = HTTP_POST,
        .handler  = rs485_config_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &rs485_config_post_uri);
    
    
    // ---------------- RS485 REGISTER (POST) -> usa o MESMO handler do ConfigSave ----------------
httpd_uri_t rs485_register_post_uri = {
    .uri      = "/rs485Register",
    .method   = HTTP_POST,
    .handler  = rs485_config_post_handler,  // handler unificado
    .user_ctx = rest_context
};
httpd_register_uri_handler(server, &rs485_register_post_uri);


// ---------------- RS485 PING (GET) ----------------
    httpd_uri_t rs485_ping_get_uri = {
        .uri      = "/rs485Ping",
        .method   = HTTP_GET,
        .handler  = rs485_ping_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &rs485_ping_get_uri);
    
    
    // ---------------- RS485 CONFIG DELETE (GET) ----------------
httpd_uri_t rs485_cfg_delete_uri = {
    .uri      = "/rs485ConfigDelete",
    .method   = HTTP_GET,
    .handler  = rs485_config_delete_handler,
    .user_ctx = rest_context
};
httpd_register_uri_handler(server, &rs485_cfg_delete_uri);

#endif  
//----------------------------------------------------------
//           Delete the file
//----------------------------------------------------------

  httpd_uri_t delete_registers_get_uri = {
        .uri = "/deleteRegisters",
        .method = HTTP_GET,
        .handler = delete_registers_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &delete_registers_get_uri);
 
//----------------------------------------------------------
// AP monitoring by ping
//----------------------------------------------------------

httpd_uri_t ping_uri = {
    .uri      = "/ping",
    .method   = HTTP_GET,
    .handler  = ping_handler,
    .user_ctx = NULL
};
httpd_register_uri_handler(server, &ping_uri); // Coloque isso onde inicializa o 

//----------------------------------------------------------

    /* URI handler for getting web server files */
    httpd_uri_t common_get_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = rest_common_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &common_get_uri);

    return ESP_OK;
}

//*******************************************
// Function for stopping the server

static esp_err_t stop_server(httpd_handle_t server)
{
/*
	 * @brief   Unregister a URI handler
	 *
	 * @param[in] handle    handle to HTTPD server instance
	 * @param[in] uri       URI string
	 * @param[in] method    HTTP method
	 *
	 * @return
	 *  - ESP_OK : On successfully deregistering the handler
	 *  - ESP_ERR_INVALID_ARG : Null arguments
	 *  - ESP_ERR_NOT_FOUND   : Handler with specified URI and method not found

	esp_err_t httpd_unregister_uri_handler(httpd_handle_t handle,
	                                       const char *uri, httpd_method_t method);

	*/
	if (server != NULL)
	{
	    ESP_LOGI(TAG, "*** Stopping HTTP Server ***");
	    if(httpd_stop(server) != ESP_OK) {
	        printf("*** Stop server failed ***\n");
	        return ESP_FAIL;
	    }
	    else
	       {
	    	printf("*** Stop server Success ***\n");
	    	server = NULL;
	    	return ESP_OK;
	        }
	}

	return ESP_ERR_INVALID_ARG;
}
//---------------------------------------
void wifi_portal_on_ap_start(void) {
	
	if (!has_always_on()) {
    return; // headless: não ligar HTTP/portal aqui
       }
    if (server == NULL) {
        ESP_LOGI(TAG, "AP_START: HTTP server não estava rodando — iniciando.");
        start_server();
    } else {
        ESP_LOGI(TAG, "AP_START: HTTP server já ativo.");
    }
    
        ap_active = true;                // reflete o estado do AP para o front-end
}
//---------------------------------------
// --- Console TCP: wrappers simples para ligar/desligar em um lugar só ---
static void console_tcp_enable(uint16_t port)
{
    start_tcp_log_server_task(port);        // seu servidor TCP (não bloqueante)
//    logmux_set_tcp_writer(tcp_log_vprintf); // writer do tcp_log_server.c
    logmux_init(tcp_log_vprintf);
    logmux_enable_tcp(true);                // envia logs para TCP
    logmux_enable_uart(false);              // mantém UART0 muda
    ESP_LOGI("CONSOLE", "TCP log console enabled on port %u", port);
    
}

static void console_tcp_disable(void) {
    // Desliga o writer TCP e reabilita completamente a UART
    logmux_set_tcp_writer(NULL);
    logmux_enable_tcp(false);
    logmux_enable_uart(true);
    logmux_restore();             // restaura vprintf original
    stop_tcp_log_server_task();
    ESP_LOGI("CONSOLE", "TCP log console DISABLED");
}
//*******************************************
//HTTP HANDLER FUNCTIONS

#define CHECK_FILE_EXTENSION(filename, ext) (strcasecmp(&filename[strlen(filename) - strlen(ext)], ext) == 0)

static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filepath)
{
    const char *type = "text/plain";
    if (CHECK_FILE_EXTENSION(filepath, ".html")) {
        type = "text/html";
    } else if (CHECK_FILE_EXTENSION(filepath, ".js")) {
        type = "application/javascript";
    } else if (CHECK_FILE_EXTENSION(filepath, ".css")) {
        type = "text/css";
    } else if (CHECK_FILE_EXTENSION(filepath, ".png")) {
        type = "image/png";
    } else if (CHECK_FILE_EXTENSION(filepath, ".ico")) {
        type = "image/x-icon";
    } else if (CHECK_FILE_EXTENSION(filepath, ".svg")) {
        type = "text/xml";
    }
    return httpd_resp_set_type(req, type);
}

static esp_err_t rest_common_get_handler(httpd_req_t *req)
{
    update_last_interaction();
    char filepath[FILE_PATH_MAX];

    rest_server_context_t *rest_context = (rest_server_context_t *)req->user_ctx;
    strlcpy(filepath, rest_context->base_path, sizeof(filepath));
    if (req->uri[strlen(req->uri) - 1] == '/') {
        strlcat(filepath, "/Home.html", sizeof(filepath));
    } else if(strcmp(req->uri, "/ConfigOperationLogin.html") == 0 || strcmp(req->uri, "/ConfigOperation.html") == 0) {
//    } else if(strcmp(req->uri, "/ConfigAdmin.html") == 0 || strcmp(req->uri, "/ConfigOperation.html") == 0) {
        if(super_user_logged) {
            strlcat(filepath, "/ConfigOperation.html", sizeof(filepath));
        } else {
            strlcat(filepath, "/ConfigOperationLogin.html", sizeof(filepath));
        }
    } else {
        strlcat(filepath, req->uri, sizeof(filepath));
    }
    int fd = open(filepath, O_RDONLY, 0);
    if (fd == -1) {
        ESP_LOGE(TAG, "Failed to open file : %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    set_content_type_from_file(req, filepath);

    char *chunk = rest_context->scratch;
    ssize_t read_bytes;
    do {
        /* Read file in chunks into the scratch buffer */
        read_bytes = read(fd, chunk, SCRATCH_BUFSIZE);
        if (read_bytes == -1) {
            ESP_LOGE(TAG, "Failed to read file : %s", filepath);
            close(fd);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read file");
            return ESP_FAIL;
        } else if (read_bytes > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                close(fd);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }
    } while (read_bytes > 0);
    /* Close file after sending complete */
    close(fd);
    ESP_LOGI(TAG, "File sending complete: %s", filepath);
    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}


static esp_err_t get_time_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "date", get_date());
    cJSON_AddStringToObject(root, "time", get_time());
    char *response = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    cJSON_Delete(root);
    free(response);
    return ESP_OK;
}

static esp_err_t config_device_post_handler(httpd_req_t *req) {
    int total_len = req->content_len;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    int cur_len = 0, received = 0;
    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    ESP_LOGI(TAG, "JSON recebido: %s", buf);

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGE(TAG, "Falha ao parsear JSON: %s", cJSON_GetErrorPtr());
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON inválido");
        return ESP_FAIL;
    }

    // Validar campos obrigatórios
    cJSON *date_item = cJSON_GetObjectItem(root, "date");
    cJSON *time_item = cJSON_GetObjectItem(root, "time");
    if (!cJSON_IsString(date_item) || !cJSON_IsString(time_item)) {
        ESP_LOGE(TAG, "Campos 'date' ou 'time' ausentes ou inválidos");
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Campos 'date' ou 'time' inválidos");
        return ESP_FAIL;
    }
    // Validar formato de data (DD/MM/YYYY)
    if (strlen(date_item->valuestring) != 10 || date_item->valuestring[2] != '/' || date_item->valuestring[5] != '/') {
        ESP_LOGE(TAG, "Formato de data inválido: %s (esperado DD/MM/YYYY)", date_item->valuestring);
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Formato de data inválido");
        return ESP_FAIL;
    }
    // Validar formato de hora (HH:MM ou HH:MM:SS)
    if (strlen(time_item->valuestring) != 5 && strlen(time_item->valuestring) != 8) {
        ESP_LOGE(TAG, "Formato de hora inválido: %s (esperado HH:MM ou HH:MM:SS)", time_item->valuestring);
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Formato de hora inválido");
        return ESP_FAIL;
    }

    // Configurar parâmetros
    set_device_id(cJSON_GetObjectItem(root, "id")->valuestring);
    set_name(cJSON_GetObjectItem(root, "name")->valuestring);
    set_phone(cJSON_GetObjectItem(root, "phone")->valuestring);
    set_ssid_ap(cJSON_GetObjectItem(root, "ssid_ap")->valuestring);
    set_password_ap(cJSON_GetObjectItem(root, "wifi_pw_ap")->valuestring);
    set_activate_sta(cJSON_IsTrue(cJSON_GetObjectItem(root, "activate_sta")));
    set_ssid_sta(cJSON_GetObjectItem(root, "ssid_sta")->valuestring);
    set_password_sta(cJSON_GetObjectItem(root, "wifi_pw_sta")->valuestring);
//    set_activate_send_freq_mode(cJSON_IsTrue(cJSON_GetObjectItem(root, "chk_freq_send_data")));
//    set_activate_send_time_mode(cJSON_IsTrue(cJSON_GetObjectItem(root, "chk_time_send_data")));
 //   set_send_period(cJSON_GetObjectItem(root, "send_period")->valueint);
    set_deep_sleep_period(cJSON_GetObjectItem(root, "deep_sleep_period")->valueint);
    save_pulse_zero(cJSON_IsTrue(cJSON_GetObjectItem(root, "save_pulse_zero")));
    set_scale(cJSON_GetObjectItem(root, "scale")->valueint);
    set_date(cJSON_GetObjectItem(root, "date")->valuestring); // Forma original
    set_time(cJSON_GetObjectItem(root, "time")->valuestring); // Forma original
    set_factory_config(cJSON_IsTrue(cJSON_GetObjectItem(root, "finished_factory")));
    set_always_on(cJSON_IsTrue(cJSON_GetObjectItem(root, "always_on")));
    set_device_active(cJSON_IsTrue(cJSON_GetObjectItem(root, "device_active")));
//    send_value(cJSON_IsTrue(cJSON_GetObjectItem(root, "send_value")));
cJSON *mode = cJSON_GetObjectItem(root, "send_mode");
if (cJSON_IsString(mode)) {
    // grava "freq" ou "time" em dev_config.send_mode
    set_send_mode(mode->valuestring);

    if (is_send_mode_freq()) {
        cJSON *p = cJSON_GetObjectItem(root, "send_period");
        if (cJSON_IsNumber(p)) {
            set_send_period(p->valueint);
        }
    }
    else if (is_send_mode_time()) {
            cJSON *arr = cJSON_GetObjectItem(root, "send_times");
            if (cJSON_IsArray(arr) && cJSON_GetArraySize(arr) == 4) {
               for (int i = 0; i < 4; i++) {
                   cJSON *t = cJSON_GetArrayItem(arr, i);
                    if (cJSON_IsNumber(t)) {
                   // número válido 0–23
                    set_send_time(i + 1, t->valueint);
                     }
                        else if (t && t->type == cJSON_NULL) {
                        // campo apagado pelo usuário → volta a ser “sem valor”
                        set_send_time(i + 1, -1);
                        }
        // caso o item esteja ausente ou de outro tipo, pula sem alterar
               }
            }
    }
}

 // imprimir Json só para teste
     const char *config_device = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, config_device);
    free((void *)config_device);
    cJSON_Delete(root);


//    cJSON_Delete(root);
save_button_action=true;//para saber que o botão de gravar foi acionado.
    // Salvar configurações e atualizar data/hora
    save_config();
    config_system_time();
    update_last_interaction();

    httpd_resp_sendstr(req, "Post control value successfully");
    return ESP_OK;
}

static esp_err_t config_operation_get_handler(httpd_req_t *req)
{
    update_last_interaction();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "serial_number", get_serial_number());
    cJSON_AddStringToObject(root, "company", get_company());
    cJSON_AddStringToObject(root, "ds_start", get_deep_sleep_start());
    cJSON_AddStringToObject(root, "ds_end", get_deep_sleep_end());
    if(has_reset_count())
    {
        cJSON_AddTrueToObject(root, "count_reset");
    }
    else
    {
        cJSON_AddFalseToObject(root, "count_reset");
    }
    cJSON_AddStringToObject(root, "keep_alive", get_keep_alive());
 /*   if(has_log_level_1())
    {
        cJSON_AddTrueToObject(root, "log1");
    }
    else
    {
        cJSON_AddFalseToObject(root, "log1");
    }
    if(has_log_level_2())
    {
        cJSON_AddTrueToObject(root, "log2");
    }
    else
    {
        cJSON_AddFalseToObject(root, "log2");
    }*/

    //--------------------------------------------
    if(has_enable_post())
    {
        cJSON_AddTrueToObject(root, "post_en");
    }
    else
    {
        cJSON_AddFalseToObject(root, "post_en");
    }
    if(has_enable_get())
    {
        cJSON_AddTrueToObject(root, "get_en");
    }
    else
    {
        cJSON_AddFalseToObject(root, "get_en");
    }

    cJSON_AddStringToObject(root, "config_server_url", get_config_server_url());
    cJSON_AddNumberToObject(root, "config_server_port", get_config_server_port());
    cJSON_AddStringToObject(root, "config_server_path", get_config_server_path());
    
    cJSON_AddNumberToObject(root, "level_min", get_level_min());
    cJSON_AddNumberToObject(root, "level_max", get_level_max());
    
    
    //**********************
    time_t t = get_last_data_sent();
    char buff[20];
    strftime(buff, 20, "%d/%m/%Y %H:%M:%S", localtime(&t));
    if (TIME_REFERENCE < get_last_data_sent()) // Serve para não aparecer a data de 1969
      {
       cJSON_AddStringToObject(root, "last_comm", buff);
      }

    cJSON_AddNumberToObject(root, "csq", get_csq());

    //**********************
/*    float voltage_bat= (float)get_battery();
    voltage_bat=voltage_bat*0.002;
    char voltage[5];
    sprintf (voltage, "%.2f", voltage_bat);

    cJSON_AddStringToObject(root, "battery", voltage);*/

    //**********************
    const char *config_operation = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, config_operation);
    free((void *)config_operation);
    cJSON_Delete(root);
    return ESP_OK;
}
//--------------------------------------------------------------------

static esp_err_t config_operation_post_handler(httpd_req_t *req)
{
    update_last_interaction();
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    set_serial_number(cJSON_GetObjectItem(root, "serial_number")->valuestring);
    set_company(cJSON_GetObjectItem(root, "company")->valuestring);
    set_deep_sleep_start(cJSON_GetObjectItem(root, "ds_start")->valuestring);
    set_deep_sleep_end(cJSON_GetObjectItem(root, "ds_end")->valuestring);
    enable_reset_count(cJSON_IsTrue(cJSON_GetObjectItem(root, "count_reset")));
    set_keep_alive(cJSON_GetObjectItem(root, "keep_alive")->valuestring);
/*    enable_log_level_1(cJSON_IsTrue(cJSON_GetObjectItem(root, "log1")));
    enable_log_level_2(cJSON_IsTrue(cJSON_GetObjectItem(root, "log2")));*/

    enable_post(cJSON_IsTrue(cJSON_GetObjectItem(root, "post_en")));
    enable_get(cJSON_IsTrue(cJSON_GetObjectItem(root, "get_en")));

    set_config_server_url(cJSON_GetObjectItem(root, "config_server_url")->valuestring);
    set_config_server_port(cJSON_GetObjectItem(root, "config_server_port")->valueint);
    set_config_server_path(cJSON_GetObjectItem(root, "config_server_path")->valuestring);
    
    set_level_min(cJSON_GetObjectItem(root, "level_min")->valueint);
    set_level_max(cJSON_GetObjectItem(root, "level_max")->valueint);
    
    cJSON_Delete(root);
    httpd_resp_sendstr(req, "Post control value successfully");
    save_config();
    return ESP_OK;
}
//--------------------------------------------------------------------
// GET /rs485ConfigGet  -> devolve JSON { "sensors": [...] }
// 1) se existir RS485_MAP_UI_PATH (JSON), entrega-o (contém type/subtype);
// 2) senão, carrega do binário e monta JSON com type/subtype vazios.
#if CONFIG_MODBUS_ENABLE
static esp_err_t rs485_config_get_handler(httpd_req_t *req)
{
    update_last_interaction();

    // 1) Tenta devolver o JSON persistido do front
    FILE *fj = fopen(RS485_MAP_UI_PATH, "rb");
    if (fj) {
        fseek(fj, 0, SEEK_END);
        long sz = ftell(fj);
        rewind(fj);
        if (sz > 0 && sz < 8192) {
            char *buf = malloc((size_t)sz + 1);
            if (buf) {
                size_t n = fread(buf, 1, (size_t)sz, fj);
                buf[n] = '\0';
                fclose(fj);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, buf);
                free(buf);
                return ESP_OK;
            }
        }
        fclose(fj);
    }

    // 2) Fallback: monta JSON a partir do binário salvo
    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_CreateArray();

    sensor_map_t map[RS485_MAX_SENSORS] = {0};
    size_t count = 0;
    load_rs485_config(map, &count);  // persiste channel/address no binário  :contentReference[oaicite:4]{index=4}

ESP_LOGI("RS485_REG_GLUE", "DELETE: carregado count=%u", (unsigned)count);
for (size_t i = 0; i < count; ++i) {
    ESP_LOGI("RS485_REG_GLUE", "ATUAL[%u]: ch=%u addr=%u type='%s' subtype='%s'",
             (unsigned)i, map[i].channel, map[i].address, map[i].type, map[i].subtype);
}

    for (size_t i = 0; i < count && i < RS485_MAX_SENSORS; i++) {
        cJSON *it = cJSON_CreateObject();
        cJSON_AddNumberToObject(it, "channel", map[i].channel);
        cJSON_AddNumberToObject(it, "address", map[i].address);
        cJSON_AddStringToObject(it, "type", "");     // sem informação no binário
        cJSON_AddStringToObject(it, "subtype", "");  // idem
        cJSON_AddItemToArray(arr, it);
    }
    cJSON_AddItemToObject(root, "sensors", arr);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json ? json : "{\"sensors\":[]}");
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

// ===== handler: POST /rs485ConfigSave e /rs485Register (aceita item único ou array) =====
static esp_err_t rs485_config_post_handler(httpd_req_t *req)
{
    update_last_interaction();

    // --- Lê corpo da requisição ---
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body missing");
        return ESP_FAIL;
    }
    buf[len] = '\0';

    // --- Parse JSON ---
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
        return ESP_FAIL;
    }

    // Aceita {"sensors":[...]} OU item único {"channel":...,"address":...,"type":...,"subtype":...}
    cJSON *arr = cJSON_GetObjectItem(root, "sensors");
    if (!cJSON_IsArray(arr)) {
        cJSON *jc = cJSON_GetObjectItem(root, "channel");
        cJSON *ja = cJSON_GetObjectItem(root, "address");
        if (cJSON_IsNumber(jc) && cJSON_IsNumber(ja)) {
            // embrulha o item único como array para reaproveitar fluxo
            cJSON *wrap = cJSON_CreateObject();
            cJSON *list = cJSON_CreateArray();
            cJSON_AddItemToArray(list, root);             // move root
            cJSON_AddItemToObject(wrap, "sensors", list);
            root = wrap;
            arr  = list;
        } else {
            cJSON_Delete(root);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"sensors_array_or_single_missing\"}");
            return ESP_OK;
        }
    }

    // Carrega a config atual para fazermos "upsert"
    sensor_map_t map[RS485_MAX_SENSORS] = {0};
    size_t count = 0;
    (void) load_rs485_config(map, &count);  // OK se vazio
    
    ESP_LOGI("RS485_REG_GLUE", "POST/SAVE: carregado count=%u (antes do upsert)", (unsigned)count);
    for (size_t i = 0; i < count; ++i) {
    ESP_LOGI("RS485_REG_GLUE", "EXISTE[%u]: ch=%u addr=%u type='%s' subtype='%s'",
             (unsigned)i, map[i].channel, map[i].address, map[i].type, map[i].subtype);
}

    // Também vamos manter/atualizar o JSON do front
    // Estrutura: { "sensors": [ {channel, address, type, subtype, ...} ] }
    cJSON *ui_root = NULL;
    cJSON *ui_arr  = NULL;
    {
        FILE *fj = fopen(RS485_MAP_UI_PATH, "rb");
        if (fj) {
            fseek(fj, 0, SEEK_END);
            long sz = ftell(fj);
            rewind(fj);
            if (sz > 0 && sz < 8192) {
                char *jbuf = (char*)malloc((size_t)sz + 1);
                if (jbuf) {
                    size_t n = fread(jbuf, 1, (size_t)sz, fj);
                    jbuf[n] = '\0';
                    ui_root = cJSON_Parse(jbuf);
                    free(jbuf);
                }
            }
            fclose(fj);
        }
        if (!ui_root) ui_root = cJSON_CreateObject();
        ui_arr = cJSON_GetObjectItem(ui_root, "sensors");
        if (!cJSON_IsArray(ui_arr)) {
            ui_arr = cJSON_CreateArray();
            cJSON_AddItemToObject(ui_root, "sensors", ui_arr);
        }
    }

    // --- Itera itens recebidos e faz upsert + valida ping curto ---
    for (cJSON *it = arr->child; it; it = it->next) {
        cJSON *jch = cJSON_GetObjectItem(it, "channel");
        cJSON *jaddr = cJSON_GetObjectItem(it, "address");
        cJSON *jtype = cJSON_GetObjectItem(it, "type");
        cJSON *jsub  = cJSON_GetObjectItem(it, "subtype");

        if (!cJSON_IsNumber(jch) || !cJSON_IsNumber(jaddr) || !cJSON_IsString(jtype)) {
            cJSON_Delete(root);
            cJSON_Delete(ui_root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing fields");
            return ESP_FAIL;
        }

        int ch = jch->valueint;
        int addr = jaddr->valueint;
        const char *ty = jtype->valuestring;
        const char *st = (jsub && cJSON_IsString(jsub)) ? jsub->valuestring : "";

ESP_LOGI("RS485_REG_GLUE", "RECV: ch=%d addr=%d type='%s' subtype='%s'", ch, addr, ty, st);
        // --- Ping curto para validar slave ---
        uint8_t used_fc = 0;
        bool exception = false; // <== corrige tipo
        bool alive = rs485_manager_ping((uint8_t)addr, pdMS_TO_TICKS(150), &used_fc, &exception);
        if (!alive) {
            // responde falha (o front mostra msg e não acende o LED)
            cJSON *resp = cJSON_CreateObject();
            cJSON_AddBoolToObject(resp, "ok", false);
            cJSON_AddStringToObject(resp, "msg", "dispositivo não respondeu");
            char *out = cJSON_PrintUnformatted(resp);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, out ? out : "{\"ok\":false}");
            free(out);
            cJSON_Delete(resp);
            cJSON_Delete(root);
            cJSON_Delete(ui_root);
            return ESP_OK;
        }
        cJSON_AddNumberToObject(it, "used_fc", used_fc);

ESP_LOGI("RS485_REG_GLUE", "PING OK: addr=%d used_fc=%u", addr, (unsigned)used_fc);

        // --- Upsert no binário (replace se já existir, senão append) ---
        sensor_map_t cand = {0};
        cand.channel = (uint8_t)ch;
        cand.address = (uint8_t)addr;
        strncpy(cand.type, ty, sizeof(cand.type) - 1);
        strncpy(cand.subtype, st, sizeof(cand.subtype) - 1);

        bool replaced = false;
        // Substitua o seu ESP_LOGI atual por:
ESP_LOGI("RS485_REG_GLUE", "UPSERT: ch=%u addr=%u type='%s' subtype='%s' (replaced=%s)",
         cand.channel, cand.address, cand.type, cand.subtype, replaced ? "yes" : "no");

        for (size_t i = 0; i < count; ++i) {
            if (map[i].channel == cand.channel && map[i].address == cand.address) {
                map[i] = cand;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            if (count >= RS485_MAX_SENSORS) {
                cJSON_Delete(root);
                cJSON_Delete(ui_root);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "max sensors");
                return ESP_FAIL;
            }
            map[count++] = cand;
        }

        // --- Upsert também no JSON do front (RS485_MAP_UI_PATH) ---
        bool ui_replaced = false;
        for (cJSON *jt = ui_arr->child; jt; jt = jt->next) {
            cJSON *uch = cJSON_GetObjectItem(jt, "channel");
            cJSON *uad = cJSON_GetObjectItem(jt, "address");
            if (cJSON_IsNumber(uch) && cJSON_IsNumber(uad) &&
                uch->valueint == ch && uad->valueint == addr)
            {
                // Atualiza type/subtype
                cJSON_ReplaceItemInObject(jt, "type",    cJSON_CreateString(ty));
                cJSON_ReplaceItemInObject(jt, "subtype", cJSON_CreateString(st));
                ui_replaced = true;
                break;
            }
        }
        if (!ui_replaced) {
            cJSON *newit = cJSON_CreateObject();
            cJSON_AddNumberToObject(newit, "channel", ch);
            cJSON_AddNumberToObject(newit, "address", addr);
            cJSON_AddStringToObject(newit, "type", ty);
            cJSON_AddStringToObject(newit, "subtype", st);
            cJSON_AddItemToArray(ui_arr, newit);
        }
    }

ESP_LOGI("RS485_REG_GLUE", "POST/SAVE: FINAL count=%u (vai persistir)", (unsigned)count);
for (size_t i = 0; i < count; ++i) {
    ESP_LOGI("RS485_REG_GLUE", "FINAL[%u]: ch=%u addr=%u type='%s' subtype='%s'",
             (unsigned)i, map[i].channel, map[i].address, map[i].type, map[i].subtype);
}

    // Persiste binário consolidado
    esp_err_t ret = save_rs485_config(map, count);
    if (ret != ESP_OK) {
        cJSON_Delete(root);
        cJSON_Delete(ui_root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
        return ret;
    }
    ESP_LOGI("RS485_REG_GLUE", "POST/SAVE: OK (count=%u)", (unsigned)count);


    // Persiste JSON do front
    char *ui_json = cJSON_PrintUnformatted(ui_root);
    if (ui_json) {
        FILE *fw = fopen(RS485_MAP_UI_PATH, "wb");
        if (fw) {
            fwrite(ui_json, 1, strlen(ui_json), fw);
            fclose(fw);
        }
        free(ui_json);
    }
    cJSON_Delete(ui_root);

    // Sucesso
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// GET /rs485Ping?channel=X&address=Y[&ts=...]
// Versão leve: NÃO faz "probe" de driver (rs485_registry_*); apenas verifica presença.
// Usa modbus_master_ping() (read-only, rápido). Mantém o mesmo JSON da versão anterior.
static esp_err_t rs485_ping_get_handler(httpd_req_t *req)
{
    update_last_interaction();

    const TickType_t PER_ADDR_MIN = pdMS_TO_TICKS(700);   // era 300 ms
    const TickType_t GLOBAL_MIN   = pdMS_TO_TICKS(150);   // simples amortecedor global

    static TickType_t s_global_last = 0;

    char qbuf[64], param[16];
    int addr = 0;
    int qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen > 1 && qlen < (int)sizeof(qbuf)) {
        if (httpd_req_get_url_query_str(req, qbuf, sizeof(qbuf)) == ESP_OK) {
            if (httpd_query_key_value(qbuf, "address", param, sizeof(param)) == ESP_OK)
                addr = atoi(param);
        }
    }
    if (addr <= 0 || addr > 247) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"alive\":false,\"used_fc\":0,\"exception\":false,\"error\":\"invalid_address\"}");
        return ESP_OK;
    }

    // Se o AP estiver suspenso, responda sem acessar o barramento
    if (!wifi_ap_is_running()) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"alive\":false,\"used_fc\":0,\"exception\":false,\"error\":\"ap_suspended\"}");
        return ESP_OK;
    }

    TickType_t now = xTaskGetTickCount();
    ping_cache2_t *C = &s_ping2[addr];

    // Circuit-breaker por backoff
    if (C->backoff_until && now_before(now, C->backoff_until)) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"alive\":false,\"used_fc\":0,\"exception\":false,\"error\":\"backoff\"}");
        return ESP_OK;
    }

    // Rate limit por endereço e global
    if ((C->ts && (now - C->ts) < PER_ADDR_MIN) || (s_global_last && (now - s_global_last) < GLOBAL_MIN)) {
        // devolve cache
        cJSON *root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "alive", C->alive);
        cJSON_AddNumberToObject(root, "used_fc", C->fc);
        cJSON_AddBoolToObject(root, "exception", false);
        char *json = cJSON_PrintUnformatted(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, json ? json : "{\"alive\":false}");
        free(json); cJSON_Delete(root);
        return ESP_OK;
    }

    // Evita reentrância: se já tem ping em voo para esse endereço, devolve cache
    if (C->inflight) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "alive", C->alive);
        cJSON_AddNumberToObject(root, "used_fc", C->fc);
        cJSON_AddBoolToObject(root, "exception", false);
        char *json = cJSON_PrintUnformatted(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, json ? json : "{\"alive\":false}");
        free(json); cJSON_Delete(root);
        return ESP_OK;
    }

    C->inflight = true;
    s_global_last = now;

 //   (void) modbus_master_init();

/*    bool alive = false; uint8_t used_fc = 0x00;
    esp_err_t ret = modbus_master_ping((uint8_t)addr, &alive, &used_fc);//Temporario*/
      
    bool alive = false; uint8_t used_fc = 0x00; esp_err_t ret = ESP_OK;
    MB_SESSION_WITH(pdMS_TO_TICKS(300)) {
        ret = modbus_master_ping((uint8_t)addr, &alive, &used_fc);
    }  
      
    C->ts = xTaskGetTickCount(); C->alive = alive; C->fc = used_fc; C->err = ret;
    C->inflight = false;

    // Circuit-breaker simples: se falhar 5x seguidas, faz backoff de 3 s
    if (ret != ESP_OK || !alive) {
        if (C->fails < 250) C->fails++;
        if (C->fails >= 5) {
            C->backoff_until = C->ts + pdMS_TO_TICKS(3000);
        }
    } else {
        C->fails = 0;
        C->backoff_until = 0;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "alive", alive);
    cJSON_AddNumberToObject(root, "used_fc", used_fc);
    cJSON_AddBoolToObject(root, "exception", false);
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json ? json : "{\"alive\":false}");
    free(json); cJSON_Delete(root);
    return ESP_OK;
}


// ===== handler: GET /rs485ConfigDelete?channel=X&address=Y =====
static esp_err_t rs485_config_delete_handler(httpd_req_t *req)
{
    update_last_interaction();

    // --- Parse de query ---
    char qbuf[64], param[16];
    int ch = -1, addr = -1;
    int qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen > 1 && qlen < (int)sizeof(qbuf)) {
        if (httpd_req_get_url_query_str(req, qbuf, sizeof(qbuf)) == ESP_OK) {
            if (httpd_query_key_value(qbuf, "channel", param, sizeof(param)) == ESP_OK) ch = atoi(param);
            if (httpd_query_key_value(qbuf, "address", param, sizeof(param)) == ESP_OK) addr = atoi(param);
        }
    }
    if (ch < 0 || addr <= 0 || addr > 247) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"missing_or_invalid_params\"}");
        return ESP_OK;
    }
    
    ESP_LOGI("RS485_REG_GLUE", "DELETE pedido: channel=%d address=%d", ch, addr);

    // --- Carrega binário e filtra fora o alvo ---
    sensor_map_t map[RS485_MAX_SENSORS] = {0};
    size_t count = 0;
    (void) load_rs485_config(map, &count);
    
    ESP_LOGI("RS485_REG_GLUE", "DELETE: carregado count=%u", (unsigned)count);
for (size_t i = 0; i < count; ++i) {
    ESP_LOGI("RS485_REG_GLUE", "ATUAL[%u]: ch=%u addr=%u type='%s' subtype='%s'",
             (unsigned)i, map[i].channel, map[i].address, map[i].type, map[i].subtype);
}

    sensor_map_t out[RS485_MAX_SENSORS] = {0};
    size_t wr = 0, removed = 0;
    for (size_t i = 0; i < count; ++i) {
        if (map[i].channel == (uint8_t)ch && map[i].address == (uint8_t)addr) {
            removed++;
            continue; // pula (remove)
        }
        out[wr++] = map[i];
    }

    esp_err_t ret = save_rs485_config(out, wr);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save_failed");
        return ret;
    }
    ESP_LOGI("RS485_REG_GLUE", "DELETE: remaining=%u (antes de salvar)", (unsigned)wr);
for (size_t i = 0; i < wr; ++i) {
    ESP_LOGI("RS485_REG_GLUE", "REMAIN[%u]: ch=%u addr=%u type='%s' subtype='%s'",
             (unsigned)i, out[i].channel, out[i].address, out[i].type, out[i].subtype);
}

    // --- Atualiza o JSON do front (se existir) removendo o item ---
    FILE *fj = fopen(RS485_MAP_UI_PATH, "rb");
    cJSON *ui_root = NULL, *ui_arr = NULL;
    if (fj) {
        fseek(fj, 0, SEEK_END);
        long sz = ftell(fj);
        rewind(fj);
        if (sz > 0 && sz < 8192) {
            char *buf = (char*)malloc((size_t)sz + 1);
            if (buf) {
                size_t n = fread(buf, 1, (size_t)sz, fj);
                buf[n] = '\0';
                ui_root = cJSON_Parse(buf);
                free(buf);
            }
        }
        fclose(fj);
    }
    if (!ui_root) ui_root = cJSON_CreateObject();
    ui_arr = cJSON_GetObjectItem(ui_root, "sensors");
    if (!cJSON_IsArray(ui_arr)) {
        ui_arr = cJSON_CreateArray();
        cJSON_AddItemToObject(ui_root, "sensors", ui_arr);
    }

    // reconstroi o array sem o alvo
    cJSON *new_arr = cJSON_CreateArray();
    for (cJSON *it = ui_arr->child; it; it = it->next) {
        cJSON *uch = cJSON_GetObjectItem(it, "channel");
        cJSON *uad = cJSON_GetObjectItem(it, "address");
        if (cJSON_IsNumber(uch) && cJSON_IsNumber(uad) &&
            uch->valueint == ch && uad->valueint == addr) {
            removed += 0; // já contamos lá em cima; aqui só ignora no JSON
            continue;
        }
        cJSON_AddItemToArray(new_arr, cJSON_Duplicate(it, /* recurse */ 1));
    }
    cJSON_ReplaceItemInObject(ui_root, "sensors", new_arr);

    // Persiste JSON
    char *ui_json = cJSON_PrintUnformatted(ui_root);
    if (ui_json) {
        FILE *fw = fopen(RS485_MAP_UI_PATH, "wb");
        if (fw) {
            fwrite(ui_json, 1, strlen(ui_json), fw);
            fclose(fw);
        }
        free(ui_json);
    }
    cJSON_Delete(ui_root);
    
    ESP_LOGI("RS485_REG_GLUE", "DELETE OK: removed=%u remaining=%u",
         (unsigned)removed, (unsigned)wr);

    // --- Resposta ---
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddNumberToObject(resp, "removed", (double)removed);
    cJSON_AddNumberToObject(resp, "remaining", (double)wr);
    char *out_json = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out_json ? out_json : "{\"ok\":true}");
    free(out_json);
    cJSON_Delete(resp);
    return ESP_OK;
}

#endif
//--------------------------------------------------------------------
static esp_err_t config_maintenance_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "--- INÍCIO /configMaintGet ---");
    
   // update_last_interaction();
   update_last_interaction_background();
    
        // Montagem do JSON de resposta normal
cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Falha ao criar objeto JSON");
        // Retorna JSON de erro em vez de plain-text
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "erro", "Falha ao criar JSON");
        const char *s = cJSON_PrintUnformatted(err);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, s);
        free((void*)s);
        cJSON_Delete(err);
        return ESP_FAIL;
    }
    
       // ===== PATCH: parsing híbrido JSON-like ou key=value =====
    char buf[128];
    char sensor_param[10] = "1";
    char unit_param[10]   = "bar";
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1 && httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
        ESP_LOGI(TAG, "Query string bruta: %s", buf);

        // 1) Tenta parse JSON-like {"sensor":"2","unit":"bar"}
        char *json = buf;
        char *s_start = strstr(json, "\"sensor\":\"");
        if (s_start) {
            s_start += strlen("\"sensor\":\"");
            char *s_end = strchr(s_start, '"');
            if (s_end) {
                size_t len = s_end - s_start;
                if (len < sizeof(sensor_param)) strncpy(sensor_param, s_start, len);
            }
        }
        char *u_start = strstr(json, "\"unit\":\"");
        if (u_start) {
            u_start += strlen("\"unit\":\"");
            char *u_end = strchr(u_start, '"');
            if (u_end) {
                size_t len = u_end - u_start;
                if (len < sizeof(unit_param)) strncpy(unit_param, u_start, len);
            }
        }

        // 2) Se não era JSON-like, faz fallback em key=value
        if (!s_start) {
            char *p = strstr(buf, "sensor=");
            if (p) {
                p += strlen("sensor=");
                sscanf(p, "%9[^&]", sensor_param);
            }
        }
        if (!u_start) {
            char *p = strstr(buf, "unit=");
            if (p) {
                p += strlen("unit=");
                sscanf(p, "%9[^&]", unit_param);
            }
        }
        ESP_LOGI(TAG, "sensor_param = %s, unit_param = %s", sensor_param, unit_param);
    }
    // ===== FIM PATCH =====

    // Determinar sensor e unidade
    sensor_t selected_sensor = (strcmp(sensor_param,"2")==0) ? analog_2 : analog_1;
    pressure_unit_t selected_unit;
    if      (strcmp(unit_param,"psi")==0) selected_unit = PRESSURE_UNIT_PSI;
    else if (strcmp(unit_param,"mca")==0) selected_unit = PRESSURE_UNIT_MCA;
    else                                   selected_unit = PRESSURE_UNIT_BAR;
    ESP_LOGI(TAG, "selected_sensor = %s, selected_unit = %d",
             selected_sensor==analog_1 ? "analog_1" : "analog_2",
             selected_unit);

    // ===== PATCH: atualiza globais e testa sensor desconectado =====
    sensor_em_calibracao  = selected_sensor;
    unidade_em_calibracao = selected_unit;

    if (has_calibration()) {
        float test_value = oneshot_analog_read(sensor_em_calibracao);
        ESP_LOGI(TAG, "Teste antes de ativar: tensão lida = %.3f V", test_value);
        if (test_value < SENSOR_DISCONNECTED_THRESHOLD) {
            enable_calibration(false);
            ESP_LOGW(TAG, "Sensor %s desconectado → desativando calibração.",
                     selected_sensor==analog_1 ? "1" : "2");
            cJSON *resp_err = cJSON_CreateObject();
            cJSON_AddBoolToObject(resp_err, "ativar_cali", has_calibration());
            cJSON_AddStringToObject(resp_err, "erro", "Sensor desconectado");
            const char *err_str = cJSON_PrintUnformatted(resp_err);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, err_str);
            cJSON_Delete(resp_err);
            free((void*)err_str);
            return ESP_OK;
        }
    }
    
    cJSON_AddBoolToObject(root, "ativar_cali", has_calibration());

    if (has_calibration()) {
		update_last_interaction();
        bool sensor_ok = false;
        
               float pressure = get_calibrated_pressure(sensor_em_calibracao,
                                                unidade_em_calibracao,
                                                &sensor_ok);
        ESP_LOGI(TAG, ">>>>>get_calibrated_pressure pressure=%.3f, sensor_ok=%d",
                 pressure, sensor_ok?1:0);

        if (sensor_ok) {
			int32_t inteira = (int32_t) pressure;
            int32_t frac    = (int32_t) ((pressure - (float)inteira) * 1000.0f + 0.5f);
            char bufp[16];
            snprintf(bufp, sizeof(bufp), "%d.%03d", inteira, frac);
            ESP_LOGI(TAG, "bufp formatado = '%s'\n", bufp);
            cJSON_AddStringToObject(root, "sensor_selected_value", bufp);
        } else {
            cJSON_AddStringToObject(root, "sensor_selected_value", "[Sem leitura]");
        }
    } else {
        cJSON_AddStringToObject(root, "sensor_selected_value", "[Desativado]");
    }

    const char *response = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG, "++++++>DEBUG JSON GET response: %s", response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    free((void*)response);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "--- FIM /configMaintGet ---");
    return ESP_OK;
}

//--------------------------------------------------------------------
// New config operation calibration
//--------------------------------------------------------------------
static esp_err_t config_maintenance_post_handler(httpd_req_t *req)
{
    update_last_interaction();

    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    int total_len = req->content_len;
    int cur_len = 0;
    int received = 0;

    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }

    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
if (root == NULL) {
        ESP_LOGE(TAG, "configMaintSave: JSON invÃ¡lido em cJSON_Parse");
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "erro", "JSON inválido");
        const char *s = cJSON_PrintUnformatted(err);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, s);
        free((void*)s);
        cJSON_Delete(err);
        return ESP_FAIL;
    }
        // pega o campo ativar_cali (se nÃ£o existir, assume false)
    cJSON *item = cJSON_GetObjectItem(root, "ativar_cali");
    bool enable = (item != NULL && cJSON_IsTrue(item));
        // atualiza o flag permanente
    enable_calibration(enable);
    ESP_LOGI(TAG, "configMaintSave: enable_calibration(%d)", enable);
    
    if (enable) {
    activate_mosfet(enable_analog_sensors);
     }
     else{
		 activate_mosfet(disable_analog_sensors);
		 }
   // ===== NOVO PATCH =====
    // Primeiro, se o JSON tiver o campo "sensor", atualizamos sensor_em_calibracao
    if (cJSON_HasObjectItem(root, "sensor")) {
        int sensor_val = cJSON_GetObjectItem(root, "sensor")->valueint;
        if (sensor_val == 1) {
            sensor_em_calibracao = analog_1;
        } else if (sensor_val == 2) {
            sensor_em_calibracao = analog_2;
        }
    }
    
if (has_calibration()) {
    // Verifica se o sensor em calibração está OK
    float test_value = oneshot_analog_read(sensor_em_calibracao);
    if (test_value < SENSOR_DISCONNECTED_THRESHOLD) {
        enable_calibration(false); // Força desativação da calibração
        ESP_LOGW(TAG, "Tentativa de ativar calibração sem sensor conectado.");
        // [Opcional] envie erro para o front (veja abaixo)
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "ativar_cali", false);
        cJSON_AddStringToObject(resp, "erro", "Sensor desconectado");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, cJSON_PrintUnformatted(resp));
        cJSON_Delete(resp);
        cJSON_Delete(root);
        return ESP_OK;
    }
}

// Se existir campo "sensor", atualiza o sensor selecionado
/*if (cJSON_HasObjectItem(root, "sensor")) {
    int sensor_val = cJSON_GetObjectItem(root, "sensor")->valueint;
    if (sensor_val == 1) {
        sensor_em_calibracao = analog_1;
    } else if (sensor_val == 2) {
        sensor_em_calibracao = analog_2;
    }
}
*/
// Se existir campo "unit", atualiza a unidade selecionada
if (cJSON_HasObjectItem(root, "unit")) {
    const char* unit_str = cJSON_GetObjectItem(root, "unit")->valuestring;
    if (strcmp(unit_str, "bar") == 0) {
        unidade_em_calibracao = PRESSURE_UNIT_BAR;
    } else if (strcmp(unit_str, "psi") == 0) {
        unidade_em_calibracao = PRESSURE_UNIT_PSI;
    }
}

    cJSON *s1_refs = cJSON_GetObjectItem(root, "sensor1_refs");
    cJSON *s2_refs = cJSON_GetObjectItem(root, "sensor2_refs");

    reference_point_t ref_sensor1[NUM_CAL_POINTS] = {0};
    reference_point_t ref_sensor2[NUM_CAL_POINTS] = {0};

    // ---------------------------
    // Sensor 1
    // ---------------------------
    if (s1_refs && cJSON_IsObject(s1_refs)) {
        cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, s1_refs) {
            const char *key = entry->string;
            cJSON *obj = entry;

            if (obj && key) {
                float ref = atof(key);
                float raw_val = oneshot_analog_read(analog_1);
                const char *unit = cJSON_GetObjectItem(obj, "unit")->valuestring;
                int idx = (int)(ref / 5);

                if (idx >= 0 && idx < NUM_CAL_POINTS) {
                    ref_sensor1[idx].ref_value = ref;
                    ref_sensor1[idx].real_value = raw_val;
                    strncpy(ref_sensor1[idx].unit, unit, sizeof(ref_sensor1[idx].unit));

                    ESP_LOGI(TAG, "[Sensor1] idx %d: ref=%.2f, raw=%.4f, unit=%s", idx, ref, raw_val, unit);
                }
            }
        }

        esp_err_t err = save_reference_points_sensor1(ref_sensor1);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Erro ao salvar dados sensor1 na NVS: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Dados de calibração sensor1 salvos na NVS");
        }
    }

    // ---------------------------
    // Sensor 2
    // ---------------------------
    if (s2_refs && cJSON_IsObject(s2_refs)) {
        cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, s2_refs) {
            const char *key = entry->string;
            cJSON *obj = entry;

            if (obj && key) {
                float ref = atof(key);
                float raw_val = oneshot_analog_read(analog_2);
                const char *unit = cJSON_GetObjectItem(obj, "unit")->valuestring;
                int idx = (int)(ref / 5);

                if (idx >= 0 && idx < NUM_CAL_POINTS) {
                    ref_sensor2[idx].ref_value = ref;
                    ref_sensor2[idx].real_value = raw_val;
                    strncpy(ref_sensor2[idx].unit, unit, sizeof(ref_sensor2[idx].unit));

                    ESP_LOGI(TAG, "[Sensor2] idx %d: ref=%.2f, raw=%.4f, unit=%s", idx, ref, raw_val, unit);
                }
            }
        }

        esp_err_t err = save_reference_points_sensor2(ref_sensor2);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Erro ao salvar dados sensor2 na NVS: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Dados de calibração sensor2 salvos na NVS");
        }
    }

    cJSON_Delete(root);
    httpd_resp_sendstr(req, "{\"status\": \"success\"}");
    return ESP_OK;
}

static esp_err_t rele_activate(httpd_req_t *req)
{
    update_last_interaction();
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Falha ao criar objeto JSON");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Erro interno");
        return ESP_FAIL;
    }

    if (req->method == HTTP_GET) {
        cJSON_AddBoolToObject(root, "active", rele_state);
        cJSON_AddStringToObject(root, "status", "success");
    } else if (req->method == HTTP_POST) {
        int total_len = req->content_len;
        int cur_len = 0;
        char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
        int received = 0;
        if (total_len >= SCRATCH_BUFSIZE) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
            return ESP_FAIL;
        }
        while (cur_len < total_len) {
            received = httpd_req_recv(req, buf + cur_len, total_len);
            if (received <= 0) {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
                return ESP_FAIL;
            }
            cur_len += received;
        }
        buf[total_len] = '\0';

        cJSON *post_data = cJSON_Parse(buf);
        if (!post_data) {
            ESP_LOGE(TAG, "Falha ao parsear JSON");
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON inválido");
            return ESP_FAIL;
        }

        cJSON *active_item = cJSON_GetObjectItem(post_data, "active");
        if (active_item) {
            rele_state = cJSON_IsTrue(active_item);
            if (rele_state) {
                rele_turn_on();
            } else {
                rele_turn_off();
            }
        }
        cJSON_Delete(post_data);

        cJSON_AddBoolToObject(root, "active", rele_state);
        cJSON_AddStringToObject(root, "status", "success");
    } else {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "Método não permitido");
        return ESP_FAIL;
    }

    const char *response = cJSON_PrintUnformatted(root);
    if (!response) {
        ESP_LOGE(TAG, "Falha ao formatar JSON");
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Erro interno");
        return ESP_FAIL;
    }
    httpd_resp_sendstr(req, response);
    free((void *)response);
    cJSON_Delete(root);
    return ESP_OK;
}

//-----------------------------------------------

/**
 * Saída unificada do front:
 * - STA ativo  → silencia o AP por N segundos (ele volta sozinho depois).
 * - STA inativo→ inicia deep sleep.
 * Também registra a interação para evitar retrigger imediato do timeout.
 */
 
static inline void notify_timemanager_factory_off(void)
{
    Send_FactoryControl_Task_ON = false;
    if (xQueue_Factory_Control) {
        bool off = false;                          // o TimeManager espera "false"
        (void)xQueueSend(xQueue_Factory_Control, &off, 0);
    }
}

static void shutdown_task(void* pvParameters) {

    user_initiated_exit = true;
  
            // Stop Factory server
    stop_factory_server();
    printf("Servidor HTTP parado.\n");
    
     sleep_prepare(true);                          
     printf(">>>>>>>>>Shutdown finished<<<<<<<<\n");
     notify_timemanager_factory_off();   // fila para TimeManager dormir
     vTaskDelay(pdMS_TO_TICKS(300));
     vTaskDelete(NULL);
}

static void factory_exit_common(uint32_t ap_silence_secs)
{
    user_initiated_exit = true;
    clear_display();
    vTaskDelay(pdMS_TO_TICKS(150));
    save_system_config_data_time();
    
//    if (has_activate_sta()) {
	  if (has_always_on()) {
		wifi_ap_suspend_temporarily(ap_silence_secs);
        ESP_LOGI(TAG, "SaÃ­da: STA ativo â†’ suspendendo AP por %u s", (unsigned)ap_silence_secs);
        // >>> NÃ£o toque em ap_active nem chame esp_wifi_set_mode() aqui <<<
        
    } else {
        ESP_LOGI(TAG, "Saída: STA inativo → deep sleep");
        xTaskCreate(shutdown_task, "shutdown_task", 6144, NULL, 5, NULL);
        deinit_factory_task();
    } 
    
     if (first_factory_setup && has_factory_config()) {
        ESP_LOGI(TAG, "### RESTART ###");
        vTaskDelay(pdMS_TO_TICKS(50));
        first_factory_setup = false;
        esp_restart();                 // ← por último
    }  
}

static esp_err_t exit_device_post_handler(httpd_req_t *req)
{
printf(">>>>>>EXIT<<<<<<<\n");
    if (has_activate_sta()&&has_always_on()) {
        httpd_resp_send(req, "AP desconectado, STA mantido", HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send(req, "Dispositivo entrando em deep sleep", HTTPD_RESP_USE_STRLEN);
    }

    // Fecha a sessão HTTP antes de acionar a saída
    httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
    vTaskDelay(pdMS_TO_TICKS(50));

    // Marca saída iniciada pelo usuário (útil para lógica de "uma vez só" no Wi-Fi)
     user_initiated_exit = true;
    // Usa o caminho unificado com suspensão condicional do AP
    factory_exit_common(compute_ap_silence_secs());

    return ESP_OK;
}


// Função separada para parar o servidor HTTP (chamada em outro contexto se necessário)
void stop_http_server(httpd_handle_t server)
{
    if (server != NULL) {
        printf("Parando servidor HTTP em task separada...\n");
        esp_err_t err = httpd_stop(server);
        if (err != ESP_OK) {
            printf("Erro ao parar httpd: %s\n", esp_err_to_name(err));
        } else {
            printf("Servidor HTTP parado.\n");
        }
    }
}

static esp_err_t config_login_post_handler(httpd_req_t *req)
{
    update_last_interaction();
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if(strcmp(cJSON_GetObjectItem(root, "key")->valuestring, SUPER_USER_KEY) == 0)
    {
        super_user_logged = true;
    }
    cJSON_Delete(root);
    if(super_user_logged)
    {
        httpd_resp_sendstr(req, "{\"login\":true}");
    }
    else
    {
        httpd_resp_sendstr(req, "{\"login\":false}");
    }
    

    return ESP_OK;
}

static esp_err_t config_device_get_handler(httpd_req_t *req)
{
	time_t system_time = time(&system_time);
	
    update_last_interaction();
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Falha ao criar objeto JSON");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Erro interno");
        return ESP_FAIL;
    }
    cJSON_AddStringToObject(root, "id", get_device_id());
    cJSON_AddStringToObject(root, "name", get_name());
    cJSON_AddStringToObject(root, "phone", get_phone());
    cJSON_AddStringToObject(root, "ssid_ap", get_ssid_ap());
    cJSON_AddStringToObject(root, "wifi_pw_ap", get_password_ap());
    if(has_activate_sta())
    {
        cJSON_AddTrueToObject(root, "activate_sta");
    }
    else
    {
        cJSON_AddFalseToObject(root, "activate_sta");
    }
    cJSON_AddStringToObject(root, "ssid_sta", get_ssid_sta());
    cJSON_AddStringToObject(root, "wifi_pw_sta", get_password_sta());
    
   // 1) Flags de qual checkbox deve vir marcado
   cJSON_AddBoolToObject(root, "activate_send_freq_mode", is_send_mode_freq());
   cJSON_AddBoolToObject(root, "activate_send_time_mode", is_send_mode_time());

   // 2) Sempre envie o campo send_mode
   cJSON_AddStringToObject(root, "send_mode", get_send_mode());

   // 3) Sempre envie o send_period
   cJSON_AddNumberToObject(root, "send_period", get_send_period());

   // 4) Sempre envie o array de horários (mesmo que zeros)
   {
     int times[4] = {
       get_send_time1(), get_send_time2(),
       get_send_time3(), get_send_time4()
     };
     cJSON *arr = cJSON_CreateIntArray(times, 4);
     cJSON_AddItemToObject(root, "send_times", arr);
   } 
    
    
    
   // 1) Modo de envio (único campo send_mode)
/*cJSON_AddStringToObject(root, "send_mode", get_send_mode());               // adiciona "freq" ou "time"
if (is_send_mode_freq()) {                                               // equivalente a send_mode == "freq"
    cJSON_AddNumberToObject(root, "send_period", get_send_period());
}
else if (is_send_mode_time()) {                                           // equivalente a send_mode == "time"
    int times[4] = {
        get_send_time1(),
        get_send_time2(),
        get_send_time3(),
        get_send_time4()
    };
    cJSON *arr = cJSON_CreateIntArray(times, 4);
    cJSON_AddItemToObject(root, "send_times", arr);
}*/

 //   cJSON_AddNumberToObject(root, "send_period", get_send_period());
     
    cJSON_AddNumberToObject(root, "deep_sleep_period", get_deep_sleep_period());
   
    if(should_save_pulse_zero())
    {
        cJSON_AddTrueToObject(root, "save_pulse_zero");
    }
    else
    {
        cJSON_AddFalseToObject(root, "save_pulse_zero");
    }
    cJSON_AddNumberToObject(root, "scale", get_scale());
    cJSON_AddNumberToObject(root, "flow_rate", get_flow_rate());
    
    if (system_time>TIME_REFERENCE)                                   //Se o tempo de sistema for maior que Janeiro de 2021
    {
    cJSON_AddStringToObject(root, "date", get_date());
    cJSON_AddStringToObject(root, "time", get_time());
    }

    
    if(has_factory_config())
    {
        cJSON_AddTrueToObject(root, "finished_factory");
    }
    else
    {
        cJSON_AddFalseToObject(root, "finished_factory");
    }
    
        
    if(has_always_on())
    {
        cJSON_AddTrueToObject(root, "always_on");
    }
    else
    {
        cJSON_AddFalseToObject(root, "always_on");
    }
    
    if(has_device_active())
    {
        cJSON_AddTrueToObject(root, "device_active");
    }
    else
    {
        cJSON_AddFalseToObject(root, "device_active");
    }
    
   
/*    if(should_send_value())
    {
        cJSON_AddTrueToObject(root, "send_value");
    }
    else
    {
        cJSON_AddFalseToObject(root, "send_value");
    }*/

 //Modificação por sugestão do Krok3   
    const char *device_config = cJSON_PrintUnformatted(root);
        if (!device_config) {
        ESP_LOGE(TAG, "Falha ao formatar JSON");
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Erro interno");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
     esp_err_t err = httpd_resp_sendstr(req, device_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao enviar resposta: %s", esp_err_to_name(err));
        free((void *)device_config);
        cJSON_Delete(root);
        return err;
    }
    
    free((void *)device_config);
    cJSON_Delete(root);
    return ESP_OK;
}
//================================================
static void update_sta_status_task(void *pvParameters) {
    while (1) {
        wifi_mode_t mode;
        wifi_ap_record_t ap_info;
        esp_err_t err = esp_wifi_get_mode(&mode);
        if (err == ESP_OK && (mode & WIFI_MODE_STA)) {
            err = esp_wifi_sta_get_ap_info(&ap_info);
            if (err == ESP_OK) {
                sta_connected = true;
 //               strncpy(sta_ssid, (char *)ap_info.ssid, sizeof(sta_ssid));
            } else {
                sta_connected = false;
                memset(sta_ssid, 0, sizeof(sta_ssid));
            }
        } else {
            sta_connected = false;
            memset(sta_ssid, 0, sizeof(sta_ssid));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static esp_err_t status_sta_get_handler(httpd_req_t *req) {
    printf("Requisição GET em /status_sta recebida\n");

    // Consulta direta ao driver: conectado se esp_wifi_sta_get_ap_info == ESP_OK
    wifi_ap_record_t ap;
    bool connected = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);

    char response[128];
    if (connected) {
        const char *ssid = get_ssid_sta();           // já existe no seu projeto
        if (!ssid || !ssid[0]) ssid = (const char *)ap.ssid;  // fallback do driver
        snprintf(response, sizeof(response),
                 "{\"sta_connected\": true, \"ssid\": \"%s\"}", ssid);
    } else {
        snprintf(response, sizeof(response), "{\"sta_connected\": false}");
    }

    ESP_LOGI(TAG, "Resposta enviada: %s", response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t connect_sta_post_handler(httpd_req_t *req) {
    char buf[512];
    int ret;

    printf("Requisição POST em /connect_sta recebida\n");
    ESP_LOGI(TAG, "Requisição POST em /connect_sta recebida, content_len: %d", req->content_len);

    if (req->content_len > sizeof(buf) - 1) {
        ESP_LOGE(TAG, "Content-Length excede buffer");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Dados muito grandes");
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) {
        ESP_LOGE(TAG, "Erro ao receber dados");
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    ESP_LOGI(TAG, "Dados recebidos: %s", buf);

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGE(TAG, "Erro ao parsear JSON");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON inválido");
        return ESP_FAIL;
    }

    cJSON *disconnect = cJSON_GetObjectItem(root, "disconnect");
    if (disconnect && cJSON_IsTrue(disconnect)) {
        ESP_LOGI(TAG, "Desconectando STA...");
//        sta_intentional_disconnect = true;
        wifi_sta_mark_intentional_disconnect(true);
        set_activate_sta(false); // Atualiza dev_config e NVS
        esp_err_t err = esp_wifi_disconnect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Erro ao desconectar STA: %s", esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Erro ao desconectar Wi-Fi");
            cJSON_Delete(root);
            return ESP_FAIL;
        }
        httpd_resp_send(req, "{\"status\": \"Desconectado\"}", HTTPD_RESP_USE_STRLEN);
    } else {
        cJSON *ssid_json = cJSON_GetObjectItem(root, "ssid");
        cJSON *pass_json = cJSON_GetObjectItem(root, "password");
        if (!ssid_json || !pass_json || !cJSON_IsString(ssid_json) || !cJSON_IsString(pass_json)) {
            ESP_LOGE(TAG, "Campos SSID ou password ausentes/inválidos");
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Dados inválidos");
            return ESP_FAIL;
        }

//        strncpy(sta_ssid, ssid_json->valuestring, sizeof(sta_ssid) - 1);
//        strncpy(sta_password, pass_json->valuestring, sizeof(sta_password) - 1);
        set_ssid_sta(ssid_json->valuestring); // Grava em dev_config
        set_password_sta(pass_json->valuestring); // Grava em dev_config
         ESP_LOGI(TAG, "Recebido SSID: '%s'", get_ssid_sta());

        wifi_config_t wifi_config = {
            .sta = {
                .ssid = {0},
                .password = {0},
                .scan_method = WIFI_ALL_CHANNEL_SCAN,
                .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            },
        };
 //       strncpy((char *)wifi_config.sta.ssid, sta_ssid, sizeof(wifi_config.sta.ssid) - 1);
//        strncpy((char *)wifi_config.sta.password, sta_password, sizeof(wifi_config.sta.password) - 1);
        strncpy((char *)wifi_config.sta.ssid, get_ssid_sta(), sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, get_password_sta(), sizeof(wifi_config.sta.password) - 1);

        esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Erro ao configurar STA: %s", esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Erro ao configurar Wi-Fi");
            cJSON_Delete(root);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Configuração STA aplicada, tentando conectar...");
//        sta_intentional_disconnect = false;
        wifi_sta_mark_intentional_disconnect(false);
        set_activate_sta(true);
        err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Erro ao conectar STA: %s", esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Erro ao conectar Wi-Fi");
            cJSON_Delete(root);
            return ESP_FAIL;
        }
        httpd_resp_send(req, "{\"status\": \"Tentando conectar\"}", HTTPD_RESP_USE_STRLEN);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

//================================================
static esp_err_t config_network_post_handler(httpd_req_t *req)
{
    update_last_interaction();
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    ESP_LOGI(TAG, "Network: %s", buf);
    set_apn(cJSON_GetObjectItem(root, "apn")->valuestring);
    set_lte_user(cJSON_GetObjectItem(root, "lte_user")->valuestring);
    set_lte_pw(cJSON_GetObjectItem(root, "lte_pw")->valuestring);
    enable_network_http(cJSON_IsTrue(cJSON_GetObjectItem(root, "http_enable")));
    set_data_server_url(cJSON_GetObjectItem(root, "data_server_url")->valuestring);
    set_data_server_port(cJSON_GetObjectItem(root, "data_server_port")->valueint);
    set_data_server_path(cJSON_GetObjectItem(root, "data_server_path")->valuestring);
    set_network_user(cJSON_GetObjectItem(root, "user")->valuestring);
    set_network_token(cJSON_GetObjectItem(root, "token")->valuestring);
    set_network_pw(cJSON_GetObjectItem(root, "pw")->valuestring);
    enable_network_user(cJSON_IsTrue(cJSON_GetObjectItem(root, "user_en")));
    enable_network_token(cJSON_IsTrue(cJSON_GetObjectItem(root, "token_en")));
    enable_network_pw(cJSON_IsTrue(cJSON_GetObjectItem(root, "pw_en")));
    enable_network_mqtt(cJSON_IsTrue(cJSON_GetObjectItem(root, "mqtt_enable")));
    set_mqtt_url(cJSON_GetObjectItem(root, "mqtt_url")->valuestring);
    set_mqtt_port(cJSON_GetObjectItem(root, "mqtt_port")->valueint);
    set_mqtt_topic(cJSON_GetObjectItem(root, "mqtt_topic")->valuestring);
    
    cJSON_Delete(root);
    httpd_resp_sendstr(req, "Post control value successfully");
    save_config();
    return ESP_OK;
}

static esp_err_t config_network_get_handler(httpd_req_t *req)
{
    update_last_interaction();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "apn", get_apn());
    cJSON_AddStringToObject(root, "lte_user", get_lte_user());
    cJSON_AddStringToObject(root, "lte_pw", get_lte_pw());  
    cJSON_AddStringToObject(root, "data_server_url", get_data_server_url());
    cJSON_AddNumberToObject(root, "data_server_port", get_data_server_port());
    cJSON_AddStringToObject(root, "data_server_path", get_data_server_path());
    cJSON_AddStringToObject(root, "mqtt_topic", get_mqtt_topic());
    cJSON_AddStringToObject(root, "user", get_network_user());
    cJSON_AddStringToObject(root, "token", get_network_token());
    cJSON_AddStringToObject(root, "pw", get_network_pw());
    cJSON_AddStringToObject(root, "mqtt_url", get_mqtt_url());
    cJSON_AddNumberToObject(root, "mqtt_port", get_mqtt_port());
    cJSON_AddStringToObject(root, "mqtt_topic", get_mqtt_topic());
    
    if(has_network_http_enabled())
    {
        cJSON_AddTrueToObject(root, "http_enable");
    }
    else
    {
        cJSON_AddFalseToObject(root, "http_enable");
     }
     
    if(has_network_token_enabled())
    {
        cJSON_AddTrueToObject(root, "token_en");
    }
    else
    {
        cJSON_AddFalseToObject(root, "token_en");
    }

    if(has_network_user_enabled())
    {
        cJSON_AddTrueToObject(root, "user_en");
    }
    else
    {
        cJSON_AddFalseToObject(root, "user_en");
    }

    if(has_network_pw_enabled())
    {
        cJSON_AddTrueToObject(root, "pw_en");
    }
    else
    {
        cJSON_AddFalseToObject(root, "pw_en");
    }
    
    if(has_network_mqtt_enabled())
    {
        cJSON_AddTrueToObject(root, "mqtt_enable");
    }
    else
    {
        cJSON_AddFalseToObject(root, "mqtt_enable");
     }

    const char *network_config = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, network_config);
    free((void *)network_config);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t load_registers_get_handler(httpd_req_t *req)
{
    update_last_interaction();

    char*  buf;
    size_t buf_len;
    uint32_t value = 0;
    char file_chunk[256];

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGD(TAG, "Found URL query => %s", buf);
            char param[32];

            if (httpd_query_key_value(buf, "last_byte", param, sizeof(param)) == ESP_OK) {
                value = atoi(param);
                ESP_LOGD(TAG, "Found URL query parameter => last_byte=%d", value);
            }
        }
        free(buf);
    }

    bool end = read_record_file_sd(&value, file_chunk);
    file_chunk[255] = '\0';

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "chunk", file_chunk);
    cJSON_AddNumberToObject(root, "last_byte", value);
    if(end)
    {
        cJSON_AddTrueToObject(root, "end");
    }
    else
    {
        cJSON_AddFalseToObject(root, "end");
    }

    const char *response = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, response);
    free((void *)response);
    cJSON_Delete(root);

    return ESP_OK;
}

static esp_err_t delete_registers_get_handler(httpd_req_t *req)
{
    update_last_interaction();
   if(delete_record_sd()==ESP_OK)
     {
	  printf ("File is Deleted\n");
      return ESP_OK;	 
	 }
   else{
		return ESP_FAIL;
        }
}
//----------------------------------------------------------
// Handler para o endpoint /ping
static esp_err_t ping_handler(httpd_req_t *req)
{
#if ENABLE_PING_LOG
    ESP_LOGI("PING", "Recebido /ping do front");
#endif
    // Importante: NÃO chamar update_last_interaction() aqui.
    // Ping não deve manter a sessão "viva" artificialmente.

    bool ap_running   = wifi_ap_is_running();
    bool ap_suspended = wifi_ap_is_suspended();          // pode ser stub (false)
    uint32_t resume_in = ap_suspended ? wifi_ap_seconds_to_resume() : 0;  // pode ser stub (0)
    uint32_t sta_count = wifi_ap_get_sta_count();        // pode ser stub (0)

    char buf[160];
    int n = snprintf(buf, sizeof(buf),
        "{\"ok\":true,\"ap_running\":%s,\"ap_suspended\":%s,\"resume_in\":%u,\"sta_count\":%u}",
        ap_running ? "true" : "false",
        ap_suspended ? "true" : "false",
        resume_in,
        sta_count);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}
//----------------------------------------------------------

static void setup_modbus_guard_once(void) {
    mb_session_config_t cfg;
    mb_session_config_defaults(&cfg);
    
    cfg.init_once     = false;          // sobe/derruba master a cada sessão
    cfg.silence_level = ESP_LOG_WARN;   // reduz ruído durante a janela Modbus
    
    ESP_ERROR_CHECK(mb_session_setup(&cfg));
}

//-------------------------------------------------------------------
//      Factory Configuration Task
//-------------------------------------------------------------------
void Factory_Config_Task(void* pvParameters)
{
 	Send_FactoryControl_Task_ON= true;
 	static bool exit_already_fired = false;   // <--- [NOVO GUARD]
    xQueueSend(xQueue_Factory_Control, &Send_FactoryControl_Task_ON,/*timeout=*/0);

	start_factory_routine();

	        // Inicializar last_interaction_ticks no boot
    last_interaction_ticks = xTaskGetTickCount();
	        
 while(1)
	  {
        
    const int64_t now_us        = esp_timer_get_time();
    const int64_t last_us       = get_factory_routine_last_interaction_us();
    const int64_t diff_us       = (last_us > 0) ? (now_us - last_us) : 0;
    const int64_t timeout_us    = (int64_t)FACTORY_CONFIG_TIMER * 1000000LL;

if (fc_user_interacted_since_exit) {
    exit_already_fired = false;
    fc_user_interacted_since_exit = false;
}

 /* ====== GUARD 0: enquanto houver cliente no console TCP,
       não avaliamos rearm/timeout e mantemos AP/HTTP ativos ====== */
    if (tcp_log_has_client()) {
        if (s_single_shot_armed) {
            ESP_LOGI(TAG, "Console TCP ativo → desarmando single-shot e mantendo AP/HTTP.");
            s_single_shot_armed = false;
        }
        vTaskDelay(pdMS_TO_TICKS(250));   // respira um pouco e volta
        continue;
    }

         // 1) Se JÁ disparamos o single-shot, só rearmar quando:
    //    (a) o AP já tiver voltado, e (b) houve nova interação DEPOIS do EXIT.
    if (!s_single_shot_armed) {
        if (wifi_ap_is_running()) {
            if (last_us > s_last_exit_us) {
                s_single_shot_armed = true;
                ESP_LOGI(TAG, "Single-shot rearmado (AP ativo e nova interação).");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        continue; // enquanto desarmado, não reavalia timeout
       }

    // 2) Aguardando INATIVIDADE: dispara UMA ÚNICA VEZ
    if (Send_FactoryControl_Task_ON && diff_us >= timeout_us) {
		
	 /* ====== GUARD 1: chegou no timeout, mas há cliente TCP?
           então não sai; aguarda fechar o console ====== */
        if (tcp_log_has_client()) {
            ESP_LOGI(TAG, "Timeout atingido, MAS console TCP em uso — mantendo AP/HTTP ativos.");
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;  // não chama factory_exit_common()
        }
		#if FC_TRACE_INTERACTION
		factory_dump_last_interaction_origin();
		#endif
        const uint32_t secs = compute_ap_silence_secs(); // ou AP_SILENCE_WINDOW_S
        ESP_LOGI(TAG, "Timeout de %d s → saída unificada (single-shot AP=%u s)",
                 FACTORY_CONFIG_TIMER, (unsigned)secs);

        factory_exit_common(secs);  // ← aqui você já suspende AP só uma vez
        s_last_exit_us      = now_us;
        s_single_shot_armed = false;   // trava até AP voltar + nova interação
    }

	        	        
vTaskDelay(pdMS_TO_TICKS(1000));  
		   	          
	    }//while final
}

void init_factory_task(void)
{
	user_initiated_exit = false;  // para não bloquear religamento do AP
	start_wifi_ap_sta();
	#if CONFIG_MODBUS_ENABLE
	setup_modbus_guard_once();
	#endif
    // Inicia o console TCP (AP: 192.168.4.1:3333; em STA use o IP do roteador)
    console_tcp_enable(3333);  // 
    ESP_LOGI("SELFTEST", "Hello TCP!");
	if (Factory_Config_TaskHandle == NULL)
	   {
        xTaskCreate( Factory_Config_Task, "Factory_Config_Task", 15000, NULL, 2, &Factory_Config_TaskHandle);
       }
}

void deinit_factory_task(void)
{
  vTaskDelete(Factory_Config_TaskHandle);
  Factory_Config_TaskHandle = NULL;  
  printf("!!!Factor Finished!!!\n");	
}
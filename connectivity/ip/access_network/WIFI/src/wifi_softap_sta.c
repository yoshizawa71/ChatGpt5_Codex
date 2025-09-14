// wifi_softap_sta.c  — versão robusta AP+STA com suspensão temporária do AP e reconexão STA com backoff
// ESP-IDF v5.x
// George: marquei as adições com [NEW]; mantive seus nomes/estilo para “colar e compilar”.

#include "wifi_softap_sta.h"

#include "datalogger_control.h"
#include "datalogger_driver.h"

#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"          // [NEW] FreeRTOS software timer (STA reconexão)
#include "esp_timer.h"                // [NEW] timer one-shot (retomar AP)
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"                // [NEW]
#include "esp_wifi.h"
#include "esp_wifi_types.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "time_sync_sntp.h"   // <— novo include

/* Ajuste aqui o fuso (ou troque por valor vindo do NVS) */
#define TIME_SYNC_TZ_DEFAULT         "UTC-3"
/* Re-sincronizar a cada 6h (ajuste a gosto) */
#define TIME_SYNC_RESYNC_INTERVAL_S  (6 * 3600)


// ================== Configs ==================
#define ESP_WIFI_CHANNEL                6
#define MAX_STA_CONN                    4

#define WIFI_CONNECTED_BIT              BIT0
#define WIFI_FAIL_BIT                   BIT1

// Backoff de reconexão do STA
#define RECONNECT_BACKOFF_BASE_MS       1000U    // 1 s
#define RECONNECT_BACKOFF_CAP_MS        60000U   // 60 s

// Janela padrão de suspensão do AP após EXIT/timeout
#define AP_SUSPEND_DEFAULT_SEC          90U

// Segurança do AP (se tiver aparelho que suporte WPA3, pode subir)
#ifndef WIFI_AUTHMODE_AP
#define WIFI_AUTHMODE_AP                WIFI_AUTH_WPA2_PSK
#endif

// Se quiser habilitar fallback de provisioning oficial depois de muitas falhas:
// (lembre de adicionar o componente wifi_provisioning no CMake)
#define USE_IDF_PROVISIONING_FALLBACK   0

#if USE_IDF_PROVISIONING_FALLBACK
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"     // ou scheme_ble.h
#endif

// ================== Externs da sua base ==================
extern bool ap_active;                 // controlado/consultado pelo Factory Control
extern bool user_initiated_exit;

char *get_ssid_ap(void);
char *get_password_ap(void);
char *get_ssid_sta(void);
char *get_password_sta(void);
bool  has_activate_sta(void);

// ================== Estado interno ==================
static const char *TAG_AP  = "WiFi AP";
static const char *TAG_STA = "WiFi STA";

static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_netif_t *s_netif_ap  = NULL;
static esp_netif_t *s_netif_sta = NULL;

static volatile bool s_sta_connected = false;
static volatile bool s_sta_intentional_disconnect = false;

static int s_retry_num = 0;

// [NEW] Controle do AP
static volatile bool s_ap_should_be_on   = true;   // “o AP deve ficar disponível?”
static volatile bool s_ap_suspended_flag = false;  // estamos em janela de suspensão
static uint64_t      s_ap_suspend_until_us = 0;    // timestamp (us) do fim da suspensão

// [NEW] Timers
static TimerHandle_t     s_sta_reconn_timer   = NULL;  // FreeRTOS timer (backoff STA)
static esp_timer_handle_t s_ap_resume_timer   = NULL;  // oneshot para religar AP

static TimerHandle_t s_time_resync_timer = NULL;
static volatile bool s_time_sync_inflight = false;

static void time_sync_task(void *);                // roda SNTP fora do event-handler
static void time_resync_timer_cb(TimerHandle_t);   // callback periódico


// ================== Utils ==================
static inline uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline bool ap_suspend_window_open(void) {   // [NEW]
    if (!s_ap_suspended_flag) return false;
    return (esp_timer_get_time() < (int64_t)s_ap_suspend_until_us);
}

// ================== (Opcional) Provisioning fallback ==================
#if USE_IDF_PROVISIONING_FALLBACK
static void start_idf_provisioning_softap(void) {
    ESP_LOGW(TAG_STA, "Iniciando Wi-Fi Provisioning (SoftAP) como fallback");
    wifi_prov_mgr_config_t prov_cfg = {
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_cfg));

    bool provisioned = false;
    wifi_prov_mgr_is_provisioned(&provisioned);
    if (!provisioned) {
        wifi_prov_security_t sec = WIFI_PROV_SECURITY_1;
        const char *pop = "123456"; // TODO: troque por PoP seguro
        const char *service_name = "DLGGR-Setup";
        const char *service_key  = NULL;
        wifi_prov_mgr_start_provisioning(sec, pop, service_name, service_key);
    } else {
        wifi_prov_mgr_deinit();
    }
}
#endif

// ================== Timers callbacks ==================
// Backoff de reconexão do STA
static void sta_reconnect_timer_cb(TimerHandle_t xTimer) {   // [NEW]
    if (s_sta_intentional_disconnect) return;
    if (!has_activate_sta()) return;

    esp_err_t err = esp_wifi_connect();
    if (err == ESP_OK) {
        ESP_LOGI(TAG_STA, "Reconnecting STA...");
    } else {
        ESP_LOGW(TAG_STA, "esp_wifi_connect() falhou: %s", esp_err_to_name(err));
    }
}

// Retomar AP ao fim da suspensão
static void ap_resume_oneshot_cb(void *arg) {                   // [NEW]
    s_ap_suspended_flag = false;
    if (!s_ap_should_be_on) return;

    // só religa se ainda não estivermos em AP+STA
    wifi_mode_t m; esp_wifi_get_mode(&m);
    if (m != WIFI_MODE_AP && m != WIFI_MODE_APSTA) {
        ESP_LOGI(TAG_AP, "AP resume timer → religando AP (AP+STA)");
        wifi_ap_force_enable();
    } else if (m == WIFI_MODE_STA) {
        ESP_LOGI(TAG_AP, "AP resume timer → setando AP+STA");
        wifi_ap_force_enable();
    }
}

// ================== Event handler ==================
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {

        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG_AP, "SoftAP iniciado");
            ap_active = true;
            break;

        case WIFI_EVENT_AP_STOP: {
            ESP_LOGI(TAG_AP, "SoftAP parado (AP_STOP)");
            ap_active = false;

            // [NEW] Se o AP deveria estar ON e não estamos no “silêncio” programado, religue.
            // (antes você só religava quando STA não estava ativo; aqui religamos mesmo em STA)
            if (s_ap_should_be_on && !ap_suspend_window_open()) {
                ESP_LOGI(TAG_AP, "AP deveria estar ON → auto-restart do SoftAP");
                wifi_ap_force_enable();
            } else {
                ESP_LOGI(TAG_AP, "AP_STOP permitido (suspenso=%d, should_on=%d)",
                         (int)s_ap_suspended_flag, (int)s_ap_should_be_on);
            }
            break;
        }

        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG_AP, "Cliente conectou ao AP");
            break;

        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG_AP, "Cliente desconectou do AP");
            break;

        case WIFI_EVENT_STA_START: {
            ESP_LOGI(TAG_STA, "STA start");

            if (!has_activate_sta()) break;

            // Config do STA
            wifi_config_t wc = { 0 };
            // Scan mais abrangente
            wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
            // Threshold ajuda a evitar assoc fraca/insegura
            wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

            const char *ssid = get_ssid_sta();
            const char *pwd  = get_password_sta();
            if (ssid && pwd && strlen(ssid) > 0) {
                strncpy((char*)wc.sta.ssid,     ssid, sizeof(wc.sta.ssid)-1);
                strncpy((char*)wc.sta.password, pwd,  sizeof(wc.sta.password)-1);
                ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
                ESP_LOGI(TAG_STA, "Conectando STA ao SSID: %s", ssid);
                esp_wifi_connect();
            } else {
                ESP_LOGW(TAG_STA, "SSID/senha STA não definidos — não conectando");
            }
            break;
        }

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG_STA, "STA associado");
            s_sta_connected = true;
            s_retry_num = 0;
            s_sta_intentional_disconnect = false;
            if (s_sta_reconn_timer) xTimerStop(s_sta_reconn_timer, 0);
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t*)event_data;
            ESP_LOGW(TAG_STA, "STA desconectado, motivo=%d", d ? d->reason : -1);
            s_sta_connected = false;
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

            // Se foi intencional (parada geral) ou o STA foi desabilitado no front, não reconecta
            if (s_sta_intentional_disconnect || !has_activate_sta()) {
                ESP_LOGI(TAG_STA, "Desconexão intencional/STA desativado → sem reconexão");
                break;
            }

            // [NEW] backoff exponencial infinito (cap 60 s)
            uint32_t delay_ms = RECONNECT_BACKOFF_BASE_MS << (s_retry_num < 16 ? s_retry_num : 16);
            delay_ms = clamp_u32(delay_ms, RECONNECT_BACKOFF_BASE_MS, RECONNECT_BACKOFF_CAP_MS);

            if (s_sta_reconn_timer) {
                xTimerChangePeriod(s_sta_reconn_timer, pdMS_TO_TICKS(delay_ms), 0);
                xTimerStart(s_sta_reconn_timer, 0);
            }
            s_retry_num++;

#if USE_IDF_PROVISIONING_FALLBACK
            if (s_retry_num == 10) { // exemplo: após várias falhas, abre provisioning
                start_idf_provisioning_softap();
            }
#endif
            break;
        }

        default:
            break;
        }
    }
    else if (event_base == IP_EVENT) {
        switch (event_id) {
        case IP_EVENT_STA_GOT_IP: {
            ip_event_got_ip_t *e = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG_STA, "STA GOT IP: " IPSTR, IP2STR(&e->ip_info.ip));
            s_sta_connected = true;
            s_retry_num = 0;
            s_sta_intentional_disconnect = false;
            if (s_sta_reconn_timer) xTimerStop(s_sta_reconn_timer, 0);
            if (!s_time_sync_inflight) {
            s_time_sync_inflight = true;
            xTaskCreate(time_sync_task, "time_sync_task", 4096, NULL, 4, NULL);
    }
    // 2) garante timer periódico ligado
    if (s_time_resync_timer) xTimerStart(s_time_resync_timer, 0);
            break;
        }
        case IP_EVENT_STA_LOST_IP:    // [NEW] trate perda de IP como gatilho de reconexão
            ESP_LOGW(TAG_STA, "IP do STA perdido — agendando reconexão");
            s_sta_connected = false;
            if (s_sta_reconn_timer) {
                xTimerChangePeriod(s_sta_reconn_timer, pdMS_TO_TICKS(RECONNECT_BACKOFF_BASE_MS), 0);
                xTimerStart(s_sta_reconn_timer, 0);
            }
            break;

        default:
            break;
        }
    }
}

// ================== API ==================
esp_err_t start_wifi_ap_sta(void)
{
    const char *ssid_ap = get_ssid_ap();
    const char *psw_ap  = get_password_ap();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_netif_ap  = esp_netif_create_default_wifi_ap();
    s_netif_sta = esp_netif_create_default_wifi_sta();
    if (!s_netif_ap || !s_netif_sta) {
        ESP_LOGE(TAG_AP, "Falha ao criar netifs AP/STA");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Alimentação por fonte → sem power-save. (Para bateria, troque p/ WIFI_PS_MIN_MODEM)
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
        ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
        IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
        IP_EVENT_STA_LOST_IP, &wifi_event_handler, NULL, NULL));   // [NEW]

    // Config SoftAP
    wifi_config_t apcfg = { 0 };
    apcfg.ap.ssid_len       = (uint8_t)(ssid_ap ? strlen(ssid_ap) : 0);
    apcfg.ap.channel        = ESP_WIFI_CHANNEL;
    apcfg.ap.max_connection = MAX_STA_CONN;
    apcfg.ap.authmode       = WIFI_AUTHMODE_AP;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    // PMF pode ser habilitado aqui se todos os clientes suportarem
    apcfg.ap.pmf_cfg.required = false;
#endif
    if (ssid_ap) strncpy((char*)apcfg.ap.ssid, ssid_ap, sizeof(apcfg.ap.ssid)-1);
    if (psw_ap)  strncpy((char*)apcfg.ap.password, psw_ap, sizeof(apcfg.ap.password)-1);
    if (!psw_ap || strlen(psw_ap) == 0) apcfg.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &apcfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    // [NEW] Timers
    if (!s_sta_reconn_timer) {
        s_sta_reconn_timer = xTimerCreate("sta_reconn",
                                pdMS_TO_TICKS(RECONNECT_BACKOFF_BASE_MS),
                                pdFALSE, NULL, sta_reconnect_timer_cb);
    }
    if (!s_ap_resume_timer) {
        const esp_timer_create_args_t args = {
            .callback = &ap_resume_oneshot_cb,
            .name = "ap_resume",
            .dispatch_method = ESP_TIMER_TASK
        };
        ESP_ERROR_CHECK(esp_timer_create(&args, &s_ap_resume_timer));
    }
    
    if (!s_time_resync_timer) {
        s_time_resync_timer = xTimerCreate(
        "time_resync",
        pdMS_TO_TICKS(TIME_SYNC_RESYNC_INTERVAL_S * 1000), // 6h por default
        pdTRUE,
        NULL,
        time_resync_timer_cb
        );
        }

    s_ap_should_be_on   = true;
    s_ap_suspended_flag = false;
    s_ap_suspend_until_us = 0;

    ESP_LOGI(TAG_AP, "AP+STA iniciado. Portal em http://192.168.4.1");
    return ESP_OK;
}

// **Para desligar tudo** (não use em EXIT/timeout do front)
void stop_wifi_ap_sta(void)
{
    ESP_LOGI(TAG_AP, "Parando Wi-Fi (modo NULL + deinit)");
    s_sta_intentional_disconnect = true;

    // Pare timers
    if (s_sta_reconn_timer) { xTimerStop(s_sta_reconn_timer, 0); }
    if (s_ap_resume_timer)   { esp_timer_stop(s_ap_resume_timer); }

    // Desconecta/para Wi-Fi
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_NULL);

    // Destrói netifs e desinicializa pilhas
    if (s_netif_ap)  { esp_netif_destroy_default_wifi(s_netif_ap);  s_netif_ap = NULL; }
    if (s_netif_sta) { esp_netif_destroy_default_wifi(s_netif_sta); s_netif_sta = NULL; }

    esp_wifi_deinit();
    if (s_wifi_event_group) { vEventGroupDelete(s_wifi_event_group); s_wifi_event_group = NULL; }
    esp_event_loop_delete_default();
    esp_netif_deinit();

    ESP_LOGI(TAG_AP, "*** WIFI Finished ***");
}

// ==== CONTROLES DE AP PARA O FRONT / FACTORY CONTROL ====

// Sair do portal/timeout → “silenciar AP” por alguns segundos, mantendo STA
void wifi_ap_suspend_temporarily(uint32_t seconds)   // [NEW]
{
    if (seconds == 0) seconds = AP_SUSPEND_DEFAULT_SEC;

    user_initiated_exit   = true;
    s_ap_should_be_on     = true;    // queremos o AP de volta no fim
    s_ap_suspended_flag   = true;
    s_ap_suspend_until_us = esp_timer_get_time() + (uint64_t)seconds * 1000000ULL;

    ESP_LOGI(TAG_AP, "Suspendendo AP por %u s (STA permanece ativo)", (unsigned)seconds);

    // Troca simplesmente o modo para STA (não deinit!)
    esp_wifi_set_mode(WIFI_MODE_STA);

    // Agenda retomada garantida por oneshot
    esp_timer_stop(s_ap_resume_timer);
    esp_timer_start_once(s_ap_resume_timer, (uint64_t)seconds * 1000000ULL);
}

// Reabilitar o AP imediatamente (ex.: botão “Reativar Portal”)
void wifi_ap_force_enable(void)               // [NEW]
{
    s_ap_should_be_on     = true;
    s_ap_suspended_flag   = false;
    user_initiated_exit   = false;

    wifi_mode_t m; esp_wifi_get_mode(&m);
    if (m != WIFI_MODE_APSTA) {
        ESP_LOGI(TAG_AP, "Forçando AP ON: setando AP+STA");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    }

    // esp_wifi_start() é idempotente quando já está STARTED; ignore erro se já estiver rodando
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN && err != ESP_ERR_WIFI_NOT_STOPPED) {
        ESP_LOGW(TAG_AP, "esp_wifi_start() ao forçar AP: %s", esp_err_to_name(err));
    }
}

// Desligar AP mantendo STA (sem timers)
void wifi_ap_force_disable(void)                     // [NEW]
{
    s_ap_should_be_on     = false;
    s_ap_suspended_flag   = false;

    ESP_LOGI(TAG_AP, "Forçando AP OFF (mantém STA)");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
}

// Consulta simples
bool wifi_ap_is_running(void)                        // [NEW]
{
    wifi_mode_t m; if (esp_wifi_get_mode(&m) != ESP_OK) return false;
    return (m == WIFI_MODE_AP || m == WIFI_MODE_APSTA);
}

void wifi_sta_mark_intentional_disconnect(bool enable)
{
    // variável interna do driver (já existe no .c novo)
    extern volatile bool s_sta_intentional_disconnect; // se o compilador reclamar, remova o extern e use a s_... diretamente aqui
    s_sta_intentional_disconnect = enable;
}

static void time_sync_task(void *arg){
    time_sync_set_timezone(TIME_SYNC_TZ_DEFAULT);
    esp_err_t e = time_sync_sntp_now(8000); // até 8s de espera
    ESP_LOGI("TIME_SYNC", "SNTP: %s", (e==ESP_OK) ? "OK" : esp_err_to_name(e));
    s_time_sync_inflight = false;
    vTaskDelete(NULL);
}
static void time_resync_timer_cb(TimerHandle_t xTimer){
    if (s_sta_connected && !s_time_sync_inflight) {
        s_time_sync_inflight = true;
        xTaskCreate(time_sync_task, "time_sync_task", 4096, NULL, 4, NULL);
    }
}



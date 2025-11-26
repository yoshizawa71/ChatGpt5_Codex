// wifi_softap_sta.c  — versão robusta AP+STA com suspensão temporária do AP e reconexão STA com backoff
// ESP-IDF v5.x
// George: marquei as adições com [NEW]; mantive seus nomes/estilo para “colar e compilar”.

#include "wifi_softap_sta.h"
#include "sdkconfig.h"

#include "datalogger_control.h"
#include "datalogger_driver.h"

#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

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
#include "system.h"



static volatile uint8_t s_sta_transitioning = 0;

static inline void sta_set_transitioning(bool v) {
    __atomic_store_n(&s_sta_transitioning, v ? 1u : 0u, __ATOMIC_RELEASE);
}

// Contador de estações associadas ao SoftAP
static volatile uint32_t s_sta_count = 0;
static TaskHandle_t s_sta_worker_task = NULL;
// Callback fraco: se você não definir nada em outro arquivo, é "no-op"
// (em factory_control.c colocaremos uma versão forte que inicia o HTTP server)
__attribute__((weak)) void wifi_portal_on_ap_start(void) { /* nop */ }

__attribute__((weak)) bool wifi_ap_is_suspended(void) { return false; }
__attribute__((weak)) uint32_t wifi_ap_seconds_to_resume(void) { return 0; }
uint32_t wifi_ap_get_sta_count(void) { return s_sta_count; }

/* Ajuste aqui o fuso (ou troque por valor vindo do NVS) */
//#define TIME_SYNC_TZ_DEFAULT         "UTC-3"
#define TIME_SYNC_TZ_DEFAULT "<-03>3"   // BRT: UTC-3, sem DST
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
static const char *TAG_EVT = "WiFi EVT";

// [NEW] cache das credenciais lidas no boot
static char s_boot_ssid[33] = {0};
static char s_boot_pwd[65]  = {0};

// [NEW] timer de “kick” do STA pós-boot (failsafe)
static TimerHandle_t s_sta_kick_timer = NULL;

static void sta_kick_timer_cb(TimerHandle_t xTimer);  // fwd
static void wifi_sta_force_connect_if_enabled(const char *ssid, const char *pwd);  // fwd


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

// (opcional) guardar handles para poder desregistrar depois
static esp_event_handler_instance_t s_wifi_any_id = NULL;
static esp_event_handler_instance_t s_ip_got_id   = NULL;
//============================================
static void wifi_evt_logger(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_AP_START:        ESP_LOGI(TAG_EVT, "AP_START"); break;
        case WIFI_EVENT_AP_STOP:         ESP_LOGW(TAG_EVT, "AP_STOP");  break;
        case WIFI_EVENT_AP_STACONNECTED: ESP_LOGI(TAG_EVT, "AP_STA_CONNECTED"); break;
        case WIFI_EVENT_AP_STADISCONNECTED: ESP_LOGW(TAG_EVT, "AP_STA_DISCONNECTED"); break;
        case WIFI_EVENT_STA_START:       ESP_LOGI(TAG_EVT, "STA_START"); break;
        case WIFI_EVENT_STA_CONNECTED:   ESP_LOGI(TAG_EVT, "STA_CONNECTED"); break;
        case WIFI_EVENT_STA_DISCONNECTED:ESP_LOGW(TAG_EVT, "STA_DISCONNECTED"); break;
        default:                         ESP_LOGI(TAG_EVT, "WIFI_EVENT id=%ld", (long)id); break;
        }
    }
}

static void ip_evt_logger(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG_EVT, "STA_GOT_IP");
    }
}

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
static void sta_reconnect_timer_cb(TimerHandle_t xTimer) {
    if (s_sta_intentional_disconnect) return;
    if (!has_activate_sta()) return;
    if (s_sta_worker_task) xTaskNotifyGive(s_sta_worker_task);
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

// Tarefa pós-AP_START: idempotente e leve
static void ap_post_start_task(void *arg)
{
    // dá tempo do BSS ficar de pé antes de mexer no netif/DHCP
    vTaskDelay(pdMS_TO_TICKS(250));

    // garanta que o DHCP do AP está ligado (idempotente)
    esp_netif_t *ap = s_netif_ap;
    if (!ap) {
        ap = esp_netif_get_handle_from_ifkey("WIFI_AP"); // fallback
    }
    if (ap) {
        // só start; não dar stop/start aqui
        esp_netif_dhcps_start(ap);

        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(ap, &ip) == ESP_OK) {
            ESP_LOGI(TAG_AP, "AP IP (pós-start): " IPSTR, IP2STR(&ip.ip));
        }
    } else {
        ESP_LOGW(TAG_AP, "ap_post_start_task: netif AP nulo");
    }

    // (re)garante o portal/HTTP — fora do handler de eventos
    wifi_portal_on_ap_start();

    vTaskDelete(NULL);
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
              //Agenda uma tarefa pós-start (200–300 ms depois).
            
                xTaskCreate(ap_post_start_task, "ap_post_start", 3072, NULL, 4, NULL);
                
              break;

        case WIFI_EVENT_AP_STOP: {
            ESP_LOGI(TAG_AP, "SoftAP parado (AP_STOP)");
            ap_active = false;
              break;
        }

        case WIFI_EVENT_AP_STACONNECTED:
             s_sta_count++;
             ESP_LOGI(TAG_AP, "STA conectou; total=%"PRIu32, s_sta_count);
            break;

        case WIFI_EVENT_AP_STADISCONNECTED:
             if (s_sta_count) s_sta_count--;
             ESP_LOGI(TAG_AP, "STA desconectou; total=%"PRIu32, s_sta_count);;
            break;

        case WIFI_EVENT_STA_START: {
            ESP_LOGI(TAG_STA, "STA start");
            sta_set_transitioning(true);

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
            sta_set_transitioning(true);
            s_sta_connected = true;
            s_retry_num = 0;
            s_sta_intentional_disconnect = false;
            if (s_sta_reconn_timer) xTimerStop(s_sta_reconn_timer, 0);
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
//			atomic_store(&s_sta_transitioning, false);
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
            sta_set_transitioning(true);

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
            sta_set_transitioning(false);  // **fim da transição**
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
            sta_set_transitioning(true);
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

// worker: faz o trabalho fora do callback de timer
static void sta_worker_task(void *arg) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_sta_intentional_disconnect || !has_activate_sta()) continue;
        esp_err_t e = esp_wifi_connect();
        if (e != ESP_OK) ESP_LOGW(TAG_STA, "esp_wifi_connect(): %s", esp_err_to_name(e));
    }
}

// ============================================================
// [NEW] Helper idempotente: aplica config do STA e tenta conectar
// Chame isto sempre que quiser garantir que o STA “anda”
// (não falha se já estiver conectado).
static void wifi_sta_force_connect_if_enabled(const char *ssid, const char *pwd)
{
    if (!has_activate_sta()) {
        ESP_LOGI(TAG_STA, "STA desativado no front → não conectando.");
        return;
    }
    if (!ssid || !*ssid) {
        ESP_LOGW(TAG_STA, "STA habilitado, mas SSID vazio → não conectando.");
        return;
    }

    wifi_config_t sta = {0};
    // scan mais abrangente e threshold típico
    sta.sta.scan_method        = WIFI_ALL_CHANNEL_SCAN;
    sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    strncpy((char*)sta.sta.ssid, ssid, sizeof(sta.sta.ssid)-1);
    if (pwd) strncpy((char*)sta.sta.password, pwd, sizeof(sta.sta.password)-1);

    esp_err_t e1 = esp_wifi_set_config(WIFI_IF_STA, &sta);
    ESP_LOGI(TAG_STA, "Aplicando STA cfg (ssid_len=%u, pwd_len=%u): %s",
             (unsigned)strlen((const char*)sta.sta.ssid),
             (unsigned)strlen((const char*)sta.sta.password),
             esp_err_to_name(e1));

    // tentar conectar (idempotente; se já conectado, IDF ignora/retorna estado)
    esp_err_t e2 = esp_wifi_connect();
    ESP_LOGI(TAG_STA, "esp_wifi_connect(): %s", esp_err_to_name(e2));
}

static void sta_kick_timer_cb(TimerHandle_t xTimer) {
    if (s_sta_worker_task) xTaskNotifyGive(s_sta_worker_task);
}

static inline void apply_tz_brt_once(void){
    static bool s_done = false;
    if (!s_done) {
        setenv("TZ", "<-03>3", 1);  // BRT fixo
        tzset();
        s_done = true;
    }
}

static void time_sync_task(void *arg){
    // garanta TZ antes de qualquer uso de mktime/localtime
    apply_tz_brt_once();

    time_sync_set_timezone(TIME_SYNC_TZ_DEFAULT);   // pode manter, não atrapalha

    esp_err_t e = time_sync_sntp_now(8000);
    ESP_LOGI("TIME_SYNC", "SNTP: %s", (e==ESP_OK) ? "OK" : esp_err_to_name(e));

    // Debug opcional
    time_t now = time(NULL);
    ESP_LOGI("TIME_SYNC", "Local now: %s", asctime(localtime(&now)));

    s_time_sync_inflight = false;
    vTaskDelete(NULL);
}
static void time_resync_timer_cb(TimerHandle_t xTimer){
    if (s_sta_connected && !s_time_sync_inflight) {
        s_time_sync_inflight = true;
        xTaskCreate(time_sync_task, "time_sync_task", 4096, NULL, 4, NULL);
    }
}

//========================================================================
bool wifi_is_sta_transitioning(void) {
    return __atomic_load_n(&s_sta_transitioning, __ATOMIC_ACQUIRE) != 0;
}

// Chame isso ao final do seu init de Wi-Fi (depois de esp_wifi_init()).
void wifi_diag_logger_init(void)
{
    // registra TODOS os WIFI_EVENT
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_evt_logger, NULL, &s_wifi_any_id);
    // (opcional) IP_EVENT STA_GOT_IP
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &ip_evt_logger, NULL, &s_ip_got_id);
}

// (opcional) desregistrar no deinit
void wifi_diag_logger_deinit(void)
{
    if (s_wifi_any_id) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_any_id);
        s_wifi_any_id = NULL;
    }
    if (s_ip_got_id) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_got_id);
        s_ip_got_id = NULL;
    }
}

// ================== API ==================
esp_err_t start_wifi_ap_sta(void)
{
    const char *ssid_ap = get_ssid_ap();
    const char *psw_ap  = get_password_ap();

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return err;                 // ou ESP_ERROR_CHECK(err);
    }
    
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return err;                 // ou ESP_ERROR_CHECK(err);
    }

    s_netif_ap  = esp_netif_create_default_wifi_ap();
    s_netif_sta = esp_netif_create_default_wifi_sta();
    if (!s_netif_ap || !s_netif_sta) {
        ESP_LOGE(TAG_AP, "Falha ao criar netifs AP/STA");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // [NEW] Armazene na RAM para não puxar configs antigas do NVS do driver
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    // [RECOMENDADO] Sem power-save quando alimentado por fonte
    // (se seu produto for a bateria, reavalie isso)
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    
    wifi_diag_logger_init();

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
        ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
        IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
        IP_EVENT_STA_LOST_IP, &wifi_event_handler, NULL, NULL));   // [NEW]

// [NEW] Carregar credenciais ANTES do start
    const char *ssid_sta = get_ssid_sta();
    const char *pwd_sta  = get_password_sta();
    memset(s_boot_ssid, 0, sizeof(s_boot_ssid));
    memset(s_boot_pwd,  0, sizeof(s_boot_pwd));
    if (ssid_sta) strncpy(s_boot_ssid, ssid_sta, sizeof(s_boot_ssid)-1);
    if (pwd_sta)  strncpy(s_boot_pwd,  pwd_sta,  sizeof(s_boot_pwd)-1);

    ESP_LOGI(TAG_STA, "Boot STA cfg: enabled=%d, ssid_len=%u, pwd_len=%u",
             has_activate_sta(),
             (unsigned)strlen(s_boot_ssid),
             (unsigned)strlen(s_boot_pwd));

    // [NEW] Prepare o timer de “kick” do STA (failsafe pós-boot)
    if (!s_sta_kick_timer) s_sta_kick_timer = xTimerCreate("sta_kick", pdMS_TO_TICKS(3000), pdFALSE, NULL, sta_kick_timer_cb);
 
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

    if (ssid_ap) {
        strncpy((char*)apcfg.ap.ssid, ssid_ap, sizeof(apcfg.ap.ssid) - 1);
        ((char*)apcfg.ap.ssid)[sizeof(apcfg.ap.ssid) - 1] = '\0';
    }

    size_t pwlen = 0;
    if (psw_ap) {
        strncpy((char*)apcfg.ap.password, psw_ap, sizeof(apcfg.ap.password) - 1);
        ((char*)apcfg.ap.password)[sizeof(apcfg.ap.password) - 1] = '\0';
        pwlen = strlen((const char*)apcfg.ap.password);
    }

    if (pwlen == 0) {
        // Sem senha -> AP aberto
        apcfg.ap.password[0] = '\0';
        apcfg.ap.authmode = WIFI_AUTH_OPEN;
    } else if (pwlen < 8) {
        // Senha inválida pra WPA/WPA2: NÃO pode dar panic.
        // Força AP aberto e descarta a senha curta.
        ESP_LOGW(TAG_AP,
                 "Senha do AP muito curta (%d chars). Forçando AP aberto.",
                 (int)pwlen);
        apcfg.ap.password[0] = '\0';
        apcfg.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        // Senha >= 8 -> mantém modo seguro configurado em WIFI_AUTHMODE_AP
        if (apcfg.ap.authmode == WIFI_AUTH_OPEN) {
            apcfg.ap.authmode = WIFI_AUTHMODE_AP;
        }
    }

    // [NEW] Setar modo AP+STA e configs ANTES do start
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &apcfg));

    // Se já temos SSID, aplique a config do STA ANTES do start (idempotente)
    if (s_boot_ssid[0]) {
        wifi_config_t sta = {0};
        sta.sta.scan_method        = WIFI_ALL_CHANNEL_SCAN;
        sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        strncpy((char*)sta.sta.ssid, s_boot_ssid, sizeof(sta.sta.ssid)-1);
        strncpy((char*)sta.sta.password, s_boot_pwd, sizeof(sta.sta.password)-1);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    }

    ESP_ERROR_CHECK(esp_wifi_start());   // Wi-Fi ON
    
    if (!s_sta_worker_task) {
    // 3072 bytes costumam ser suficientes; ajuste se usar logs muito verbosos
    xTaskCreate(sta_worker_task, "sta_reconn_wkr", 3072, NULL, 4, &s_sta_worker_task);
}

// Failsafe imediato pós-start: não dependa só do WIFI_EVENT_STA_START
wifi_sta_force_connect_if_enabled(s_boot_ssid, s_boot_pwd);

// E programa um “kick” 3 s depois, caso a ordem de eventos atrapalhe
if (s_sta_kick_timer) xTimerStart(s_sta_kick_timer, 0);

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

    // --- Fase 0: sinaliza desconexão intencional (evita reconectar em callbacks) ---
    s_sta_intentional_disconnect = true;

    // --- Fase 1: pare e destrua timers que podem repostar eventos/callbacks ---
    if (s_sta_reconn_timer)  {
        xTimerStop(s_sta_reconn_timer, 0);
        xTimerDelete(s_sta_reconn_timer, 0);
        s_sta_reconn_timer = NULL;
    }
    if (s_time_resync_timer) {
        xTimerStop(s_time_resync_timer, 0);
        xTimerDelete(s_time_resync_timer, 0);
        s_time_resync_timer = NULL;
    }
    if (s_ap_resume_timer)   {
        esp_timer_stop(s_ap_resume_timer);
        esp_timer_delete(s_ap_resume_timer);
        s_ap_resume_timer = NULL;
    }

    // --- Fase 2: calar loggers e handlers de evento antes do vendaval do stop() ---
    // Logger diagnóstico (já existia no seu projeto)
    wifi_diag_logger_deinit();

    // Seu handler principal de Wi-Fi/IP (evita prints/lógica na task sys_evt durante o stop)
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT,    IP_EVENT_STA_GOT_IP,  &wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT,    IP_EVENT_STA_LOST_IP, &wifi_event_handler);

    // --- Fase 3: pequena drenagem para esvaziar a fila do event loop ---
    vTaskDelay(pdMS_TO_TICKS(2)); // 2–5 ms é suficiente

    // --- Fase 4: parar Wi-Fi com ordem: disconnect -> stop -> NULL mode ---
    // (erros são tolerados se já estiver parado)
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_NULL);

    // --- Fase 5: destruir netifs específicos do Wi-Fi (deixe o esp-netif vivo) ---
    if (s_netif_ap)  { esp_netif_destroy_default_wifi(s_netif_ap);  s_netif_ap  = NULL; }
    if (s_netif_sta) { esp_netif_destroy_default_wifi(s_netif_sta); s_netif_sta = NULL; }

    // --- Fase 6: desinicializar o driver Wi-Fi ---
    esp_wifi_deinit();

    // --- Fase 7: recursos gerais (mantenha o default event loop e o esp_netif vivos) ---
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }

    // IMPORTANTE:
    // 1) NÃO delete o event loop default aqui; outros subsistemas podem depender dele.
    //    Se você apagar, terá que recriar antes de cada novo start.
    // esp_event_loop_delete_default();   // <-- deixe comentado

    // 2) NÃO deinitialize o esp-netif global; manter vivo evita reinit custoso/erros.
    // esp_netif_deinit();                // <-- deixe comentado

    ESP_LOGI(TAG_AP, "*** WIFI Finished ***");
}

// ==== CONTROLES DE AP PARA O FRONT / FACTORY CONTROL ====

// Sair do portal/timeout â†’ â€œsilenciar APâ€ por alguns segundos, mantendo STA
void wifi_ap_suspend_temporarily(uint32_t seconds)   // [NEW]
{
    if (seconds == 0) seconds = AP_SUSPEND_DEFAULT_SEC;

    user_initiated_exit   = true;
    s_ap_should_be_on     = true;    // queremos o AP de volta no fim
    s_ap_suspended_flag   = true;
    s_ap_suspend_until_us = esp_timer_get_time() + (uint64_t)seconds * 1000000ULL;

    ESP_LOGI(TAG_AP, "Suspendendo AP por %u s (STA permanece ativo)", (unsigned)seconds);

    // Troca simplesmente o modo para STA (nÃ£o deinit!)
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
    wifi_mode_t m = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&m) != ESP_OK) return false;
    return (m == WIFI_MODE_AP || m == WIFI_MODE_APSTA);
}

void wifi_sta_mark_intentional_disconnect(bool enable)
{
    // variável interna do driver (já existe no .c novo)
    extern volatile bool s_sta_intentional_disconnect; // se o compilador reclamar, remova o extern e use a s_... diretamente aqui
    s_sta_intentional_disconnect = enable;
}


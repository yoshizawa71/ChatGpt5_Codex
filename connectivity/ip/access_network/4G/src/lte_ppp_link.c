/*
 * lte_ppp_link.c
 *
 * Implementacao do link PPP via LTE usando ubxlib em ESP-IDF.
 *
 * Objetivo:
 *  - Subir/derrubar PPP APENAS para uso de IP (por ex. OTA).
 *  - Nao mexer no caminho atual de envio (AT / uCellNetConnect).
 *
 * Caracteristicas:
 *  - Inicializa ubxlib por conta propria (uPortInit/uDeviceInit).
 *  - Abre o device SARA (uDeviceOpen) se ninguem tiver fornecido um handle.
 *  - Se voce chamar lte_ppp_set_device_handle(devHandle), ele passa a usar esse
 *    handle externo (nao chama uDeviceOpen).
 *  - Usa uNetworkInterfaceUp()/Down() com U_NETWORK_TYPE_CELL (modo PPP).
 */

#include "lte_ppp_link.h"

#include <string.h>
#include "esp_log.h"
#include "esp_netif.h"

// ubxlib
#include "ubxlib.h"
#include "u_cell_net.h"                 // uCellNetAuthenticationMode_t
#include "u_cfg_app_platform_specific.h"
#include "u_cell_module_type.h"

// Projeto: credenciais LTE
#include "datalogger_control.h"         // get_apn(), get_lte_user(), get_lte_pw()
#include "sara_r422.h"

static const char *TAG = "lte_ppp_link";

/* ----------------------------------------------------------------
 * FALLBACK DE MODULE TYPE
 * -------------------------------------------------------------- */
/*
 * Em alguns projetos, U_CFG_TEST_CELL_MODULE_TYPE nao vem definido
 * pelos exemplos da ubxlib. Como sabemos que o seu modulo e um
 * SARA-R4 (R422), definimos um padrao aqui se o macro nao existir.
 *
 * Se em algum momento voce configurar U_CFG_TEST_CELL_MODULE_TYPE
 * no u_cfg_app_platform_specific.h ou na linha de compilacao,
 * essa definicao aqui sera ignorada.
 */
#ifndef U_CFG_TEST_CELL_MODULE_TYPE
#define U_CFG_TEST_CELL_MODULE_TYPE U_CELL_MODULE_TYPE_SARA_R4
#endif

/* ----------------------------------------------------------------
 * ESTADO INTERNO
 * -------------------------------------------------------------- */

static bool               s_initialized     = false;
static bool               s_ubxInitialized  = false;
static uDeviceHandle_t    s_devHandle       = NULL;
static lte_ppp_state_t    s_state           = LTE_PPP_STATE_IDLE;
static lte_ppp_event_cb_t s_eventCb         = NULL;
static esp_netif_t       *s_lteNetif        = NULL;  // por enquanto fica NULL

/* ----------------------------------------------------------------
 * CONFIGURACAO DO DEVICE (UBXLIB)
 * -------------------------------------------------------------- */
/*
 * A configuracao abaixo usa os macros da ubxlib:
 *  - U_CFG_TEST_CELL_MODULE_TYPE       (tipo do modulo, ex.: U_CELL_MODULE_TYPE_SARA_R4)
 *  - U_CFG_APP_PIN_CELL_ENABLE_POWER
 *  - U_CFG_APP_PIN_CELL_PWR_ON
 *  - U_CFG_APP_PIN_CELL_VINT
 *  - U_CFG_APP_PIN_CELL_DTR
 *  - U_CFG_APP_CELL_UART
 *  - U_CFG_APP_PIN_CELL_TXD/RXD/CTS/RTS
 *
 * Esses macros normalmente estao em u_cfg_app_platform_specific.h.
 */

static const uDeviceCfg_t gLtePppDeviceCfg = {
    .deviceType = U_DEVICE_TYPE_CELL,
    .deviceCfg = {
        .cfgCell = {
            .moduleType         = U_CFG_TEST_CELL_MODULE_TYPE,
            .pSimPinCode        = NULL,
            .pinEnablePower     = U_CFG_APP_PIN_CELL_ENABLE_POWER,
            .pinPwrOn           = U_CFG_APP_PIN_CELL_PWR_ON,
            .pinVInt            = U_CFG_APP_PIN_CELL_VINT,
            .pinDtrPowerSaving  = U_CFG_APP_PIN_CELL_DTR
        },
    },
    .transportType = U_DEVICE_TRANSPORT_TYPE_UART,
    .transportCfg = {
        .cfgUart = {
            .uart     = U_CFG_APP_CELL_UART,
            .baudRate = U_CELL_UART_BAUD_RATE,
            .pinTxd   = U_CFG_APP_PIN_CELL_TXD,
            .pinRxd   = U_CFG_APP_PIN_CELL_RXD,
            .pinCts   = U_CFG_APP_PIN_CELL_CTS,
            .pinRts   = U_CFG_APP_PIN_CELL_RTS,
#ifdef U_CFG_APP_UART_PREFIX
            .pPrefix  = U_PORT_STRINGIFY_QUOTED(U_CFG_APP_UART_PREFIX)
#else
            .pPrefix  = NULL
#endif
        },
    },
};

/* ----------------------------------------------------------------
 * HELPERS INTERNOS
 * -------------------------------------------------------------- */

static void notify_state(lte_ppp_state_t new_state)
{
    s_state = new_state;
    if (s_eventCb) {
        s_eventCb(new_state);
    }
    ESP_LOGI(TAG, "LTE PPP state -> %d", (int)new_state);
}

static void load_credentials_to_cfg(uNetworkCfgCell_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->type = U_NETWORK_TYPE_CELL;

    const char *apn  = get_apn();
    const char *user = get_lte_user();
    const char *pw   = get_lte_pw();

    cfg->pApn      = (apn  && apn[0])  ? apn  : NULL;
    cfg->pUsername = (user && user[0]) ? user : NULL;
    cfg->pPassword = (pw   && pw[0])   ? pw   : NULL;

    cfg->authenticationMode = U_CELL_NET_AUTHENTICATION_MODE_PAP;
    cfg->timeoutSeconds     = 180; // timeout generoso para NB-IoT/LTE

    if (cfg->pApn) {
        ESP_LOGI(TAG, "PPP APN: %s", cfg->pApn);
    } else {
        ESP_LOGW(TAG, "PPP APN nao definida (usa default da operadora, se houver).");
    }
    if (cfg->pUsername) {
        ESP_LOGI(TAG, "PPP usuario definido.");
    } else {
        ESP_LOGI(TAG, "PPP usuario NAO definido (NULL).");
    }
    if (cfg->pPassword) {
        ESP_LOGI(TAG, "PPP senha definida.");
    } else {
        ESP_LOGI(TAG, "PPP senha NAO definida (NULL).");
    }
}

static esp_err_t ensure_ubxlib_initialized(void)
{
    if (s_ubxInitialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Inicializando ubxlib (uPort/uDevice) para PPP...");

    int32_t rc = uPortInit();
    if (rc != 0) {
        ESP_LOGE(TAG, "uPortInit() falhou, rc=%d", (int)rc);
        notify_state(LTE_PPP_STATE_ERROR);
        return ESP_FAIL;
    }

    rc = uDeviceInit();
    if (rc != 0) {
        ESP_LOGE(TAG, "uDeviceInit() falhou, rc=%d", (int)rc);
        notify_state(LTE_PPP_STATE_ERROR);
        return ESP_FAIL;
    }

    s_ubxInitialized = true;
    ESP_LOGI(TAG, "ubxlib inicializado para PPP.");
    return ESP_OK;
}

static esp_err_t ensure_device_open(void)
{
    if (s_devHandle != NULL) {
        // Ja temos um handle (pode ter vindo de lte_ppp_set_device_handle()).
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Abrindo device SARA para PPP (uDeviceOpen)...");

    int32_t rc = uDeviceOpen(&gLtePppDeviceCfg, &s_devHandle);
    if (rc != 0) {
        ESP_LOGE(TAG, "uDeviceOpen() falhou, rc=%d", (int)rc);
        s_devHandle = NULL;
        notify_state(LTE_PPP_STATE_ERROR);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Device SARA aberto para PPP (handle=%p).", (void *)s_devHandle);
    return ESP_OK;
}

/* ----------------------------------------------------------------
 * API PUBLICA
 * -------------------------------------------------------------- */

esp_err_t lte_ppp_init(void)
{
    ESP_LOGI(TAG, "Inicializando modulo LTE PPP...");

    s_initialized    = true;
    s_state          = LTE_PPP_STATE_IDLE;
    s_eventCb        = NULL;
    s_lteNetif       = NULL;
    // s_ubxInitialized/s_devHandle permanecem como estao; init/open sao on-demand.

    ESP_LOGI(TAG, "LTE PPP inicializado (estado=IDLE).");
    return ESP_OK;
}

void lte_ppp_set_device_handle(uDeviceHandle_t devHandle)
{
    if (devHandle) {
        ESP_LOGI(TAG, "Device handle LTE PPP recebido externamente (%p).", (void *)devHandle);
        s_devHandle = devHandle;
    } else {
        ESP_LOGW(TAG, "lte_ppp_set_device_handle() chamado com devHandle NULL.");
    }
}

void lte_ppp_register_callback(lte_ppp_event_cb_t cb)
{
    s_eventCb = cb;
}

static esp_err_t check_preconditions(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "lte_ppp_init() nao foi chamado antes de lte_ppp_start().");
        return ESP_FAIL;
    }

    if (s_state == LTE_PPP_STATE_NET_BRINGUP) {
        ESP_LOGW(TAG, "PPP LTE ja em processo de conexao (NET_BRINGUP).");
        return ESP_FAIL;
    }

    if (s_state == LTE_PPP_STATE_CONNECTED) {
        ESP_LOGI(TAG, "PPP LTE ja esta conectado.");
        return ESP_OK;
    }

    return ESP_OK;
}

esp_err_t lte_ppp_start(void)
{
    esp_err_t err = check_preconditions();
    if (err != ESP_OK && s_state != LTE_PPP_STATE_CONNECTED) {
        return ESP_FAIL;
    }
    if (s_state == LTE_PPP_STATE_CONNECTED) {
        // Ja esta conectado, nada a fazer.
        return ESP_OK;
    }

    // 1) Garantir ubxlib pronto
    if (ensure_ubxlib_initialized() != ESP_OK) {
        return ESP_FAIL;
    }

    // 2) Garantir device aberto (usa handle externo se tiver, senao abre)
    if (ensure_device_open() != ESP_OK) {
        return ESP_FAIL;
    }

    // 3) Montar configuracao PPP (APN/USER/PASS)
    uNetworkCfgCell_t netCfg;
    load_credentials_to_cfg(&netCfg);

    notify_state(LTE_PPP_STATE_NET_BRINGUP);
    ESP_LOGI(TAG, "Chamando uNetworkInterfaceUp() para PPP LTE...");

    int32_t rc = uNetworkInterfaceUp(s_devHandle, U_NETWORK_TYPE_CELL, &netCfg);
    if (rc != 0) {
        ESP_LOGE(TAG, "uNetworkInterfaceUp() falhou, rc=%d", (int)rc);
        notify_state(LTE_PPP_STATE_ERROR);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "uNetworkInterfaceUp() retornou sucesso. PPP LTE ativo.");

    // Opcional: obter esp_netif associado ao PPP (se a sua versao da ubxlib suportar).
    // Deixamos comentado para nao introduzir dependencia de simbolo ausente.
    //
    // extern esp_netif_t *uNetworkGetEspNetif(uDeviceHandle_t devHandle, uNetworkType_t netType);
    // s_lteNetif = uNetworkGetEspNetif(s_devHandle, U_NETWORK_TYPE_CELL);
    // if (!s_lteNetif) {
    //     ESP_LOGW(TAG, "uNetworkGetEspNetif() retornou NULL; "
    //                   "sockets ainda devem funcionar via LwIP, mas nao temos o ponteiro netif.");
    // }

    notify_state(LTE_PPP_STATE_CONNECTED);
    ESP_LOGI(TAG, "PPP LTE CONNECTED com sucesso.");

    return ESP_OK;
}

void lte_ppp_stop(void)
{
    ESP_LOGI(TAG, "Solicitado stop do PPP LTE...");

    if (!s_initialized) {
        ESP_LOGW(TAG, "lte_ppp_stop() chamado sem lte_ppp_init(). Nada a fazer.");
        return;
    }

    if (s_devHandle == NULL) {
        ESP_LOGW(TAG, "lte_ppp_stop(): devHandle NULL; nada a fazer.");
        notify_state(LTE_PPP_STATE_IDLE);
        return;
    }

    if (s_state != LTE_PPP_STATE_CONNECTED &&
        s_state != LTE_PPP_STATE_NET_BRINGUP &&
        s_state != LTE_PPP_STATE_ERROR) {
        ESP_LOGI(TAG, "lte_ppp_stop(): estado atual nao requer derrubar rede (state=%d).", (int)s_state);
        return;
    }

    ESP_LOGI(TAG, "Chamando uNetworkInterfaceDown() para PPP LTE...");
    int32_t rc = uNetworkInterfaceDown(s_devHandle, U_NETWORK_TYPE_CELL);
    if (rc != 0) {
        ESP_LOGW(TAG, "uNetworkInterfaceDown() retornou erro rc=%d.", (int)rc);
    }

    // Nao fechamos o device aqui; ele pode ser reutilizado em uma proxima
    // chamada de lte_ppp_start(). Se em algum momento voce quiser ser
    // agressivo com economia, podemos adicionar uma funcao que fecha device
    // e faz uDeviceDeinit/uPortDeinit.

    notify_state(LTE_PPP_STATE_IDLE);
    ESP_LOGI(TAG, "PPP LTE parado (estado=IDLE).");
}

bool lte_ppp_is_connected(void)
{
    return (s_state == LTE_PPP_STATE_CONNECTED);
}

lte_ppp_state_t lte_ppp_get_state(void)
{
    return s_state;
}

esp_netif_t *lte_ppp_get_netif(void)
{
    return s_lteNetif;
}

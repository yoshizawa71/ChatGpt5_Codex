#include "esp_log.h"
#include "esp_netif.h"
#include "u_port_ppp.h"
#include "u_sock.h" // Para uSockIpAddress_t
#include "esp_err.h"
#include "datalogger_control.h" // Para get_apn(), get_lte_user(), get_lte_pw()

static const char *TAG = "ppp_control";

// Handle global para o dispositivo (deve ser inicializado pelo chamador)
static void *gDevHandle = NULL;
static esp_netif_t *gPppNetif = NULL;

// Callback para receber dados PPP
static void ppp_receive_callback(void *pDevHandle, const char *pData, size_t dataSize, void *pCallbackParam) {
    ESP_LOGI(TAG, "Received %zu bytes via PPP", dataSize);
}

// Callback para abrir a conexão PPP
static int32_t ppp_connect_callback(void *pDevHandle,
                                    uPortPppReceiveCallback_t *pReceiveCallback,
                                    void *pReceiveCallbackParam,
                                    char *pReceiveData,
                                    size_t receiveDataSize,
                                    bool (*pKeepGoingCallback)(void *)) {
    ESP_LOGI(TAG, "Opening PPP connection");
    if (pReceiveData == NULL && pReceiveCallback != NULL) {
        ESP_LOGI(TAG, "Allocating receive buffer of size %zu", receiveDataSize);
    }
    return 0;
}

// Callback para fechar a conexão PPP
static int32_t ppp_disconnect_callback(void *pDevHandle, bool pppTerminateRequired) {
    ESP_LOGI(TAG, "Closing PPP connection");
    if (pppTerminateRequired) {
        ESP_LOGI(TAG, "Terminating PPP connection");
    }
    return 0;
}

// Callback para transmitir dados via PPP
static int32_t ppp_transmit_callback(void *pDevHandle, const char *pData, size_t dataSize) {
    ESP_LOGI(TAG, "Transmitting %zu bytes via PPP", dataSize);
    return (int32_t)dataSize;
}

// Inicializa a conexão PPP
esp_err_t init_ppp(void) {
 //   esp_err_t ret = ESP_OK;

    // Verificar handles
    if (gDevHandle == NULL || gPppNetif == NULL) {
        ESP_LOGE(TAG, "Device handle or PPP netif not initialized");
        return ESP_FAIL;
    }

    // Configurar callbacks PPP
    ESP_LOGI(TAG, "Attaching PPP callbacks...");
    int32_t attach_ret = uPortPppAttach(gDevHandle,
                                        ppp_connect_callback,
                                        ppp_disconnect_callback,
                                        ppp_transmit_callback);
    if (attach_ret != 0) {
        ESP_LOGE(TAG, "Failed to attach PPP callbacks: %d", attach_ret);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "PPP callbacks attached");

    // Obter credenciais
    const char *username = get_lte_user();
    const char *password = get_lte_pw();
    ESP_LOGI(TAG, "Using username: %s", username ? username : "NULL");
    ESP_LOGI(TAG, "Using password: %s", password ? "****" : "NULL");

    // Configurar parâmetros de conexão PPP
    uPortPppAuthenticationMode_t authMode = U_PORT_PPP_AUTHENTICATION_MODE_PAP;

    // Iniciar conexão PPP
    ESP_LOGI(TAG, "Starting PPP connection...");
    uSockIpAddress_t ipAddress = {0};
    uSockIpAddress_t dnsPrimary = {0};
    uSockIpAddress_t dnsSecondary = {0};
    int32_t ppp_ret = uPortPppConnect(gDevHandle,
                                      &ipAddress,
                                      &dnsPrimary,
                                      &dnsSecondary,
                                      username,
                                      password,
                                      authMode);
    if (ppp_ret != 0) {
        ESP_LOGE(TAG, "Failed to connect PPP: %d", ppp_ret);
        return ESP_FAIL;
    }

    // Logar IP atribuído
    if (ipAddress.address.ipv4 != 0) {
        ESP_LOGI(TAG, "Assigned IP: %d.%d.%d.%d",
                 (ipAddress.address.ipv4 >> 24) & 0xFF,
                 (ipAddress.address.ipv4 >> 16) & 0xFF,
                 (ipAddress.address.ipv4 >> 8) & 0xFF,
                 ipAddress.address.ipv4 & 0xFF);
    } else {
        ESP_LOGW(TAG, "No IP address assigned");
    }

    ESP_LOGI(TAG, "PPP initialized successfully");
    return ESP_OK;
}

// Configura o handle do dispositivo
void set_ppp_device_handle(void *devHandle) {
    ESP_LOGI(TAG, "Setting PPP device handle");
    gDevHandle = devHandle;
}

// Configura a interface de rede PPP
void set_ppp_netif(esp_netif_t *netif) {
    ESP_LOGI(TAG, "Setting PPP netif");
    gPppNetif = netif;
}
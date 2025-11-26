/*
 * ppp_control.c
 *
 * Controle centralizado do PPP LTE (SARA-R4/R41/R412/R422)
 * com máquina de estados + task dedicada + fila de comandos.
 *
 * Autor: ChatGPT (Enterprise Edition)
 */

#include "ppp_control.h"
#include "lte_ppp_link.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"  // esp_timer_get_time()

// -----------------------------------------------------------------------------
//  LOG
// -----------------------------------------------------------------------------
static const char *TAG = "ppp_control";

// -----------------------------------------------------------------------------
//  DEFINES
// -----------------------------------------------------------------------------
#define PPP_CTRL_TASK_STACK     (4096)
#define PPP_CTRL_TASK_PRIO      (configMAX_PRIORITIES - 4)
#define PPP_CTRL_QUEUE_LEN      (8)

// Timeout do estado STARTING -> CONNECTED (opcional)
#define PPP_CTRL_CONNECT_TIMEOUT_MS   35000

// -----------------------------------------------------------------------------
//  ENUMS / TIPOS INTERNOS
// -----------------------------------------------------------------------------

// Comandos que chegam pela fila
typedef enum {
    PPP_CTRL_CMD_START = 1,
    PPP_CTRL_CMD_STOP,
    PPP_CTRL_CMD_SHUTDOWN   // Para destruir a task (opcional)
} ppp_ctrl_cmd_t;

// -----------------------------------------------------------------------------
//  VARIÁVEIS ESTÁTICAS INTERNAS
// -----------------------------------------------------------------------------
static xQueueHandle      s_ctrlQueue        = NULL;
static TaskHandle_t      s_ctrlTaskHandle   = NULL;
static ppp_ctrl_state_t  s_state            = PPPC_STATE_DISCONNECTED;

// Timestamp para timeout
static uint64_t          s_stateStartTimeMs = 0;

// “Futuros” parâmetros de reconexão (por enquanto só armazenados)
static bool              s_autoReconnect    = false;
static int               s_maxRetries       = 0;
static int               s_retryDelayMs     = 0;

// -----------------------------------------------------------------------------
//  EVENTO PÚBLICO PARA OUTROS MÓDULOS
// -----------------------------------------------------------------------------
ESP_EVENT_DEFINE_BASE(PPP_CTRL_EVENT);

// -----------------------------------------------------------------------------
//  PROTÓTIPOS INTERNOS
// -----------------------------------------------------------------------------
static void ppp_ctrl_task(void *param);
static void ppp_ctrl_emit(ppp_ctrl_event_t evt);
static void ppp_ctrl_process_state(void);
static void ppp_ctrl_set_state(ppp_ctrl_state_t newState);
static void ppp_ctrl_do_start(void);
static void ppp_ctrl_do_stop(void);

// -----------------------------------------------------------------------------
//  EMISSOR DE EVENTOS
// -----------------------------------------------------------------------------
static void ppp_ctrl_emit(ppp_ctrl_event_t evt)
{
    esp_event_post(PPP_CTRL_EVENT, evt, NULL, 0, portMAX_DELAY);
}

// -----------------------------------------------------------------------------
//  ALTERAÇÃO DO ESTADO + EVENTOS
// -----------------------------------------------------------------------------
static void ppp_ctrl_set_state(ppp_ctrl_state_t newState)
{
    if (newState == s_state) {
        return;
    }

    ESP_LOGI(TAG, "PPP CTRL state %d -> %d", s_state, newState);
    s_state = newState;

    switch (newState) {
        case PPPC_STATE_DISCONNECTED:
            ppp_ctrl_emit(PPP_CTRL_EVENT_STOPPED);
            break;

        case PPPC_STATE_STARTING:
            ppp_ctrl_emit(PPP_CTRL_EVENT_STARTING);
            s_stateStartTimeMs = esp_timer_get_time() / 1000ULL;
            break;

        case PPPC_STATE_CONNECTED:
            ppp_ctrl_emit(PPP_CTRL_EVENT_CONNECTED);
            break;

        case PPPC_STATE_STOPPING:
            ppp_ctrl_emit(PPP_CTRL_EVENT_STOPPING);
            break;

        case PPPC_STATE_FAILED:
            ppp_ctrl_emit(PPP_CTRL_EVENT_FAILED);
            break;
    }
}

// -----------------------------------------------------------------------------
//  COMPORTAMENTO SOBRE COMANDO START
// -----------------------------------------------------------------------------
static void ppp_ctrl_do_start(void)
{
    if (s_state == PPPC_STATE_CONNECTED) {
        ESP_LOGW(TAG, "START ignorado: PPP já conectado.");
        return;
    }

    if (s_state == PPPC_STATE_STARTING) {
        ESP_LOGW(TAG, "START ignorado: já está em STARTING.");
        return;
    }

    ppp_ctrl_set_state(PPPC_STATE_STARTING);

    // Chama o módulo de baixo nível
    ESP_LOGI(TAG, "Chamando lte_ppp_start() via ppp_control...");
    if (lte_ppp_start() == ESP_OK) {
        // A conexão real só será confirmada pelo lte_ppp_link
        // através de polling no state machine
        ESP_LOGI(TAG, "lte_ppp_start() retornou OK, aguardando CONNECTED...");
    } else {
        ESP_LOGE(TAG, "falha lte_ppp_start()");
        ppp_ctrl_set_state(PPPC_STATE_FAILED);
    }
}

// -----------------------------------------------------------------------------
//  COMPORTAMENTO SOBRE COMANDO STOP
// -----------------------------------------------------------------------------
static void ppp_ctrl_do_stop(void)
{
    if (s_state == PPPC_STATE_DISCONNECTED) {
        ESP_LOGW(TAG, "STOP ignorado: PPP já desligado.");
        return;
    }

    ppp_ctrl_set_state(PPPC_STATE_STOPPING);
    lte_ppp_stop();

    // Estado final
    ppp_ctrl_set_state(PPPC_STATE_DISCONNECTED);
}

// -----------------------------------------------------------------------------
//  MÁQUINA DE ESTADOS (RODADA NA TASK)
// -----------------------------------------------------------------------------
static void ppp_ctrl_process_state(void)
{
    int linkState = lte_ppp_get_state();

    switch (s_state) {

    case PPPC_STATE_STARTING:
        if (linkState == LTE_PPP_STATE_CONNECTED) {
            ppp_ctrl_set_state(PPPC_STATE_CONNECTED);
        } else {
            // Timeout opcional
            {
                uint64_t now = (esp_timer_get_time() / 1000ULL);
                if (now - s_stateStartTimeMs > PPP_CTRL_CONNECT_TIMEOUT_MS) {
                    ESP_LOGE(TAG, "Timeout esperando PPP CONNECTED");
                    ppp_ctrl_set_state(PPPC_STATE_FAILED);
                }
            }
        }
        break;

    case PPPC_STATE_CONNECTED:
        if (linkState != LTE_PPP_STATE_CONNECTED) {
            ESP_LOGW(TAG, "PPP caiu (linkDown) detectado pelo ppp_control");
            ppp_ctrl_set_state(PPPC_STATE_FAILED);
        }
        break;

    case PPPC_STATE_FAILED:
        // Aguarda STOP ou novo START externo.
        // (Aqui, no futuro, poderia entrar lógica de auto-reconexão)
        break;

    case PPPC_STATE_STOPPING:
        // Aqui já chamamos stop, estado finalizado acima
        break;

    case PPPC_STATE_DISCONNECTED:
    default:
        break;
    }
}

// -----------------------------------------------------------------------------
//  TASK PRINCIPAL DO PPP CONTROL
// -----------------------------------------------------------------------------
static void ppp_ctrl_task(void *param)
{
    (void)param;
    ppp_ctrl_cmd_t cmd;

    while (1) {
        // Espera comando ou timeout para rodar state machine
        if (xQueueReceive(s_ctrlQueue, &cmd, pdMS_TO_TICKS(500))) {
            switch (cmd) {
            case PPP_CTRL_CMD_START:
                ppp_ctrl_do_start();
                break;

            case PPP_CTRL_CMD_STOP:
                ppp_ctrl_do_stop();
                break;

            case PPP_CTRL_CMD_SHUTDOWN:
                ESP_LOGI(TAG, "PPP Control SHUTDOWN solicitado. Encerrando task...");
                vTaskDelete(NULL);
                return;
            }
        }

        // Poll da máquina de estados
        ppp_ctrl_process_state();
    }
}

// -----------------------------------------------------------------------------
//  API PÚBLICA
// -----------------------------------------------------------------------------

esp_err_t ppp_control_init(void)
{
    if (s_ctrlQueue != NULL && s_ctrlTaskHandle != NULL) {
        ESP_LOGW(TAG, "PPP Control já inicializado.");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Inicializando serviço PPP Control...");

    // Criar fila
    s_ctrlQueue = xQueueCreate(PPP_CTRL_QUEUE_LEN, sizeof(ppp_ctrl_cmd_t));
    if (!s_ctrlQueue) {
        ESP_LOGE(TAG, "Falha ao criar fila PPP Control");
        return ESP_FAIL;
    }

    // Criar a task
    BaseType_t ok = xTaskCreate(
        ppp_ctrl_task,
        "ppp_ctrl_task",
        PPP_CTRL_TASK_STACK,
        NULL,
        PPP_CTRL_TASK_PRIO,
        &s_ctrlTaskHandle);

    if (ok != pdPASS || !s_ctrlTaskHandle) {
        ESP_LOGE(TAG, "Falha ao criar task PPP Control");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "PPP Control inicializado (task e fila criadas).");
    return ESP_OK;
}

esp_err_t ppp_control_start(void)
{
    if (!s_ctrlQueue) {
        return ESP_FAIL;
    }
    ppp_ctrl_cmd_t cmd = PPP_CTRL_CMD_START;
    xQueueSend(s_ctrlQueue, &cmd, 0);
    return ESP_OK;
}

esp_err_t ppp_control_stop(void)
{
    if (!s_ctrlQueue) {
        return ESP_FAIL;
    }
    ppp_ctrl_cmd_t cmd = PPP_CTRL_CMD_STOP;
    xQueueSend(s_ctrlQueue, &cmd, 0);
    return ESP_OK;
}

esp_err_t ppp_control_shutdown(void)
{
    if (!s_ctrlQueue) {
        return ESP_FAIL;
    }
    ppp_ctrl_cmd_t cmd = PPP_CTRL_CMD_SHUTDOWN;
    xQueueSend(s_ctrlQueue, &cmd, portMAX_DELAY);
    return ESP_OK;
}

ppp_ctrl_state_t ppp_control_get_state(void)
{
    return s_state;
}

bool ppp_control_is_connected(void)
{
    return (s_state == PPPC_STATE_CONNECTED);
}

void ppp_control_set_auto_reconnect(bool enable)
{
    s_autoReconnect = enable;
    ESP_LOGI(TAG, "Auto-reconnect PPP Control: %s", enable ? "ON" : "OFF");
}

void ppp_control_set_retry_params(int max_retries, int retry_delay_ms)
{
    s_maxRetries   = max_retries;
    s_retryDelayMs = retry_delay_ms;
    ESP_LOGI(TAG, "PPP Control retry params: max=%d, delay=%d ms",
             s_maxRetries, s_retryDelayMs);
}

esp_netif_t *ppp_control_get_netif(void)
{
    // Apenas delega para o módulo de link PPP, caso ele exponha o netif.
    return lte_ppp_get_netif();
}

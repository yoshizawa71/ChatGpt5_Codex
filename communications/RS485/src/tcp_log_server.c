#include "tcp_log_server.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#define TCP_LOG_PORT 3333

static const char *TAG = "TCP_LOG";
static int log_socket = -1;

int tcp_log_vprintf(const char *fmt, va_list args) {
    if (log_socket < 0) {
        return 0; // Servidor não está ativo
    }

    char buffer[512];
    int len = vsnprintf(buffer, sizeof(buffer) - 2, fmt, args); // Reservar espaço para \r\n
    if (len > 0) {
        // Garantir que a mensagem termine com \r\n
        if (buffer[len - 1] == '\n') {
            // Substituir \n por \r\n
            buffer[len - 1] = '\r';
            buffer[len] = '\n';
            buffer[len + 1] = '\0';
            len += 1;
        } else {
            // Adicionar \r\n ao final
            buffer[len] = '\r';
            buffer[len + 1] = '\n';
            buffer[len + 2] = '\0';
            len += 2;
        }

        int sent = send(log_socket, buffer, len, 0);
        if (sent < 0) {
            // Cliente desconectado, fechar o socket
            close(log_socket);
            log_socket = -1;
        }
    }
    return len;
}

esp_err_t start_tcp_log_server(void) {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        return ESP_FAIL;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(TCP_LOG_PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(server_socket);
        return ESP_FAIL;
    }

    if (listen(server_socket, 1) < 0) {
        close(server_socket);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Waiting for client connection on port %d...", TCP_LOG_PORT);
    log_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
    if (log_socket < 0) {
        close(server_socket);
        return ESP_FAIL;
    }

    // Fechar o socket do servidor, pois já temos a conexão com o cliente
    close(server_socket);

    ESP_LOGI(TAG, "Servidor TCP de logs iniciado na porta %d", TCP_LOG_PORT);
    ESP_LOGI(TAG, "Cliente conectado: %s", inet_ntoa(client_addr.sin_addr));
    return ESP_OK;
}
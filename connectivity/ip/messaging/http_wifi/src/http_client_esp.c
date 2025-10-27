/*
 * http_client_esp.c
 *
 *  Created on: 7 de out. de 2025
 *      Author: geopo
 */
#include "http_client_esp.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

static const char *TAG = "HTTP/ESP";

static inline void http_set_basic_auth(esp_http_client_handle_t h,
                                       const char *user, const char *pass) {
#if ESP_IDF_VERSION_MAJOR >= 5
    esp_http_client_set_authtype(h, HTTP_AUTH_TYPE_BASIC);
    esp_http_client_set_username(h, user);
    esp_http_client_set_password(h, pass ? pass : "");
#else
    esp_http_client_set_basic_auth(h, user, pass ? pass : "");
#endif
}

static esp_err_t _http_event(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "CONNECTED");
            break;
        case HTTP_EVENT_HEADERS_SENT:
            ESP_LOGD(TAG, "HEADERS_SENT");
            break;

        // ===== FIX AQUI: usar header_key/header_value (v5.x) =====
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HDR %s: %s",
                     evt->header_key  ? evt->header_key  : "(null)",
                     evt->header_value? evt->header_value: "(null)");
            break;

        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "DATA len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "DISCONNECTED");
            break;
        default: break;
    }
    return ESP_OK;
}

esp_err_t http_client_esp_post_json(const http_conn_cfg_t *cfg,
                                    const char *json,
                                    int *out_status)
{
    if (!cfg || !cfg->url || !cfg->url[0] || !json) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_config_t hc = {
        .url          = cfg->url,
        .method       = HTTP_METHOD_POST,
        .timeout_ms   = (cfg->timeout_ms > 0 ? cfg->timeout_ms : 10000),
        .event_handler= _http_event,  // seu handler (em v5.x use header_key/header_value)
    };

    // HTTPS: usa bundle se habilitado, ou o PEM explícito se fornecido
    if (strncmp(cfg->url, "https://", 8) == 0) {
    #if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        if (!cfg->ca_cert_pem || !cfg->ca_cert_pem[0]) {
            hc.crt_bundle_attach = esp_crt_bundle_attach;
        } else
    #endif
        {
            hc.cert_pem = cfg->ca_cert_pem;
        }
    }

    esp_http_client_handle_t h = esp_http_client_init(&hc);
    if (!h) return ESP_FAIL;

    // Headers básicos
    const char *ct = (cfg->content_type && cfg->content_type[0])
                     ? cfg->content_type : "application/json";
    esp_http_client_set_header(h, "Content-Type", ct);
    esp_http_client_set_header(h, "Accept", "application/json");

    // Authorization: Bearer OU Basic (IDF v5.x)
    if (cfg->auth_bearer && cfg->auth_bearer[0]) {
        char hdr[160];
        snprintf(hdr, sizeof(hdr), "Bearer %s", cfg->auth_bearer);
        esp_http_client_set_header(h, "Authorization", hdr);
    } else if (cfg->basic_user && cfg->basic_user[0]) {
    http_set_basic_auth(h, cfg->basic_user, cfg->basic_pass);
    }

    // Corpo JSON
    size_t len = strlen(json);
    esp_err_t err = esp_http_client_set_post_field(h, json, (int)len);
    if (err != ESP_OK) {
        esp_http_client_cleanup(h);
        return err;
    }

    // Executa
    err = esp_http_client_perform(h);
    int status = -1;

    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(h);
        ESP_LOGI("HTTP/ESP", "HTTP status=%d, len=%d",
                 status, esp_http_client_get_content_length(h));
        if (status < 200 || status >= 300) {
            err = ESP_FAIL; // só 2xx é sucesso
        }
    } else {
        ESP_LOGE("HTTP/ESP", "perform() erro: %s", esp_err_to_name(err));
    }

    if (out_status) *out_status = status;
    esp_http_client_cleanup(h);
    return err;
}




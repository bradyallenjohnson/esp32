/*
 * Temperature reporter HTTP handling.
 *
 * Copyright (C) 2023 Brady Johnson <bradyallenjohnson@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <https://www.gnu.org/licenses/gpl2.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <string.h>
#include <sys/param.h> // MIN()

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_tls.h"  // esp_tls_get_and_clear_last_error()

#include "temperature_reporter.h"

/* Client CURL messages to register a device
 *
 * curl -X POST http://127.0.0.1:8182/bj/api/v1.0/device/201e88239088 \
 *      -H "Content-Type: application/json" \
 *      -d '{"device-registration": {"device": "201e88239088", "device-ip": "192.168.100.16"}}'
 *
 * Client CURL message to Report the temperature for a device:
 *
 * curl -X POST http://127.0.0.1:8182/bj/api/v1.0/temperature/201e88239088 \
 *      -H "Content-Type: application/json" \
 *      -d '{"zone-temperature": {"device": "201e88239088", "temperature": "22.5", "humidity": "45"}}'
 *
 * Server CURL messages to get and set the Collector IP destination address:
 *
 * curl -X GET http://127.0.0.1:8188/bj/api/v1.0/collectorserver \
 *      -H "Content-Type: application/json"
 *
 * curl -X POST http://127.0.0.1:8188/bj/api/v1.0/collectorserver \
 *      -H "Content-Type: application/json" \
 *      -d '{"collector-server": "192.168.1.150"}'
 */

static const char *HTTP_URL_REGISTER_FORMAT_STR = "http://%s:%d/bj/api/v1.0/device/%s";
static const char *HTTP_URL_REPORT_FORMAT_STR   = "http://%s:%d/bj/api/v1.0/temperature/%s";
static const char *JSON_REGISTER_FORMAT_STR = "{\"device-registration\": {\"device\": \"%s\", \"device-ip\": \"%s\"}}";
static const char *JSON_REPORT_FORMAT_STR = "{\"zone-temperature\": {\"device\": \"%s\", \"temperature\": \"%s\", \"humidity\": \"%s\"}}";
static const char *JSON_COLLECTOR_SERVER_FORMAT_STR = "{\"collector-server\": \"%s\"}";
static const char *JSON_COLLECTOR_SERVER_PREFIX = "{\"collector-server\": ";
static const char *HTTP_URI_COLLECTOR_SERVER_STR = "/bj/api/v1.0/collectorserver";
static const char *TAG = "TempReporterHttp";

/* Protect simultanesous accesses to http_context->dst_ip_str from the
 * http_client and the http_server, which can set the parameter */
static portMUX_TYPE my_spinlock = portMUX_INITIALIZER_UNLOCKED;

typedef struct http_context_ {
    char dst_ip_str[20];
    int  dst_http_port;
    char device_mac_str[20];
    char device_ip_str[20];
    char http_url_register[128];
    char http_url_report[128];
    char post_data[256];
    esp_http_client_config_t http_client_config;
    esp_http_client_handle_t http_client;
    httpd_handle_t http_server;
} http_context;


static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            esp_http_client_set_header(evt->client, "From", "user@example.com");
            esp_http_client_set_header(evt->client, "Accept", "text/html");
            esp_http_client_set_redirection(evt->client);
            break;
    }

    return ESP_OK;
}


/* Must call this function during first initialization, and after erroneous
 * http connections from esp_http_client_perform() */
static void http_new_client(http_context *context)
{
    context->http_client = esp_http_client_init(&context->http_client_config);
    printf("temperature_reporter creating new http_client\n");
}


static void http_reset_client(http_context *context)
{
    esp_http_client_cleanup(context->http_client);
    http_new_client(context);
}


esp_err_t http_server_handler_get(httpd_req_t *request)
{
    char response_str[64];
    http_context *context = (http_context *) request->user_ctx;

    taskENTER_CRITICAL(&my_spinlock);
    sprintf(response_str, JSON_COLLECTOR_SERVER_FORMAT_STR, context->dst_ip_str);
    taskEXIT_CRITICAL(&my_spinlock);

    httpd_resp_set_type(request, HTTPD_TYPE_JSON);
    httpd_resp_send(request, response_str, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}


esp_err_t http_server_handler_post(httpd_req_t *request)
{
    /* For string data, null termination will be absent,
     * and length of string is in content length */
    char content[100];
    http_context *context = (http_context *) request->user_ctx;

    /* Truncate if content length larger than the buffer */
    size_t recv_size = MIN(request->content_len, sizeof(content));

    int bytes_rx = httpd_req_recv(request, content, recv_size);
    if (bytes_rx <= 0) {  /* 0 return value indicates connection closed */
        /* Check if timeout occurred */
        if (bytes_rx == HTTPD_SOCK_ERR_TIMEOUT) {
            /* In case of timeout you can retry calling httpd_req_recv(),
             * but to keep it simple, here we respond with an
             * HTTP 408 (Request Timeout) error */
            httpd_resp_send_408(request);
        }
        /* Returning ESP_FAIL will ensure that the underlying socket is closed */
        return ESP_FAIL;
    }

    /* Parse the JSON message
     * Naive parsing, assuming exactly '{"collector-server": "<some ip>"}'*/
    char *index_ptr = strcasestr(content, JSON_COLLECTOR_SERVER_PREFIX);
    if (index_ptr == NULL) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid JSON request");
        return ESP_FAIL;
    }

    char *ip_str_ptr = index_ptr + strlen(JSON_COLLECTOR_SERVER_PREFIX) + 1;
    char *quote_index_ptr = strchr(ip_str_ptr, '\"');
    if (quote_index_ptr == NULL) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid JSON request IP");
        return ESP_FAIL;
    }

    taskENTER_CRITICAL(&my_spinlock);
    memcpy(context->dst_ip_str, ip_str_ptr, (quote_index_ptr - ip_str_ptr));
    sprintf(context->http_url_register, HTTP_URL_REGISTER_FORMAT_STR,
            context->dst_ip_str, context->dst_http_port, context->device_mac_str);
    sprintf(context->http_url_report, HTTP_URL_REPORT_FORMAT_STR,
            context->dst_ip_str, context->dst_http_port, context->device_mac_str);
    taskEXIT_CRITICAL(&my_spinlock);
    ESP_LOGI(TAG, "Collector server IP updated to: %s", context->dst_ip_str);

    /* Send a simple response */
    const char resp[] = "Collector Server IP updated";
    httpd_resp_send(request, resp, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}


/*
 * API functions
 */

void *http_reporter_init(const char *dst_ip_str, int dst_http_port, const char *device_mac_str, const char *device_ip_str)
{
    /* Initialize the HTTP client */
    http_context *context = malloc(sizeof(http_context));

    strcpy(context->dst_ip_str,      dst_ip_str);
    strcpy(context->device_mac_str,  device_mac_str);
    strcpy(context->device_ip_str,   device_ip_str);
    context->dst_http_port = dst_http_port;

    sprintf(context->http_url_register, HTTP_URL_REGISTER_FORMAT_STR,
            dst_ip_str, dst_http_port, device_mac_str);
    sprintf(context->http_url_report, HTTP_URL_REPORT_FORMAT_STR,
            dst_ip_str, dst_http_port, device_mac_str);

    memset(&context->http_client_config, 0, sizeof(context->http_client_config));
    context->http_client_config.event_handler = http_event_handler;
    context->http_client_config.disable_auto_redirect = true;
    context->http_client_config.url = context->http_url_register;
    http_new_client(context);

    /* Initialize the HTTP server, used to set/get the HTTP collector server IP */

    /* URI handler structure for GET /uri */
    httpd_uri_t uri_get = {
        .uri      = HTTP_URI_COLLECTOR_SERVER_STR,
        .method   = HTTP_GET,
        .handler  = http_server_handler_get,
        .user_ctx = context
    };

    /* URI handler structure for POST /uri */
    httpd_uri_t uri_post = {
        .uri      = HTTP_URI_COLLECTOR_SERVER_STR,
        .method   = HTTP_POST,
        .handler  = http_server_handler_post,
        .user_ctx = context
    };

    /* Generate default configuration */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8188;

    /* Empty handle to esp_http_server */
    context->http_server = NULL;

    /* Start the httpd server */
    if (httpd_start(&context->http_server, &config) != ESP_OK) {
        ESP_LOGI(TAG, "Error starting HTTP server");
    }

    /* Register the URI handlers */
    httpd_register_uri_handler(context->http_server, &uri_get);
    httpd_register_uri_handler(context->http_server, &uri_post);

    return (void *) context;
}

static bool http_send_post(http_context *context, const char *http_url, const char *post_data)
{
    char http_url_copy[128];
    /* The URL can be changed by the http server above, so access needs to be protected.
     * The esp_http_client_set_url() function below may also call taskENTER_CRITICAL(),
     * so lets just copy it here. */
    taskENTER_CRITICAL(&my_spinlock);
    strcpy(http_url_copy, http_url);
    taskEXIT_CRITICAL(&my_spinlock);

    printf("http_send_post\n\tpost_data: %s\n\thttp_url:  %s\n", post_data, http_url_copy); fflush(stdout);

    esp_http_client_handle_t http_client = context->http_client;
    ESP_ERROR_CHECK(esp_http_client_set_header(http_client, "Content-Type", "application/json"));
    ESP_ERROR_CHECK(esp_http_client_set_method(http_client, HTTP_METHOD_POST));
    ESP_ERROR_CHECK(esp_http_client_set_post_field(http_client, post_data, strlen(post_data)));
    ESP_ERROR_CHECK(esp_http_client_set_url(http_client, http_url_copy));

    int result = esp_http_client_perform(http_client);
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %"PRId64,
                esp_http_client_get_status_code(http_client),
                esp_http_client_get_content_length(http_client));
        return true;
    } else {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(result));
        http_reset_client(context);
        return false;
    }
}

bool http_register_device(void *context, const char *device_ip_str)
{
    http_context *http_context_ = (http_context *) context;

    // In case the IP changed
    strcpy(http_context_->device_ip_str, device_ip_str);

    sprintf(http_context_->post_data,
            JSON_REGISTER_FORMAT_STR,
            http_context_->device_mac_str,
            http_context_->device_ip_str);

    return http_send_post(http_context_,
                          http_context_->http_url_register,
                          http_context_->post_data);
}

bool http_report_temperature(void *context, const char *temperature_str, const char *humidity_str)
{
    http_context *http_context_ = (http_context *) context;

    sprintf(http_context_->post_data,
            JSON_REPORT_FORMAT_STR,
            http_context_->device_mac_str,
            temperature_str,
            humidity_str);
    return http_send_post(http_context_,
                          http_context_->http_url_report,
                          http_context_->post_data);
}

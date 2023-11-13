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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"  // esp_tls_get_and_clear_last_error()

#include "temperature_reporter.h"

/* CURL message to register a device
 * curl -X POST http://127.0.0.1:8182/bj/api/v1.0/device/201e88239088 \
 *      -H "Content-Type: application/json" \
 *      -d '{"device-registration": {"device": "201e88239088", "device-ip": "192.168.100.16"}}'
 *
 * CURL message to Report the temperature for a device:
 * curl -X POST http://127.0.0.1:8182/bj/api/v1.0/temperature/201e88239088 \
 *      -H "Content-Type: application/json" \
 *      -d '{"zone-temperature": {"device": "201e88239088", "temperature": "22.5", "humidity": "45"}}'
 */

static const char *HTTP_URL_REGISTER_FORMAT_STR = "http://%s:%d/bj/api/v1.0/device/%s";
static const char *HTTP_URL_REPORT_FORMAT_STR   = "http://%s:%d/bj/api/v1.0/temperature/%s";
static const char *JSON_REGISTER_FORMAT_STR = "{\"device-registration\": {\"device\": \"%s\", \"device-ip\": \"%s\"}}";
static const char *JSON_REPORT_FORMAT_STR = "{\"zone-temperature\": {\"device\": \"%s\", \"temperature\": \"%s\", \"humidity\": \"%s\"}}";
static const char *TAG = "TempReporterHttp";

typedef struct http_context_ {
    char dst_ip_str[20];
    char device_mac_str[20];
    char device_ip_str[20];
    char http_url_register[128];
    char http_url_report[128];
    char post_data[256];
    esp_http_client_config_t http_client_config;
    esp_http_client_handle_t http_client;
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
    printf("temperature_reporter creating new http_client %p\n", context->http_client);
}


static void http_reset_client(http_context *context)
{
    esp_http_client_cleanup(context->http_client);
    http_new_client(context);
}


/*
 * API functions
 */

void *http_reporter_init(const char *dst_ip_str, int dst_http_port, const char *device_mac_str, const char *device_ip_str)
{
    http_context *context = malloc(sizeof(http_context));

    strcpy(context->dst_ip_str,      dst_ip_str);
    strcpy(context->device_mac_str,  device_mac_str);
    strcpy(context->device_ip_str,   device_ip_str);

    sprintf(context->http_url_register, HTTP_URL_REGISTER_FORMAT_STR,
            dst_ip_str, dst_http_port, device_mac_str);
    sprintf(context->http_url_report, HTTP_URL_REPORT_FORMAT_STR,
            dst_ip_str, dst_http_port, device_mac_str);

    memset(&context->http_client_config, 0, sizeof(context->http_client_config));
    context->http_client_config.event_handler = http_event_handler;
    context->http_client_config.disable_auto_redirect = true;
    context->http_client_config.url = context->http_url_register;
    http_new_client(context);

    return (void *) context;
}

static bool http_send_post(http_context *context, const char *http_url, const char *post_data)
{
    printf("http_send_post\n\tpost_data: %s\n\thttp_url:  %s\n", post_data, http_url); fflush(stdout);

    esp_http_client_handle_t http_client = context->http_client;
    ESP_ERROR_CHECK(esp_http_client_set_header(http_client, "Content-Type", "application/json"));
    ESP_ERROR_CHECK(esp_http_client_set_method(http_client, HTTP_METHOD_POST));
    ESP_ERROR_CHECK(esp_http_client_set_url(http_client, http_url));
    ESP_ERROR_CHECK(esp_http_client_set_post_field(http_client, post_data, strlen(post_data)));

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
    strcpy(http_context_->dst_ip_str, device_ip_str);
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

/*
 * Temperature reporter main
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
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "temperature_reporter.h"

static const char *TAG = "TempReporterMain";
static const int TEMP_SENSOR_PIN = GPIO_NUM_4;
static const char *DST_IP_STR = "192.168.1.62";
static const int   DST_HTTP_PORT = 8182;
static void *http_context = NULL;

/* Temperature sensor reporter callback called by temperature_reporter_sensor::readDHT()
 * when the temperature and humidity are read.
 * This callback was set in chained_wifi_event_handler() when http_reporter_init()
 * was called.
 */
void temperature_sensor_reporter_cb(void *arg, float temperature, float humidity)
{
    void *http_context = arg;
    char temperature_str[10];
    char humidity_str[10];

    sprintf(temperature_str, "%f", temperature);
    sprintf(humidity_str, "%f", humidity);
    http_report_temperature(http_context, temperature_str, humidity_str);
}


/* This is a chained event handler that gets called after the temperature_reporter_wifi
 * event handler when important Wifi events happen, namely WIFI connected and IP set.
 * The HTTP reporter and Temperature sensor will not be initialized and started
 * until a WIFI connection is successful.
 */
void chained_wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        uint8_t mac_address[6];
        char mac_str_nocolon[16];
        char ip_str[16];

        ESP_ERROR_CHECK(esp_read_mac(mac_address, ESP_MAC_WIFI_STA));
        sprintf(mac_str_nocolon, "%02X%02X%02X%02X%02X%02X",
                mac_address[0], mac_address[1], mac_address[2],
                mac_address[3], mac_address[4], mac_address[5]);
        sprintf(ip_str, IPSTR, IP2STR(&event->ip_info.ip));

        // Initialize the HTTP reporter
        if (http_context == NULL) {
            http_context = http_reporter_init(DST_IP_STR, DST_HTTP_PORT, mac_str_nocolon, ip_str);
            temperature_sensor_initialize(TEMP_SENSOR_PIN, temperature_sensor_reporter_cb, http_context);
        }

        // Call register every time (its more like an update), in case this device IP changes
        http_register_device(http_context, ip_str);
        temperature_sensor_start_reading();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Stop reporting if the WIFI goes down
        temperature_sensor_stop_reading();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing Temperature reporter main");

    // Initialize NVS
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);

    // The HTTP reporter and Temperature sensor are initialized by
    // chained_wifi_event_handler() once the WIFI is connected.
    wifi_init_station(&chained_wifi_event_handler);

    ESP_LOGI(TAG, "Initialized Temperature reporter main");
}

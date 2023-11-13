/*
 * TemperatureReporter common include file
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

#ifndef TEMPERATURE_REPORTER_H_
#define TEMPERATURE_REPORTER_H_

#include "esp_event.h"

/*
 * WIFI functions defined in temperature_reporter_wifi.c
 */
bool wifi_init_station(esp_event_handler_t chained_event_handler);
bool wifi_is_connected();


/*
 * HTTP functions defined in temperature_reporter_http.c
 */
void *http_reporter_init(const char *dst_ip_str, int dst_http_port, const char *device_mac_str, const char *device_ip_str);
bool http_register_device(void *context, const char *device_ip_str);
bool http_report_temperature(void *context, const char *temperature_str, const char *humidity_str);

/*
 * Temperature sensor functions defined in temperature_reporter_sensor.c 
 */
typedef void (*temperature_sensor_reporter)(void *arg, float temperature, float humidity);

bool temperature_sensor_initialize(int temp_sensor_pin, temperature_sensor_reporter func, void *sensor_reporter_context);
void temperature_sensor_start_reading();
void temperature_sensor_stop_reading();

#endif

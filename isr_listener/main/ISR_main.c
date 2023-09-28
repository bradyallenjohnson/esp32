/*
 * ISR Listener main application for an Interrupt Service Routine (ISR) GPIO
 * pin-level change listener.
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

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "rom/ets_sys.h"
#include "driver/gpio.h"

#include "ISR_listener.h"

// Wait at least 1 second before communicating with the sensor
const int INITIAL_DELAY = 1500 / portTICK_PERIOD_MS;
// 5 second intervals
const int COLLECTION_INTERVAL = 5000 / portTICK_PERIOD_MS;

#define ISR_PIN  GPIO_NUM_2

void ISR_task(void *pvParameter)
{
	printf("Starting ISR Task\n\n");
    void *isr_context = ISR_setup_listener(ISR_PIN, 128, true);

    if (isr_context == NULL) {
        printf("Error in ISR_setup_listener\n");
        return;
    }

    ISR_start(isr_context);

    // The only way to exit this loop is to set "stop_at_max = true" in
    // ISR_setup_listener() then when the max number of events is reached,
    // they will be dumped and this loop will exit.

    int prev_index = 0;
    int current_index = 0;
    bool is_capturing = true;
    while(1) {
        current_index = ISR_num_captures(isr_context);
        is_capturing = ISR_is_capturing(isr_context);
        printf("=== Reading ISR, num captures %d\n", current_index);

        if (prev_index == current_index) {
            if (prev_index > 0) {
                ISR_dump(isr_context);
                ISR_reset(isr_context);
                printf("resetting capture index\n");
                fflush(stdout);
            }
        }

        // Exit loop if is_capturing is false, triggered internally
        // by setting stop_at_max = true
        if (is_capturing != true) {
            ISR_dump(isr_context);
            // TODO we're not supposed to exit this task, so just start again from zero here
            break;
        }

        prev_index = current_index;
        vTaskDelay(COLLECTION_INTERVAL);
    }

    ISR_delete(isr_context);
}

void app_main()
{
    //nvs_flash_init();
    vTaskDelay(INITIAL_DELAY);

    // Launch a task to communicate with the sensor
    xTaskCreate(
            &ISR_task,  // Task to execute
            "ISR_task", // Task name
            2048,       // Stack size in words
            NULL,       // Task input parameter
            5,          // Priority of the task
            NULL);      // Task handle
}


/*
 * RMT IR Main application for an Infrared Remote receiver
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

#include "rmt_ir.h"

// Wait at least 1 second before communicating with the sensor
const int INITIAL_DELAY = 1500 / portTICK_PERIOD_MS;

#define RMT_RX_PIN  GPIO_NUM_2

void rmt_ir_task(void *pvParameter)
{
	printf("Starting RMT IR Task for Musical Fidelity remote\n\n");

    rx_ir_config rx_config;
    int rc = setup_remote_musical_fidelity(&rx_config, RMT_RX_PIN);
    if (rc != RMT_IR_OK) {
        printf("Error in init_receiver\n");
        return;
    }

    while(1) {
        printf("\n=== Waiting for IR data\n");

        rc = start_receiving(&rx_config, true);

        if (rc == RMT_IR_OK) {
            if (rx_config.data_list != NULL) {
                printf("=== IR data received\n");
                for (int i = 0; i < rx_config.num_data_list_entries; i++) {
                    printf("\t RX byte[%02d] %02X\n", i, rx_config.data_list[i]);
                }
            }
        }
    }

    free_pulse_data_info(&rx_config.ir_config.start_pulse_data);
}

void app_main()
{
    //nvs_flash_init();
    vTaskDelay(INITIAL_DELAY);

    // Launch a task to communicate with the sensor
    xTaskCreate(
            &rmt_ir_task,  // Task to execute
            "rmt_ir_task", // Task name
            //2048,          // Stack size in words
            8192,          // Stack size in words
            NULL,          // Task input parameter
            5,             // Task Priority
            NULL);         // Task handle
}


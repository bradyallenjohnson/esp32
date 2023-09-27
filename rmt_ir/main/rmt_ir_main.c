/*
 * ISR Listener main
 *
 * Main application for an Interrupt Service Routine (ISR) GPIO pin-level
 * change listener
 *
 * Sep 2023: <Brady Johnson> bradyallenjohnson@gmail.com
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "rom/ets_sys.h"

#include "rmt_ir.h"

// Wait at least 1 second before communicating with the sensor
const int INITIAL_DELAY = 1500 / portTICK_PERIOD_MS;
// 5 second intervals
const int COLLECTION_INTERVAL = 5000 / portTICK_PERIOD_MS;

#define RMT_RX_PIN  GPIO_NUM_2

void rmt_ir_task(void *pvParameter)
{
	printf("Starting RMT IR Task\n\n");

	// For the Musical Fidelity remote, Ive seen from 790 to 910
	int pulse_width_ms = 850;
    rx_ir_config rx_config;
    rx_config.ir_config.gpio_pin              = RMT_RX_PIN;
    rx_config.ir_config.ir_enc_type           = DIFF_MANCHESTER_ENCODING;
    rx_config.ir_config.pulse_width           = pulse_width_ms;
    rx_config.ir_config.pulse_threshold       = 60;
    rx_config.ir_config.signal_range_min_ns   = 200 * 1000;
    rx_config.ir_config.signal_range_max_ns   = 40000 * 1000; // Ive seen max 86500 usec
    rx_config.ir_config.num_data_bits         = 23;    // 26 total bits, 3 start bits set below in add_pulse_data_info()

    int rc = init_receiver(&rx_config);

    if (rc != RMT_IR_OK) {
        printf("Error in init_receiver\n");
        return;
    }

	// Bits 1 and 2 are start bits: should be low, high, low, high
	// Bit 3 is the toggle bit, which changes each time
    // The Musical Fidelity remote alternates between these start pulses:
    //    low, high, low, high, low,  high
    //    low, high, low, low,  high, high
    add_pulse_data_info(&rx_config.ir_config.start_pulse_data, PULSE_LEVEL_LOW,    pulse_width_ms); // pulse 1 low
    add_pulse_data_info(&rx_config.ir_config.start_pulse_data, PULSE_LEVEL_HIGH,   pulse_width_ms); // pulse 2 high
    add_pulse_data_info(&rx_config.ir_config.start_pulse_data, PULSE_LEVEL_LOW,    pulse_width_ms); // pulse 3 low
    add_pulse_data_info(&rx_config.ir_config.start_pulse_data, PULSE_LEVEL_EITHER, pulse_width_ms); // pulse 4 low/high
    add_pulse_data_info(&rx_config.ir_config.start_pulse_data, PULSE_LEVEL_EITHER, pulse_width_ms); // pulse 5 low/high
    add_pulse_data_info(&rx_config.ir_config.start_pulse_data, PULSE_LEVEL_EITHER, pulse_width_ms); // pulse 6 low/high
    // There is no stop_pulse_data

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


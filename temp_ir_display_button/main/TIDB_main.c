/*
 *
 * TIDB_main.c
 * Temperature, Infrared, Display (LCD), and Button.
 * The Temperature will be read periodically and displayed on an LCD.
 * If an Infrared Volume up command is received, increase the temp read time by 10 seconds.
 * If an Infrared Volume down command is received, decrease the temp read time by 10 seconds.
 * The LCD display will only display information for 20 seconds.
 * If the push-button is pushed, then turn on the LCD display for 20 seconds.
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
#include <string.h> // memcmp

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "DHT22.h"
#include "ISR_listener.h"
#include "rmt_ir.h"
#include "lcd_2004_i2c.h"

// Have to wait at least 1 second before communicating with the sensor
static const int INITIAL_DELAY_MS = 1500 / portTICK_PERIOD_MS;

// Interval between data collections
static const int COLLECTION_INTERVAL_MIN_SEC = 10; // Minimal supported by the DHT22 is 2 seconds
static const int COLLECTION_INTERVAL_MAX_SEC = 300;
static const int COLLECTION_INTERVAL_DEFAULT_SEC = 60;

static const int LCD_ON_MAX_SEC = 20;

static const int BUTTON_PIN  = GPIO_NUM_23;
static const int REMOTE_PIN  = GPIO_NUM_2;
static const int TEMP_PIN    = GPIO_NUM_4;
static const int LCD_SDA_PIN = GPIO_NUM_21;
static const int LCD_SCL_PIN = GPIO_NUM_22;

static const uint8_t LG_VOL_UP_BYTES[4] = {0x04, 0xFB, 0x02, 0xFD};
static const uint8_t LG_VOL_DN_BYTES[4] = {0x04, 0xFB, 0x03, 0xFC};

static const int IR_COMMAND_EMPTY   = 0;
static const int IR_COMMAND_VOL_UP  = 1;
static const int IR_COMMAND_VOL_DN  = 2;
static const char IR_COMMAND_STR_EMPTY[]  = "Noop";
static const char IR_COMMAND_STR_VOL_UP[] = "VolUP";
static const char IR_COMMAND_STR_VOL_DN[] = "VolDN";

static portMUX_TYPE my_spinlock = portMUX_INITIALIZER_UNLOCKED;
static int IR_COMMAND = IR_COMMAND_EMPTY;

//
// Util functions
//

static int get_ir_command()
{
    taskENTER_CRITICAL(&my_spinlock);
    int command = IR_COMMAND;
    taskEXIT_CRITICAL(&my_spinlock);

    return command;
}

static void set_ir_command(int command)
{
    taskENTER_CRITICAL(&my_spinlock);
    IR_COMMAND = command;
    taskEXIT_CRITICAL(&my_spinlock);
}

static bool is_button_pressed(void *isr_context)
{
    uint8_t num_captures = ISR_num_captures(isr_context);
    if (num_captures == 0) {
        return false;
    } else {
        ISR_reset(isr_context);
        return true;
    }
}

static bool is_ir_match(const rx_ir_config *rx_config, const uint8_t *match_bytes)
{
    if (rx_config->num_data_list_entries != 4) {
        return false;
    }

    if (memcmp(rx_config->data_list, match_bytes, 4) == 0) {
        return true;
    } else {
        return false;
    }
}

static void display_hum_tmp(void *dht_handle, void *lcd_handle)
{
    static char lcd_str_data[20];

    sprintf(lcd_str_data, "Hum %.1f%%", get_humidity(dht_handle));
    lcd_2004_i2c_write_string(lcd_handle, 0, 0, lcd_str_data, strlen(lcd_str_data));

    sprintf(lcd_str_data, "Tmp %.1f C", get_temperature(dht_handle));
    lcd_2004_i2c_write_string(lcd_handle, 1, 0, lcd_str_data, strlen(lcd_str_data));

    // Clear the Cycle time and IR Cmd lines
    sprintf(lcd_str_data, "                   ");
    lcd_2004_i2c_write_string(lcd_handle, 2, 0, lcd_str_data, strlen(lcd_str_data));
    lcd_2004_i2c_write_string(lcd_handle, 3, 0, lcd_str_data, strlen(lcd_str_data));
}

static void display_remote_command(void *dht_handle, void *lcd_handle, int remote_command, int cycle_time)
{
    static char lcd_str_data[20];
    const char *vol_str = IR_COMMAND_STR_EMPTY;

    if (remote_command == IR_COMMAND_VOL_UP) {
        vol_str = IR_COMMAND_STR_VOL_UP;
    } else if (remote_command == IR_COMMAND_VOL_DN) {
        vol_str = IR_COMMAND_STR_VOL_DN;
    }

    sprintf(lcd_str_data, "IR Cmd: %s", vol_str);
    lcd_2004_i2c_write_string(lcd_handle, 2, 0, lcd_str_data, strlen(lcd_str_data));

    sprintf(lcd_str_data, "Cycle Time: %d sec", cycle_time);
    lcd_2004_i2c_write_string(lcd_handle, 3, 0, lcd_str_data, strlen(lcd_str_data));
}

void TIDB_task(void *pvParameter)
{
    int collection_time_sec = COLLECTION_INTERVAL_DEFAULT_SEC;
    bool is_lcd_on = false;
    uint64_t cycle_now_us = 0; // us => micro seconds
    uint64_t lcd_on_us = 0;
    uint64_t temp_measure_us = 0;
    uint32_t diff_sec = 0;
    int ir_command = IR_COMMAND_EMPTY;

    printf("Starting TIDB Task\n\n");

    //
    // Sensors and LCD initialization
    //

    // initialize the temp sensor
    void *dht_handle = setup_DHT(TEMP_PIN);
    // The first reading is invalid, and we have to sleep 2 sec between readings
    read_DHT(dht_handle);
    vTaskDelay(2250 / portTICK_PERIOD_MS);
    read_DHT(dht_handle);
    printf("Hum %.1f%%\n", get_humidity(dht_handle));
    printf("Tmp %.1f C\n", get_temperature(dht_handle));

    // initialize the button
    void *button_context = ISR_setup_listener(BUTTON_PIN, 16, false);
    ISR_set_gpio_pullmode(BUTTON_PIN, GPIO_PULLDOWN_ONLY); // button should be connected to VCC
    ISR_start(button_context);

    // initialize the LCD
    void *lcd_handle = lcd_2004_i2c_init(LCD_SCL_PIN, LCD_SDA_PIN, LCD_I2C_HW_ADDR_DEFAULT);
    display_hum_tmp(dht_handle, lcd_handle);
    is_lcd_on = true;
    lcd_on_us = esp_timer_get_time();

    //
    // Start looping
    //
    while(1) {
        cycle_now_us = esp_timer_get_time();

        //
        // Check if an IR command was received
        // If so, either increment or decrement the collection time by 10 seconds
        //
        ir_command = get_ir_command();
        if (ir_command != IR_COMMAND_EMPTY) {
            printf("Received an IR signal\n");

            // Update the collection_time_sec
            if (ir_command == IR_COMMAND_VOL_UP) {
                set_ir_command(IR_COMMAND_EMPTY);
                if (collection_time_sec >= COLLECTION_INTERVAL_MAX_SEC) {
                    printf("Temperature cycle interval already at maximum: %d\n", collection_time_sec);
                } else {
                    collection_time_sec += 10;
                }
            } else if (ir_command == IR_COMMAND_VOL_DN) {
                set_ir_command(IR_COMMAND_EMPTY);
                if (collection_time_sec <= COLLECTION_INTERVAL_MIN_SEC) {
                    printf("Temperature cycle interval already at minimum: %d\n", collection_time_sec);
                } else {
                    collection_time_sec -= 10;
                }
            }

            printf("Temperature cycle interval now set to: %d\n", collection_time_sec);

            // Update the LCD with the new cycle time
            lcd_2004_i2c_display_on(lcd_handle);
            display_remote_command(dht_handle, lcd_handle, ir_command, collection_time_sec);
            is_lcd_on = true;
            lcd_on_us = cycle_now_us;
        }

        //
        // Check if the button was pressed
        // If so, turn on the LCD for a while
        //
        if (is_button_pressed(button_context) == true) {
            // Even if the LCD is already on, just reset the start time
            lcd_on_us = cycle_now_us;
            printf("Button pressed\n");

            // Turn on the LCD, if its not already on
            if (is_lcd_on == false) {
                printf("Turn on LCD\n");
                lcd_2004_i2c_display_on(lcd_handle);
                is_lcd_on = true;
            }
        }

        //
        // Check if the LCD should be turned off
        //
        if (is_lcd_on == true) {
            diff_sec = ((cycle_now_us - lcd_on_us) / (1000*1000));
            if (diff_sec >= LCD_ON_MAX_SEC) {
                // Turn off the LCD
                printf("Turn off LCD\n");
                lcd_2004_i2c_display_off(lcd_handle);
                is_lcd_on = false;
            }
        }

        //
        // Check if we should read from the temperature sensor
        //
        diff_sec = ((cycle_now_us - temp_measure_us) / (1000*1000));
        if (diff_sec >= collection_time_sec) {
            printf("Reading DHT at %lld usec diff_sec %ld\n", cycle_now_us, diff_sec);
            int ret = read_DHT(dht_handle);
            temp_measure_us = cycle_now_us;

            if (ret == DHT_OK) {
                printf("Hum %.1f%%\n", get_humidity(dht_handle));
                printf("Tmp %.1f C\n", get_temperature(dht_handle));
                // Update the LCD, but do not turn it on
                display_hum_tmp(dht_handle, lcd_handle);
            }
        }

        //
        // Sleep 250 ms
        //
        vTaskDelay(250 / portTICK_PERIOD_MS);
    }
}

void TIDB_ir_task(void *pvParameter)
{
    // initialize the IR sensor
    rx_ir_config rx_config;
    setup_remote_lg_tv(&rx_config, REMOTE_PIN);
    start_receiving(&rx_config, false);
    int rc = 0;

    //
    // Start looping
    //
    while(1) {
        // This call will block until some IR data is ready
        rc = start_receiving(&rx_config, true);

        if (rc == RMT_IR_OK) {
            if (rx_config.data_list == NULL) {
                printf("No IR data received\n");
                continue;
            }

            // Update the collection_time_sec
            if (is_ir_match(&rx_config, LG_VOL_UP_BYTES) == true) {
                set_ir_command(IR_COMMAND_VOL_UP);
            } else if (is_ir_match(&rx_config, LG_VOL_DN_BYTES) == true) {
                set_ir_command(IR_COMMAND_VOL_DN);
            }
        }
    }
}

void app_main()
{
    //nvs_flash_init();
    vTaskDelay(INITIAL_DELAY_MS);

    // Launch a task to communicate with the sensors
    xTaskCreatePinnedToCore(
            &TIDB_task,  // Task to execute
            "TIDB_task", // Task name
            4096,        // Stack size in words
            NULL,        // Task input parameter
            5,           // Priority of the task
            NULL,        // Task handle
            0);          // Core to run this task on
    xTaskCreatePinnedToCore(
            &TIDB_ir_task,  // Task to execute
            "TIDB_ir_task", // Task name
            4096,           // Stack size in words
            NULL,           // Task input parameter
            5,              // Priority of the task
            NULL,           // Task handle
            1);             // Core to run this task on
}


/*
 * LCD I2C Main application for a SunFounder 2004a display
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
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "lcd_2004_i2c.h"


#define LCD_SDA_PIN  GPIO_NUM_21
#define LCD_SCL_PIN  GPIO_NUM_22

void lcd_i2c_task(void *pvParameter)
{
    printf("\nStarting LCD I2C task\n");

    void *lcd_handle = lcd_2004_i2c_init(LCD_SCL_PIN, LCD_SDA_PIN, LCD_I2C_HW_ADDR_DEFAULT);

    // Write some data
    printf("Sending text to LCD screen\n");
    char str_data[20];

    sprintf(str_data, "Hello World!");
    lcd_2004_i2c_write_string(lcd_handle, 0, 1, str_data, strlen(str_data));

    sprintf(str_data, "My first test");
    lcd_2004_i2c_write_string(lcd_handle, 1, 1, str_data, strlen(str_data));

    sprintf(str_data, "not my last test");
    lcd_2004_i2c_write_string(lcd_handle, 2, 1, str_data, strlen(str_data));

    sprintf(str_data, "(: Bye for now :)");
    lcd_2004_i2c_write_string(lcd_handle, 3, 1, str_data, strlen(str_data));

    vTaskDelay(4000 / portTICK_PERIOD_MS);
    sprintf(str_data, "One last message  ");
    lcd_2004_i2c_write_string(lcd_handle, 3, 1, str_data, strlen(str_data));

    while (1) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void app_main()
{
    // Launch a task to communicate with the sensor
    xTaskCreate(
            &lcd_i2c_task,  // Task to execute
            "lcd_i2c_task", // Task name
            //2048,         // Stack size in words
            8192,           // Stack size in words
            NULL,           // Task input parameter
            5,              // Task Priority
            NULL);          // Task handle
}


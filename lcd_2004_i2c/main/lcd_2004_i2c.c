/*
 * LCD I2C library for a SunFounder 2004a display
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
//#include "driver/i2c.h"
#include "driver/i2c_master.h"

#include "lcd_2004_i2c.h"

// The SunFounder 2004 LCD has a PCF8574 I2C chip
// https://www.ti.com/lit/ds/symlink/pcf8574.pdf

#define LCD_CLOCK_HZ (100 * 1000)

// The pixel number in horizontal and vertical
#define LCD_H_RES              5
#define LCD_V_RES              8

// commands
#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME 0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_CURSORSHIFT 0x10
#define LCD_FUNCTIONSET 0x20
#define LCD_SETCGRAMADDR 0x40
#define LCD_SETDDRAMADDR 0x80

// flags for display entry mode
#define LCD_ENTRYRIGHT 0x00
#define LCD_ENTRYLEFT 0x02
#define LCD_ENTRYSHIFTINCREMENT 0x01
#define LCD_ENTRYSHIFTDECREMENT 0x00

// flags for display on/off control
#define LCD_DISPLAYON 0x04
#define LCD_DISPLAYOFF 0x00
#define LCD_CURSORON 0x02
#define LCD_CURSOROFF 0x00
#define LCD_BLINKON 0x01
#define LCD_BLINKOFF 0x00

// flags for display/cursor shift
#define LCD_DISPLAYMOVE 0x08
#define LCD_CURSORMOVE 0x00
#define LCD_MOVERIGHT 0x04
#define LCD_MOVELEFT 0x00

// flags for function set
#define LCD_8BITMODE 0x10
#define LCD_4BITMODE 0x00
#define LCD_2LINE 0x08
#define LCD_1LINE 0x00
#define LCD_5x10DOTS 0x04
#define LCD_5x8DOTS 0x00

#define LCD_BACKLIGHT 0x08
#define LCD_NOBACKLIGHT 0x00

// The HD44780 LCD needs to be used in 4-bit mode since the PCF8574 I2C expander
// has 8 data pins, but one of them needs to be connected to the LCD register select
// pin, and another to the LCD Enable pin. This means only 4 data pins will be used:
// P4-P7 will connect to LCD data pins D4-D7.
// PCF8574 pin P0 will connect to LCD pin RS and P2 to the LCD Enable pin.
//    PCF P0 => RS, P1 => RW, P2 => EN
//    PCF P4-P7 => LCD D4-D7

#define ENABLE_BIT     0x04  // Enable bit
#define READWRITE_BIT  0x02  // Read/Write bit
#define REG_SELECT_BIT 0x01  // Register select bit

static int num_callbacks = 0;
static int num_event_alive = 0;
static int num_event_done = 0;
static int num_event_nack = 0;

typedef struct lcd_i2c_2004_context_ {
    i2c_master_bus_config_t i2c_bus_config;
    i2c_master_bus_handle_t bus_handle;
    i2c_device_config_t device_config;
    i2c_master_dev_handle_t lcd_device;
} lcd_i2c_2004_context;

/*
 * Util functions
 */

static bool i2c_done_cb(i2c_master_dev_handle_t i2c_dev, const i2c_master_event_data_t *evt_data, void *arg)
{
    num_callbacks++;
    if (evt_data->event == I2C_EVENT_ALIVE) {
        num_event_alive++;
    } else if (evt_data->event == I2C_EVENT_DONE) {
        num_event_done++;
    } else if (evt_data->event == I2C_EVENT_NACK) {
        num_event_nack++;
    }
    return true;
}


static void write_4bits(i2c_master_dev_handle_t device, uint8_t data_byte)
{
    uint8_t pulse_bytes[3] = {
        data_byte,
        data_byte | ENABLE_BIT,  // Write with Enable high
        data_byte | ~ENABLE_BIT, // Write with Enable low
    };

    /*
    printf("write_4bits [%02X] [%02X] [%02X]\n",
            pulse_bytes[0], pulse_bytes[1], pulse_bytes[2]);
    */

    // TODO without this sleep, nothing works and the CPU panics
    vTaskDelay(10 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(i2c_master_transmit(device, pulse_bytes, 3, -1));
}


static void write_LCD_command(i2c_master_dev_handle_t device, uint8_t command_byte)
{
    // Write the high nibble
    uint8_t nibble = (command_byte & 0xf0);
    write_4bits(device, nibble);

    // Write the low nibble
    nibble  = ((command_byte << 4) & 0xf0);
    write_4bits(device, nibble);
}

/*
 * API functions
 */

void lcd_2004_i2c_write_string(void *lcd_handle, uint8_t row, uint8_t col, char *data, uint8_t data_len)
{
    static int row_offsets[] = { 0x00, 0x40, 0x14, 0x54 };

    lcd_i2c_2004_context *lcd_context = (lcd_i2c_2004_context *) lcd_handle;

    uint8_t byte_cmd = LCD_SETDDRAMADDR | (col + row_offsets[row]);
    //uint8_t byte_cmd = LCD_SETDDRAMADDR | (2 + row_offsets[0]);
    write_LCD_command(lcd_context->lcd_device, byte_cmd);

    uint8_t high_nibble;
    uint8_t low_nibble;
    for (int i = 0; i < data_len; i++) {
        uint8_t d = (uint8_t) data[i];
        high_nibble = (d & 0xf0) | REG_SELECT_BIT;
        low_nibble  = ((d << 4) & 0xf0) | REG_SELECT_BIT;

        //printf("Sending character: %02X\n", d);
        write_4bits(lcd_context->lcd_device, high_nibble);
        write_4bits(lcd_context->lcd_device, low_nibble);
    }
}

void *lcd_2004_i2c_init(uint8_t scl_pin, uint8_t sda_pin, uint8_t lcd_i2c_hw_addr)
{
    // Have to wait at least 40 ms after LCD power-on
    vTaskDelay(50 / portTICK_PERIOD_MS);

    lcd_i2c_2004_context *lcd_context = malloc(sizeof(lcd_i2c_2004_context));

    lcd_context->i2c_bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    lcd_context->i2c_bus_config.i2c_port   = I2C_NUM_0;
    lcd_context->i2c_bus_config.scl_io_num = scl_pin;
    lcd_context->i2c_bus_config.sda_io_num = sda_pin;
    lcd_context->i2c_bus_config.glitch_ignore_cnt = 7;
    lcd_context->i2c_bus_config.intr_priority     = 0;
    lcd_context->i2c_bus_config.trans_queue_depth = 50;
    lcd_context->i2c_bus_config.flags.enable_internal_pullup = 1;

    ESP_ERROR_CHECK(i2c_new_master_bus(&lcd_context->i2c_bus_config, &lcd_context->bus_handle));

    if (i2c_master_probe(lcd_context->bus_handle, lcd_i2c_hw_addr, 100) != ESP_OK) {
        printf("Error getting device address: 0x%02x\n", lcd_i2c_hw_addr);
    } else {
        printf("Successfully obtained device address: 0x%02x\n", lcd_i2c_hw_addr);
    }

    lcd_context->device_config.dev_addr_length = 1;
    lcd_context->device_config.device_address = lcd_i2c_hw_addr;
    lcd_context->device_config.scl_speed_hz = LCD_CLOCK_HZ;

    i2c_master_bus_add_device(
            lcd_context->bus_handle,
            &lcd_context->device_config,
            &lcd_context->lcd_device);

    i2c_master_event_callbacks_t callbacks = {
            .on_trans_done = i2c_done_cb,
    };
    ESP_ERROR_CHECK(i2c_master_register_event_callbacks(
            lcd_context->lcd_device, &callbacks, NULL));

    // Reset the I2C bus
    i2c_master_bus_reset(lcd_context->bus_handle);

    uint8_t byte_cmd = LCD_NOBACKLIGHT;
    write_LCD_command(lcd_context->lcd_device, byte_cmd);
    vTaskDelay(1 / portTICK_PERIOD_MS);

    //
    // Set the LCD to 4-bit mode, this takes 4 messages, with a delay in-between each
    //
    byte_cmd = 0x03 << 4; // have to send this 3 times
        // First time
    write_4bits(lcd_context->lcd_device, byte_cmd);
    vTaskDelay(4 / portTICK_PERIOD_MS); // Wait 4.1 ms
        // Second time
    write_4bits(lcd_context->lcd_device, byte_cmd);
    vTaskDelay(4 / portTICK_PERIOD_MS);
        // Third time
    write_4bits(lcd_context->lcd_device, byte_cmd);
    vTaskDelay(1 / portTICK_PERIOD_MS);
        // Fourth and final time
    byte_cmd = 0x02 << 4;
    write_4bits(lcd_context->lcd_device, byte_cmd);

    //
    // Now set the LCD attributes
    //
    printf("Setting LCD screen attributes\n");
    byte_cmd = LCD_FUNCTIONSET | LCD_4BITMODE | LCD_2LINE | LCD_5x8DOTS;
    write_LCD_command(lcd_context->lcd_device, byte_cmd);

    byte_cmd = LCD_DISPLAYCONTROL | LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF;
    write_LCD_command(lcd_context->lcd_device, byte_cmd);

    byte_cmd = LCD_CLEARDISPLAY;
    write_LCD_command(lcd_context->lcd_device, byte_cmd);
    vTaskDelay(2 / portTICK_PERIOD_MS); // This command needs a long delay

    byte_cmd = LCD_ENTRYMODESET | LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT;
    write_LCD_command(lcd_context->lcd_device, byte_cmd);

    byte_cmd = LCD_RETURNHOME;
    write_LCD_command(lcd_context->lcd_device, byte_cmd);
    vTaskDelay(2 / portTICK_PERIOD_MS); // This command needs a long delay

    byte_cmd = LCD_CLEARDISPLAY;
    write_LCD_command(lcd_context->lcd_device, byte_cmd);

    byte_cmd = LCD_BACKLIGHT | LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF;
    write_LCD_command(lcd_context->lcd_device, byte_cmd);

    return lcd_context;
}

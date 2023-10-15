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

#define LCD_I2C_HW_ADDR_DEFAULT 0x27

void *lcd_2004_i2c_init(uint8_t scl_pin, uint8_t sda_pin, uint8_t lcd_i2c_hw_addr);

void lcd_2004_i2c_write_string(void *lcd_handle, uint8_t row, uint8_t col, char *data, uint8_t data_len);

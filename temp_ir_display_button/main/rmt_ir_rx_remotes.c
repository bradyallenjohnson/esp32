/*
 * Setup functions for different types of remote controls
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
#include "rmt_ir.h"

int setup_remote_musical_fidelity(rx_ir_config *rx_config, int gpio_pin)
{
	// For the Musical Fidelity remote, Ive seen from 790 to 910
	int pulse_width_ms = 850;
    rx_config->ir_config.gpio_pin              = gpio_pin;
    rx_config->ir_config.ir_enc_type           = DIFF_MANCHESTER_ENCODING;
    rx_config->ir_config.pulse_width           = pulse_width_ms;
    rx_config->ir_config.pulse_threshold       = 60;
    rx_config->ir_config.signal_range_min_ns   = 200 * 1000;   // anything larger causes a "value too large" error
    rx_config->ir_config.signal_range_max_ns   = 40000 * 1000; // Ive seen max 86500 usec
    rx_config->ir_config.num_data_bits         = 23;    // 26 total bits, 3 start bits set below in add_pulse_data_info()

    int rc = init_receiver(rx_config);

    if (rc != RMT_IR_OK) {
        printf("Error in init_receiver\n");
        return rc;
    }

	// Bits 1 and 2 are start bits: should be low, high, low, high
	// Bit 3 is the toggle bit, which changes each time
    // The Musical Fidelity remote alternates between these start pulses:
    //    low, high, low, high, low,  high
    //    low, high, low, low,  high, high
    add_pulse_data_info(&rx_config->ir_config.start_pulse_data, PULSE_LEVEL_LOW,    pulse_width_ms); // pulse 1 low
    add_pulse_data_info(&rx_config->ir_config.start_pulse_data, PULSE_LEVEL_HIGH,   pulse_width_ms); // pulse 2 high
    add_pulse_data_info(&rx_config->ir_config.start_pulse_data, PULSE_LEVEL_LOW,    pulse_width_ms); // pulse 3 low
    add_pulse_data_info(&rx_config->ir_config.start_pulse_data, PULSE_LEVEL_EITHER, pulse_width_ms); // pulse 4 low/high
    add_pulse_data_info(&rx_config->ir_config.start_pulse_data, PULSE_LEVEL_EITHER, pulse_width_ms); // pulse 5 low/high
    add_pulse_data_info(&rx_config->ir_config.start_pulse_data, PULSE_LEVEL_EITHER, pulse_width_ms); // pulse 6 low/high
    // There is no stop_pulse_data

    // This needs to be called externally when finished
    //free_pulse_data_info(&rx_config->ir_config.start_pulse_data);

    return RMT_IR_OK;
}


int setup_remote_lg_tv(rx_ir_config *rx_config, int gpio_pin)
{
    int pulse_width_ms = 562;
    rx_config->ir_config.gpio_pin              = gpio_pin;
    rx_config->ir_config.ir_enc_type           = PULSE_DISTANCE_ENCODING;
    rx_config->ir_config.pulse_width           = pulse_width_ms;
    rx_config->ir_config.pulse_threshold       = 60;
    rx_config->ir_config.signal_range_min_ns   = 200 * 1000;   // anything larger causes a "value too large" error
    rx_config->ir_config.signal_range_max_ns   = 9100 * 1000;
    rx_config->ir_config.num_data_bits         = 32;

    int rc = init_receiver(rx_config);

    if (rc != RMT_IR_OK) {
        printf("Error in init_receiver\n");
        return rc;
    }

    /* Start Data
     * - 9ms leading pulse burst, 16 times the pulse_width
     * - 4.5ms space, 8 times the pulse_width
     */
    add_pulse_data_info(&rx_config->ir_config.start_pulse_data, PULSE_LEVEL_HIGH, pulse_width_ms * 16);
    add_pulse_data_info(&rx_config->ir_config.start_pulse_data, PULSE_LEVEL_LOW,  pulse_width_ms * 8);

     // The stop data is one high pulse_width
    add_pulse_data_info(&rx_config->ir_config.stop_pulse_data, PULSE_LEVEL_HIGH,  pulse_width_ms);

    // This needs to be called externally when finished
    //free_pulse_data_info(&rx_config->ir_config.start_pulse_data);

    return RMT_IR_OK;
}

/*
 * Decoder functions for different remote control encoding formats
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

#include <string.h>  // memset()
#include "rmt_ir.h"

/*
 * Util functions
 */


// Check if the received duration is in this range:
// (pulse_width - threshold) <= duration <= (pulse_width + duration)
// For example: pulse_width = 850, threshold = 30:
//     duration = 832: 820 <= 832 <= 880, return true
//     duration = 818, return false
//     duration = 890, return false
static bool pulse_in_threshold(uint32_t pulse_width, uint8_t threshold, uint32_t duration)
{
    if (((pulse_width - threshold) <= duration) &&
        (duration <= (pulse_width + threshold))) {
        return true;
    }

    /* printf("pulse_in_threshold pulse_width %ld, threshold %d, duration %ld, min %ld, max %ld\n",
            pulse_width, threshold, duration,
            (pulse_width - threshold),
            (pulse_width + threshold)); */

    return false;
}

/*
 * API functions
 */

int decode_rx_data_manchester(rx_ir_config *rx_config, rmt_rx_done_event_data_t *rx_done_data)
{
    /* Decode both Differential and Normal Manchester encoding:
     */

    // First lets remove all of the double pulses and make a simple list of single pulses
    // No need to use the pulse_info->next field here
    pulse_info *single_pulses =
            malloc(sizeof(pulse_info) * rx_done_data->num_symbols * 4);
    memset(single_pulses, 0, sizeof(pulse_info) * rx_done_data->num_symbols * 4);
    int single_pulse_index = 0;

    for (int i = 0; i < rx_done_data->num_symbols; i++) {
        rmt_symbol_word_t *symbol = &rx_done_data->received_symbols[i];

        /*
        printf("\t [%2d] level0 = %d duration0 = %5d, level1 = %d duration1 = %5d\n",
               i, symbol->level0, symbol->duration0,
               symbol->level1, symbol->duration1);
        */

        bool is_dur0_single_pulse = pulse_in_threshold(
                rx_config->ir_config.pulse_width,
                rx_config->ir_config.pulse_threshold,
                symbol->duration0);
        bool is_dur0_double_pulse = pulse_in_threshold(
                rx_config->ir_config.pulse_width * 2,
                rx_config->ir_config.pulse_threshold * 2,
                symbol->duration0);
        bool is_dur1_single_pulse = pulse_in_threshold(
                rx_config->ir_config.pulse_width,
                rx_config->ir_config.pulse_threshold,
                symbol->duration1);
        bool is_dur1_double_pulse = pulse_in_threshold(
                rx_config->ir_config.pulse_width * 2,
                rx_config->ir_config.pulse_threshold * 2,
                symbol->duration1);

        // In Manchester encoding, each pulse can only be a single or double pulse
        if (is_dur0_single_pulse == false && is_dur0_double_pulse == false) {
            printf("Erroneous duration0 pulse [%d] pulse width=%d\n", i, symbol->duration0);
            free(single_pulses);
            return RMT_IR_ERROR;
        }
        if (is_dur1_single_pulse == false && is_dur1_double_pulse == false && symbol->duration1 != 0) {
            printf("Erroneous duration1 pulse [%d] pulse width=%d\n", i, symbol->duration1);
            free(single_pulses);
            return RMT_IR_ERROR;
        }

        if (is_dur0_single_pulse) {
            single_pulses[single_pulse_index].pulse_level = symbol->level0;
            single_pulses[single_pulse_index].pulse_duration_usec = symbol->duration0;
            single_pulse_index++;
        } else if (is_dur0_double_pulse) {
            single_pulses[single_pulse_index].pulse_level = symbol->level0;
            single_pulses[single_pulse_index].pulse_duration_usec = symbol->duration0/2;
            single_pulses[single_pulse_index+1].pulse_level = symbol->level0;
            single_pulses[single_pulse_index+1].pulse_duration_usec = symbol->duration0/2;
            single_pulse_index += 2;
        }

        // The final duration1 may be 0, which is ok, skip it
        if (symbol->duration1 == 0) {
            continue;
        }

        if (is_dur1_single_pulse) {
            single_pulses[single_pulse_index].pulse_level = symbol->level1;
            single_pulses[single_pulse_index].pulse_duration_usec = symbol->duration1;
            single_pulse_index++;
        } else if (is_dur1_double_pulse) {
            single_pulses[single_pulse_index].pulse_level = symbol->level1;
            single_pulses[single_pulse_index].pulse_duration_usec = symbol->duration1/2;
            single_pulses[single_pulse_index+1].pulse_level = symbol->level1;
            single_pulses[single_pulse_index+1].pulse_duration_usec = symbol->duration1/2;
            single_pulse_index += 2;
        }
    }

    printf("Numer of single pulses: %d\n", single_pulse_index);

    // TODO need to verify rx_config->ir_config->start_pulse_data

    // For manchester encoding the number of pulses must be even.
    // If its not, then pad the last pulse with a zero.
    // Calculate the number of data_bytes based on the number of
    // pulses, not including the start pulses. The number of data
    // bytes should be half the number of data pulses.
    int num_data_bits;
    if (((single_pulse_index - rx_config->ir_config.start_pulse_data.num_pulses) / 2) % 2 == 0) {
        // Even number of pulses
        num_data_bits =
                (single_pulse_index -
                 rx_config->ir_config.start_pulse_data.num_pulses) / 2;
    } else {
        // Odd number of pulses, pad 1
        num_data_bits =
                ((single_pulse_index -
                  rx_config->ir_config.start_pulse_data.num_pulses) / 2) + 1;
    }
    int num_data_list_entries =
            ((num_data_bits % 8) == 0 ?
                    (num_data_bits / 8) : ((num_data_bits / 8) + 1));

    // Only malloc new memory if the size changed from the previous run
    if (rx_config->data_list == NULL) {
        rx_config->data_list = malloc(sizeof(uint8_t) * num_data_list_entries);
    } else if (num_data_list_entries != rx_config->num_data_list_entries) {
        // Free the previous allocation
        free(rx_config->data_list);
        rx_config->data_list = malloc(sizeof(uint8_t) * num_data_list_entries);
    }
    memset(rx_config->data_list, 0, sizeof(uint8_t) * num_data_list_entries);

    rx_config->num_data_list_entries = num_data_list_entries;
    uint8_t bit_index  = 7;
    uint8_t byte_index = 0;

    printf("Numer of data list entries: %d\n", num_data_list_entries);

    // Now iterate the single pulses and determine the bits
    int i = rx_config->ir_config.start_pulse_data.num_pulses; // must be even
    for ( ; i < single_pulse_index; i+=2) {
        pulse_info *pulse0 = &single_pulses[i];
        pulse_info *pulse1 = &single_pulses[i+1];
        /*
        printf("\t [%2d] pulse0: level = %d duration = %5ld\n",
               i, pulse0->pulse_level, pulse0->pulse_duration_usec);
        printf("\t [%2d] pulse1: level = %d duration = %5ld, ",
               i+1, pulse1->pulse_level, pulse1->pulse_duration_usec);
        */

        if (pulse0->pulse_level == 0 && pulse1->pulse_level == 1) {
            if (rx_config->ir_config.ir_enc_type == DIFF_MANCHESTER_ENCODING) {
                // With differential manchester encoding, a transition is a 0
                //printf("Binary 0\n");
            } else {
                // With normal manchester endocing a low to high pulse transition is a logical 1
                //printf("Binary 1\n");
                rx_config->data_list[byte_index] |= (1 << bit_index);
            }
        } else if (pulse0->pulse_level == 1 && pulse1->pulse_level == 0) {
            if (rx_config->ir_config.ir_enc_type == DIFF_MANCHESTER_ENCODING) {
                // With differential manchester encoding, a transition is a locical 0
                //printf("Binary 0\n");
            } else {
                // With normal manchester endocing a high to low pulse transition is a logical 0
                //printf("Binary 0\n");
            }
        } else {
            if (rx_config->ir_config.ir_enc_type == DIFF_MANCHESTER_ENCODING) {
                // With differential manchester encoding, no transition is a locical 1
                //printf("Binary 1\n");
                rx_config->data_list[byte_index] |= (1 << bit_index);
            } else {
                printf("Undetermined Manchester encoding %d, %d\n", pulse0->pulse_level, pulse1->pulse_level);
                // TODO what to do here??
            }
        }

        if (bit_index == 0) {
            bit_index = 7;
            ++byte_index;
        }
        else {
            bit_index--;
        }
    }

    free(single_pulses);

    return RMT_IR_OK;
}


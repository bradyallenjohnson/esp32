/*
 * RMT IR header file for remote control receivers and transmitters
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

#ifndef RMT_IR_H_ 
#define RMT_IR_H_

#include <stdlib.h>  // bool, uint8_t, etc
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_types.h"

const static int RMT_IR_OK = 0;
const static int RMT_IR_ERROR = 1;

/*
 * Refer to the following for an explanation of the different encoding types:
 * - https://www.phidgets.com/docs/IR_Remote_Control_Guide
 * - https://techdocs.altium.com/display/FPGA/Infrared+Communication+Concepts
 * - https://en.wikipedia.org/wiki/Manchester_code
 * - https://en.wikipedia.org/wiki/Differential_Manchester_encoding
 */

typedef enum {
    PULSE_DISTANCE_ENCODING  = 1,
    PULSE_LENGTH_ENCODING    = 2,
    MANCHESTER_ENCODING      = 3,
    DIFF_MANCHESTER_ENCODING = 4    // Differential Manchester encoding
} IR_encoding_type;

typedef enum {
    PULSE_LEVEL_LOW = 0,
    PULSE_LEVEL_HIGH = 1,
    PULSE_LEVEL_EITHER = 2
} IR_pulse_level;

/* TODO should we use this instead of the already defined rmt_symbol_word_t ??? */
typedef struct pulse_info_ {
    IR_pulse_level pulse_level;
	uint32_t pulse_duration_usec;
	struct pulse_info_ *next;
} pulse_info;

/* TODO rmt_rx_done_event_data_t already kind-of defines this */
typedef struct pulse_data_ {
	uint16_t num_pulses;
	pulse_info *pulse_list_head;  // pupulate this list with add_pulse_data_info()
} pulse_data;

/*
 * Input configuration describing an Infrared transmission
 */
typedef struct ir_config_data_ {
    uint8_t gpio_pin;
    IR_encoding_type ir_enc_type;
    pulse_data start_pulse_data;
    pulse_data stop_pulse_data;
    uint32_t pulse_width;           // used to configure RMT min pulse width
    uint8_t pulse_threshold;        // plus/minus threshhold: if pulse=850, 820 < 850 < 880
    uint32_t signal_range_min_ns;   // used to configure RMT min pulse width, may need some tweeking
    uint32_t signal_range_max_ns;   // used to configure RMT max pulse width, which marks completion
    uint16_t num_data_bits;         // set num start pulses in start_pulse_data
} ir_config_data;
	
typedef struct rx_ir_config_ {
    ir_config_data ir_config;

    uint16_t num_data_list_entries;
    uint8_t *data_list; /* Output parameter */

    rmt_rx_channel_config_t   rx_chan_config;
    rmt_receive_config_t      rx_config;
    rmt_channel_handle_t      rx_chan;
    QueueHandle_t             rx_queue;
    rmt_rx_event_callbacks_t  rx_cbs;
    rmt_symbol_word_t         rx_raw_symbols[128]; // 128 symbols should be sufficient for a standard remote
    BaseType_t                high_task_wakeup;

} rx_ir_config;

typedef struct tx_ir_config_ {
    ir_config_data ir_config;

    /* TODO add more parameters as needed */
} tx_ir_config;

//-------------------------------------------------------------------

/*
 * RMT Reciever API functions
 */

/*
 * Initialize RMT IR infra for a receiver
 * Call this before any other receiver APIs
 */
int init_receiver(rx_ir_config *rx_config);

/*
 * Start receiving data
 * If called with block_until_done=false, use is_receive_done() to check when receiving is complete
 */
int start_receiving(rx_ir_config *rx_config, bool block_until_done);
bool is_receiver_done(rx_ir_config *rx_config);

/* This call is blocking until the receiver completes, unless is_receiver_done()
 * returns true, in which case, the call will not block and the recieved data
 * will be processed. The received data is available in rx_config->data_list
 */
int wait_for_receiver(rx_ir_config *rx_config);

// Implemented in rmt_ir_rx_decoders.c
int decode_rx_data_manchester(
        rx_ir_config *rx_config,
        rmt_rx_done_event_data_t *rx_done_data);
int decode_rx_data_pulse_distance(
        rx_ir_config *rx_config,
        rmt_rx_done_event_data_t *rx_done_data);

// Implemented in rmt_ir_rx_remotes.c
int setup_remote_musical_fidelity(rx_ir_config *rx_config, int gpio_pin);
int setup_remote_lg_tv(rx_ir_config *rx_config, int gpio_pin);

//-------------------------------------------------------------------

/*
 * RMT Transmitter API functions
 */

/*
 * Initialize RMT IR infra for a transmitter
 * Call this before any other transmitter APIs
 */
int init_transmitter(tx_ir_config *tx_config);

/*
 * Start transmitting data
 * If called with block_until_done=false, use is_transmit_done() to check when
 * transmitting is complete, and use wait_for_transmitter() upon completion.
 */
int start_transmitting(tx_ir_config *tx_config, bool block_until_done);
bool is_transmitter_done(tx_ir_config *tx_config);
int wait_for_transmitter(tx_ir_config *tx_config);


//-------------------------------------------------------------------

/*
 * API functions common to both the RMT Receiver and Transmitter
 */

/*
 * Only call this after init_receiver() or init_transmitter()
 * Append new ir_config->pulse_data->pulse_list_head entries.
 * Call with either ir_config->start_pulse_data or with ir_config->stop_pulse_data
 * This will dynamically add list entries with malloc() use free_pulse_data_info()
 * to free all the dynamically allocated memory.
 */
void add_pulse_data_info(pulse_data *pd, IR_pulse_level level, uint32_t duration);
void free_pulse_data_info(pulse_data *pd);

#endif

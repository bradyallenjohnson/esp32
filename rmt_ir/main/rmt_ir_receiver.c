
/*
 * RMT IR receiver implementation
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
#include "driver/gpio.h"
#include "rmt_ir.h"

/*
 * Util functions
 */

static bool rmt_rx_done_callback(
        rmt_channel_handle_t channel,
        const rmt_rx_done_event_data_t *edata,
        void *user_data)
{
    rx_ir_config *rx_config = (rx_ir_config *) user_data;
    QueueHandle_t receive_queue = (QueueHandle_t) rx_config->rx_queue;
    rx_config->high_task_wakeup = pdFALSE;

    // send the received RMT symbols to the parser task
    xQueueSendFromISR(receive_queue, edata, &rx_config->high_task_wakeup);

    // return whether any task is woken up
    return rx_config->high_task_wakeup == pdTRUE;
}

/*
 * API functions
 */

void add_pulse_data_info(pulse_data *pd, IR_pulse_level level, uint32_t duration)
{
    pulse_info *pi = malloc(sizeof(pulse_info));
    pi->pulse_level = level;
    pi->pulse_duration_usec = duration;
    pi->next = NULL;

    pd->num_pulses++;
    if (pd->pulse_list_head == NULL) {
        pd->pulse_list_head = pi;
    } else {
        pulse_info *last_pi = pd->pulse_list_head;
        while (last_pi->next != NULL) {
            last_pi = last_pi->next;
        }
        last_pi->next = pi;
    }
}


void free_pulse_data_info(pulse_data *pd)
{
    if (pd == NULL) {
        return;
    }

    pd->num_pulses = 0;
    pulse_info *pi = pd->pulse_list_head;
    pulse_info *pi_next = NULL;
    while(pi != NULL) {
        pi_next = pi->next;
        free(pi);
        pi = pi_next;
    }
}


int init_receiver(rx_ir_config *rx_config)
{
    rx_config->rx_chan_config.clk_src = RMT_CLK_SRC_REF_TICK;  // 1MHz, using RMT_CLK_SRC_DEFAULT affects dht_rx_config.signal_range_min_ns
    rx_config->rx_chan_config.resolution_hz = 1 * 1000 * 1000; // 1 MHz tick resolution, i.e., 1 tick = 1 µs
    rx_config->rx_chan_config.mem_block_symbols = 128;         // memory block size, 64 * 4 = 256 Bytes
    rx_config->rx_chan_config.gpio_num = rx_config->ir_config.gpio_pin;  // GPIO number
    rx_config->rx_chan_config.flags.invert_in = false;         // do not invert input signal
    rx_config->rx_chan_config.flags.with_dma = false;          // do not need DMA backend
    rx_config->rx_chan_config.flags.io_loop_back = true;       // feed the GPIO signal output to the input path
    rx_config->high_task_wakeup = pdFALSE;                     // set in rmt_rx_done_callback() when receiver completes
    rx_config->data_list = NULL;
    rx_config->num_data_list_entries = 0;

    rx_config->rx_chan = NULL;
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_config->rx_chan_config, &rx_config->rx_chan));
    ESP_ERROR_CHECK(rmt_enable(rx_config->rx_chan));

    // Done callback setup
    rx_config->rx_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    rx_config->rx_cbs.on_recv_done = rmt_rx_done_callback;
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_config->rx_chan, &rx_config->rx_cbs, rx_config));

    // The IR receiver is not happy if we dont set these 2
    //ESP_ERROR_CHECK(gpio_set_level(rx_config->ir_config.gpio_pin, 1));
    ESP_ERROR_CHECK(gpio_set_level(rx_config->ir_config.gpio_pin, 0));
    ESP_ERROR_CHECK(gpio_set_direction(rx_config->ir_config.gpio_pin, GPIO_MODE_INPUT));

    rx_config->ir_config.start_pulse_data.num_pulses = 0;
    rx_config->ir_config.start_pulse_data.pulse_list_head = NULL;
    rx_config->ir_config.stop_pulse_data.num_pulses = 0;
    rx_config->ir_config.stop_pulse_data.pulse_list_head = NULL;

    // TODO ADD more returns on error above
    return RMT_IR_OK;
}


int start_receiving(rx_ir_config *rx_config, bool block_until_done)
{
    // These 2 config values are supposed to be set each time
	// These values depend on dht_rx_chan_config.clk_src = RMT_CLK_SRC_REF_TICK, which is 1MHz
    rx_config->rx_config.signal_range_min_ns = rx_config->ir_config.signal_range_min_ns;
    rx_config->rx_config.signal_range_max_ns = rx_config->ir_config.signal_range_max_ns;

    // The receiver is stopped by the driver when it finishes working, that is, when
    // it receive a signal whose duration is bigger than
    // rmt_receive_config_t::signal_range_max_ns
    ESP_ERROR_CHECK(rmt_receive(
            rx_config->rx_chan,
            rx_config->rx_raw_symbols,
            sizeof(rx_config->rx_raw_symbols),
            &rx_config->rx_config));

    int rc = RMT_IR_OK;
    if (block_until_done) {
        rc = wait_for_receiver(rx_config);
    }

    return rc;
}


int wait_for_receiver(rx_ir_config *rx_config)
{
    rmt_rx_done_event_data_t rx_done_data;
    memset(&rx_done_data, 0, sizeof(rx_done_data));
    memset(rx_config->rx_raw_symbols, 0, sizeof(rx_config->rx_raw_symbols));

    // This is a blocking call
    xQueueReceive(rx_config->rx_queue, &rx_done_data, portMAX_DELAY);
    //xQueueReceive(rx_config->rx_queue, &rx_done_data, pdMS_TO_TICKS(5000) /*portMAX_DELAY*/);

    // TODO maybe we could have several remote types created, and auto-detect
    //      the decoder based on matching the start_pulses

    int rc = RMT_IR_OK;
    printf("Data received: %d symbols\n", rx_done_data.num_symbols);
    if (rx_config->ir_config.ir_enc_type == MANCHESTER_ENCODING ||
        rx_config->ir_config.ir_enc_type == DIFF_MANCHESTER_ENCODING) {
        rc = decode_rx_data_manchester(rx_config, &rx_done_data);
    } else if (rx_config->ir_config.ir_enc_type == PULSE_DISTANCE_ENCODING) {
        rc = decode_rx_data_pulse_distance(rx_config, &rx_done_data);
    } else {
        printf("ERROR RX decoder [%d] is not implemented yet\n",
               rx_config->ir_config.ir_enc_type);
        rc = RMT_IR_ERROR;
    }

    return rc;
}


bool is_receiver_done(rx_ir_config *rx_config)
{
    // This value is only ever set in rmt_rx_done_callback()
    return rx_config->high_task_wakeup == pdTRUE;
}


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

// Check if the received duration is in this range:
// (pulse_width - threshold) <= duration <= (pulse_width + duration)
// For example: pulse_width = 850, threshold = 30:
//     duration = 832: 820 <= 832 <= 880, return true
//     duration = 818, return false
//     duration = 890, return false
bool pulse_in_threshold(uint32_t pulse_width, uint8_t threshold, uint32_t duration)
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

    int rc = RMT_IR_OK;
    printf("Data received: %d symbols\n", rx_done_data.num_symbols);
    if (rx_config->ir_config.ir_enc_type == MANCHESTER_ENCODING ||
        rx_config->ir_config.ir_enc_type == DIFF_MANCHESTER_ENCODING) {
        rc = decode_rx_data_manchester(rx_config, &rx_done_data);
    } else {
        // TODO implement the other decoders
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

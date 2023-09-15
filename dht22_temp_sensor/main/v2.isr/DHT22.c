/*------------------------------------------------------------------------------

	DHT22 temperature & humidity sensor AM2302 (DHT22) driver for ESP32

	Jun 2017:	Ricardo Timmermann, new for DHT22  	

	Code Based on Adafruit Industries and Sam Johnston and Coffe & Beer. Please help
	to improve this code. 
	
	This example code is in the Public Domain (or CC0 licensed, at your option.)

	Unless required by applicable law or agreed to in writing, this
	software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
	CONDITIONS OF ANY KIND, either express or implied.

	PLEASE KEEP THIS CODE IN LESS THAN 0XFF LINES. EACH LINE MAY CONTAIN ONE BUG !!!

---------------------------------------------------------------------------------*/

#define LOG_LOCAL_LEVEL   ESP_LOG_VERBOSE

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_types.h"

#include "DHT22.h"

// == global defines =============================================

static const char* TAG = "DHT";

typedef struct isr_data_ {
	uint64_t micros;
	uint8_t  level;
} isr_data;

int ISR_DATA_INDEX = 0;
#define ISR_DATA_INDEX_MAX 64
isr_data ISR_DATA_LIST[ISR_DATA_INDEX_MAX];

typedef struct dht_context_ {
    // RMT transmitter config
    rmt_channel_handle_t       dht_tx_chan;
    rmt_tx_channel_config_t    dht_tx_chan_config;
    rmt_encoder_handle_t       dht_copy_encoder;
    rmt_copy_encoder_config_t  dht_encoder_config;
    rmt_transmit_config_t      dht_tx_config;
    rmt_symbol_word_t          dht_tx_data_symbol;

    // RMT receiver config
    rmt_channel_handle_t       dht_rx_chan;
    rmt_rx_channel_config_t    dht_rx_chan_config;
    rmt_receive_config_t       dht_rx_config;
    QueueHandle_t              dht_rx_queue;
    rmt_rx_event_callbacks_t   dht_rx_cbs;
    rmt_symbol_word_t          dht_rx_raw_symbols[64]; // 64 symbols should be sufficient for a standard DHT22 frame

    int dht_gpio_pin;
    float humidity;
    float temperature;

} dht_context;

// == get temp & hum =============================================

float get_humidity(dht_context *dhtc) { return dhtc->humidity; }
float get_temperature(dht_context *dhtc) { return dhtc->temperature; }

// == error handler ===============================================

void error_handler(int response)
{
    switch(response) {

        case DHT_TIMEOUT_ERROR :
            ESP_LOGE( TAG, "Sensor Timeout\n" );
            break;

        case DHT_CHECKSUM_ERROR:
            ESP_LOGE( TAG, "CheckSum error\n" );
            break;

        case DHT_OK:
            break;

        default :
            ESP_LOGE( TAG, "Unknown error\n" );
    }
}

static bool rmt_rx_done_callback(
        rmt_channel_handle_t channel,
        const rmt_rx_done_event_data_t *edata,
        void *user_data)
{
    //printf("rmt_rx_done_callback\n");

    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t receive_queue = (QueueHandle_t)user_data;
    // send the received RMT symbols to the parser task
    xQueueSendFromISR(receive_queue, edata, &high_task_wakeup);
    // return whether any task is woken up
    return high_task_wakeup == pdTRUE;
}

static void gpio_isr_edge_handler(void *arg)
{
    if (ISR_DATA_INDEX >= ISR_DATA_INDEX_MAX) {
        //ESP_LOGE( TAG, "gpio_isr_edge_handler overflow\n" );
        return;
    }

    isr_data *isrd = &ISR_DATA_LIST[ISR_DATA_INDEX++];
    isrd->micros = esp_timer_get_time();
    isrd->level = gpio_get_level((uint32_t) arg);
}


#define MAX_DHT_DATA 5    // to complete 40 = 5*8 Bits

int process_dht_data(dht_context *dhtc, rmt_rx_done_event_data_t *rx_data)
{
    uint8_t bit_index  = 7;
    uint8_t byte_index = 0;
    uint8_t dhtData[MAX_DHT_DATA];

    memset(dhtData, 0, sizeof(uint8_t)*MAX_DHT_DATA);
    /* The first 4 items are the DHT setup signals, and the last one completes it:
       received_symbols[0]  level0=1 duration0=19, level1=0 duration1=82
       received_symbols[1]  level0=1 duration0=81, level1=0 duration1=52
       received_symbols[42] level0=1 duration0=0,  level100 duration1=55 */
    printf("process_dht_data num_symbols [%d]\n", rx_data->num_symbols); fflush(stdout);
    for (int i = 2; i < rx_data->num_symbols - 1; i++) {
        /* level0/duration0 is the bit indicator, and level1/duration1 is the bit separator
           - 70 usec high-level is "1"
           - 26-28 usec high-level is "0" */
        rmt_symbol_word_t *symbol = &rx_data->received_symbols[i];
        if (symbol->level1 != 0 || symbol->duration1 < 44 || symbol->duration1 > 56) {
            printf("Error in bit separator data[%d] level1=%d duration1=%d\n",
                    i, symbol->level1, symbol->duration1);
            bit_index++;
            continue;
            //return;
        }

        if (symbol->level0 == 1 && symbol->duration0 > 65 && symbol->duration0 < 75) {
            dhtData[byte_index] |= (1 << bit_index);
        }

       if (symbol->level0 == 0 || symbol->duration0 < 20 || symbol->duration0 > 34) {
           printf("Error in data[%d] level0=%d duration0=%d\n",
                   i, symbol->level1, symbol->duration1);
           bit_index++;
           continue;
           //return;
        }

        if (bit_index == 0) { bit_index = 7; ++byte_index; }
        else { bit_index--; }
    }

    // Get humidity from Data[0] and Data[1]
    dhtc->humidity = dhtData[0];
    dhtc->humidity *= 0x100;       // >> 8
    dhtc->humidity += dhtData[1];
    dhtc->humidity /= 10;          // get the decimal

    // Get temp from Data[2] and Data[3]
    dhtc->temperature = dhtData[2] & 0x7F;
    dhtc->temperature *= 0x100;           // >> 8
    dhtc->temperature += dhtData[3];
    dhtc->temperature /= 10;

    if (dhtData[2] & 0x80) {
        dhtc->temperature *= -1;
    }

    // Verify the checksum is ok: Checksum is the sum of Data 8 bits masked out 0xFF
    if (dhtData[4] == ((dhtData[0] + dhtData[1] + dhtData[2] + dhtData[3]) & 0xFF)) {
        return DHT_OK;
    }
    else {
        return DHT_CHECKSUM_ERROR;
    }
}


void *setup_DHT(int dht_gpio_pin)
{
    dht_context *dhtc = malloc(sizeof(dht_context));

    gpio_set_direction(dht_gpio_pin, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_level(dht_gpio_pin, 1);

    dhtc->dht_gpio_pin = dht_gpio_pin;
    dhtc->humidity = 0.0;
    dhtc->temperature = 0.0;

    /* Note:
     * Due to a software limitation in the GPIO driver, when both TX and RX
     * channels are bound to the same GPIO, ensure the RX Channel is initialized
     * before the TX Channel. If the TX Channel was set up first, then during
     * the RX Channel setup, the previous RMT TX Channel signal will be overridden
     * by the GPIO control signal.
     */

    //
    // Setup the RMT receiver
    //

#if 0
    dhtc->dht_rx_chan_config.clk_src = RMT_CLK_SRC_DEFAULT;   // select source clock
    dhtc->dht_rx_chan_config.resolution_hz = 1 * 1000 * 1000; // 1 MHz tick resolution, i.e., 1 tick = 1 µs
    dhtc->dht_rx_chan_config.mem_block_symbols = 64;          // memory block size, 64 * 4 = 256 Bytes
    dhtc->dht_rx_chan_config.gpio_num = dht_gpio_pin;         // GPIO number
    dhtc->dht_rx_chan_config.flags.invert_in = false;         // do not invert input signal
    dhtc->dht_rx_chan_config.flags.with_dma = false;          // do not need DMA backend
    //dhtc->dht_rx_chan_config.flags.io_loop_back = true;

    dhtc->dht_rx_chan = NULL;
    ESP_ERROR_CHECK(rmt_new_rx_channel(&dhtc->dht_rx_chan_config, &dhtc->dht_rx_chan));
    ESP_ERROR_CHECK(rmt_enable(dhtc->dht_rx_chan));

	// Done callback setup
    dhtc->dht_rx_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    dhtc->dht_rx_cbs.on_recv_done = rmt_rx_done_callback;
#endif

    //
    // Setup the RMT transmitter
    //

    dhtc->dht_tx_chan_config.clk_src = RMT_CLK_SRC_DEFAULT;   // select source clock
    dhtc->dht_tx_chan_config.gpio_num = dht_gpio_pin;         // GPIO number
    dhtc->dht_tx_chan_config.mem_block_symbols = 64;          // memory block size, 64 * 4 = 256 Bytes
    dhtc->dht_tx_chan_config.resolution_hz = 1 * 1000 * 1000; // 1 MHz tick resolution, i.e., 1 tick = 1 µs
    dhtc->dht_tx_chan_config.trans_queue_depth = 4;           // set the number of transactions that can pend in the background
    dhtc->dht_tx_chan_config.flags.invert_out = false;        // do not invert output signal
    dhtc->dht_tx_chan_config.flags.with_dma = false;          // do not need DMA backend

    dhtc->dht_tx_chan = NULL;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&dhtc->dht_tx_chan_config, &dhtc->dht_tx_chan));

    // The copy_encoder copies the RMT symbols from user space into the driver layer
    memset(&dhtc->dht_encoder_config, 0, sizeof(dhtc->dht_encoder_config)); // nothing to config
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&dhtc->dht_encoder_config, &dhtc->dht_copy_encoder));
    ESP_ERROR_CHECK(rmt_enable(dhtc->dht_tx_chan));

    // DHT22 protocol:
    // - set low for at least 1 msec
    // - set high for 20-40 usec
    // - leave high, set with rmt_transmit_config_t.flags.eot_level = 1
#if 0
    dhtc->dht_tx_data_symbol.level0    = 0;     // set low
    dhtc->dht_tx_data_symbol.duration0 = 2000;  // at least 1 msec
    dhtc->dht_tx_data_symbol.level1    = 1;     // set high
    dhtc->dht_tx_data_symbol.duration1 = 30;    // 20-40 usec
#else
    dhtc->dht_tx_data_symbol.level0    = 1;     // set high
    dhtc->dht_tx_data_symbol.duration0 = 30;    // 20-40 usec
    dhtc->dht_tx_data_symbol.level1    = 0;     // set low
    dhtc->dht_tx_data_symbol.duration1 = 2000;  // at least 1 msec
#endif
    dhtc->dht_tx_config.loop_count = 0;         // no transfer loop
    dhtc->dht_tx_config.flags.eot_level = 1;    // Set the output level for the "End Of Transmission"

    ESP_ERROR_CHECK(gpio_set_intr_type(dht_gpio_pin, GPIO_INTR_ANYEDGE));
    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3));
    ESP_ERROR_CHECK(gpio_isr_handler_add(dht_gpio_pin, gpio_isr_edge_handler, (void *) dht_gpio_pin));

    return (void *) dhtc;
}

/*----------------------------------------------------------------------------
;
;	read DHT22 sensor

copy/paste from AM2302/DHT22 Docu:

DATA: Hum = 16 bits, Temp = 16 Bits, check-sum = 8 Bits

Example: MCU has received 40 bits data from AM2302 as
0000 0010 1000 1100 0000 0001 0101 1111 1110 1110
16 bits RH data + 16 bits T data + check sum

1) we convert 16 bits RH data from binary system to decimal system, 0000 0010 1000 1100 → 652
Binary system Decimal system: RH=652/10=65.2%RH

2) we convert 16 bits T data from binary system to decimal system, 0000 0001 0101 1111 → 351
Binary system Decimal system: T=351/10=35.1°C

When highest bit of temperature is 1, it means the temperature is below 0 degree Celsius. 
Example: 1000 0000 0110 0101, T= minus 10.1°C: 16 bits T data

3) Check Sum=0000 0010+1000 1100+0000 0001+0101 1111=1110 1110 Check-sum=the last 8 bits of Sum=11101110

Signal & Timings:

The interval of whole process must be beyond 2 seconds.

To request data from DHT:
1) Sent low pulse for > 1~10 ms (MILI SEC)
2) Sent high pulse for > 20~40 us (Micros).
3) When DHT detects the start signal, it will pull low the bus 80us as response signal, 
   then the DHT pulls up 80us for preparation to send data.
4) When DHT is sending data to MCU, every bit's transmission begin with low-voltage-level that last 50us, 
   the following high-voltage-level signal's length decide the bit is "1" or "0".
	0: 26~28 us
	1: 70 us

;----------------------------------------------------------------------------*/

int read_DHT(void *dht_handle)
{
    dht_context *dhtc = (dht_context *) dht_handle;

    rmt_rx_done_event_data_t rx_data;
    memset(&rx_data, 0, sizeof(rx_data));
    memset(dhtc->dht_rx_raw_symbols, 0, sizeof(dhtc->dht_rx_raw_symbols));

    // Receiver setup
#if 0
	ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(dhtc->dht_rx_chan, &dhtc->dht_rx_cbs, dhtc->dht_rx_queue));
    //dhtc->dht_rx_config.signal_range_min_ns = 2000;     // the shortest duration for DHT22 signal is 26 µs
    //dhtc->dht_rx_config.signal_range_max_ns = 10000000;  // the longest duration for DHT22 signal is 5000 µs (5 ms), 10 ms max so the receive does not stop early
    dhtc->dht_rx_config.signal_range_min_ns = 2000;     // the shortest duration for DHT22 signal is 26 µs
    dhtc->dht_rx_config.signal_range_max_ns = 5000000;  // the longest duration for DHT22 signal is 5000 µs (5 ms), 10 ms max so the receive does not stop early
#endif

    dhtc->dht_tx_config.loop_count = 0;       // no transfer loop
    dhtc->dht_tx_config.flags.eot_level = 1;  // Set the output level for the "End Of Transmission"

    // Send start signal to DHT sensor
    ESP_ERROR_CHECK(rmt_transmit(
            dhtc->dht_tx_chan,
            dhtc->dht_copy_encoder,
            &dhtc->dht_tx_data_symbol,
            sizeof(rmt_symbol_word_t),
            &dhtc->dht_tx_config));
    //ESP_ERROR_CHECK(rmt_tx_wait_all_done(dhtc->dht_tx_chan, portMAX_DELAY));

    // The receiver is stopped by the driver when it finishes working, that is, when
    // it receive a signal whose duration is bigger than rmt_receive_config_t::signal_range_max_ns
#if 0
    ESP_ERROR_CHECK(rmt_receive(
    		dhtc->dht_rx_chan,
			dhtc->dht_rx_raw_symbols,
			sizeof(dhtc->dht_rx_raw_symbols),
			&dhtc->dht_rx_config));
    printf("Waiting for received data\n");

    xQueueReceive(dhtc->dht_rx_queue, &rx_data, pdMS_TO_TICKS(5000) /*portMAX_DELAY*/);

    if (rx_data.num_symbols != 43) {
    	printf("Erroneous data captured, num_symbols=%d\n", rx_data.num_symbols);
    	return DHT_TIMEOUT_ERROR;
    }

    return process_dht_data(dhtc, &rx_data);
#else
    // Delay for 5 ms
    uint64_t start_micros = esp_timer_get_time();
    uint64_t now_micros = esp_timer_get_time();
    while ((now_micros - start_micros) < 5000) {
        now_micros = esp_timer_get_time();
    }

    printf("ISR Data index: %d\n", ISR_DATA_INDEX);
    for (int i = 0; i < ISR_DATA_INDEX; i++) {
    	isr_data isrd = ISR_DATA_LIST[i];
    	int timeDiff = ((i == 0) ? 0 : (isrd.micros - ISR_DATA_LIST[i-1].micros));
    	printf("\t [%lld] timeDiff %d, level %d\n", isrd.micros, timeDiff, isrd.level);
    }
    ISR_DATA_INDEX = 0;

    return DHT_OK;
#endif
}

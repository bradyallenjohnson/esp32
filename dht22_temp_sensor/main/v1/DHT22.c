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
#define LEDC_DUTY_RES     LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY         (4095) // Set duty to 50%. ((2 ** 13) - 1) * 50% = 4095
#define LEDC_FREQUENCY    (5000) // Frequency in Hertz. Set frequency at 5 kHz

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
#include "driver/ledc.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_types.h"

#include "DHT22.h"

// == global defines =============================================

static const char* TAG = "DHT";

float humidity = 0.;
float temperature = 0.;

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

} dht_context;

// == get temp & hum =============================================

float getHumidity() { return humidity; }
float getTemperature() { return temperature; }

// == error handler ===============================================

void errorHandler(int response)
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

void *setupDHT(int dht_gpio_pin)
{
    dht_context *dhtc = malloc(sizeof(dht_context));

    dhtc->dht_gpio_pin = dht_gpio_pin;

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

    dhtc->dht_rx_chan_config.clk_src = RMT_CLK_SRC_DEFAULT;   // select source clock
    dhtc->dht_rx_chan_config.resolution_hz = 1 * 1000 * 1000; // 1 MHz tick resolution, i.e., 1 tick = 1 µs
    dhtc->dht_rx_chan_config.mem_block_symbols = 64;          // memory block size, 64 * 4 = 256 Bytes
    dhtc->dht_rx_chan_config.gpio_num = dht_gpio_pin;         // GPIO number
    dhtc->dht_rx_chan_config.flags.invert_in = false;         // do not invert input signal
    dhtc->dht_rx_chan_config.flags.with_dma = false;          // do not need DMA backend
    //dhtc->dht_rx_chan_config.flags.io_loop_back = true;

    dhtc->dht_rx_chan = NULL;
    ESP_ERROR_CHECK(rmt_new_rx_channel(&dhtc->dht_rx_chan_config, &dhtc->dht_rx_chan));

	// Done callback setup
	dhtc->dht_rx_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
	dhtc->dht_rx_cbs.on_recv_done = rmt_rx_done_callback;

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

    // DHT22 protocol:
    // - set low for at least 1 msec
    // - set high for 20-40 usec
    // - leave high, set with rmt_transmit_config_t.flags.eot_level = 1
    dhtc->dht_tx_data_symbol.level0    = 0;     // set low
    //dhtc->dht_tx_data_symbol.duration0 = 1010;  // at least 1 msec
    dhtc->dht_tx_data_symbol.duration0 = 2000;  // at least 1 msec
    dhtc->dht_tx_data_symbol.level1    = 1;     // set high
    dhtc->dht_tx_data_symbol.duration1 = 30;    // 20-40 usec
    dhtc->dht_tx_config.loop_count = 0;       // no transfer loop
    dhtc->dht_tx_config.flags.eot_level = 1;  // Set the output level for the "End Of Transmission"

    ESP_ERROR_CHECK(rmt_enable(dhtc->dht_tx_chan));
    ESP_ERROR_CHECK(rmt_enable(dhtc->dht_rx_chan));

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

int readDHT(void *dht_handle)
{
    dht_context *dhtc = (dht_context *) dht_handle;

    rmt_rx_done_event_data_t rx_data;
    memset(&rx_data, 0, sizeof(rx_data));
    memset(dhtc->dht_rx_raw_symbols, 0, sizeof(dhtc->dht_rx_raw_symbols));

    // Receiver setup
	ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(dhtc->dht_rx_chan, &dhtc->dht_rx_cbs, dhtc->dht_rx_queue));
    dhtc->dht_rx_config.signal_range_min_ns = 1000;    // the shortest duration for DHT22 signal is 26 µs
    dhtc->dht_rx_config.signal_range_max_ns = 50000;  // the longest duration for DHT22 signal is 5000 µs (5 ms), 10 ms max so the receive does not stop early

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
    //ESP_ERROR_CHECK(rmt_disable(dhtc->dht_tx_chan));

    // The receiver is stopped by the driver when it finishes working,
    // i.e., receive a signal whose duration is bigger than
    // rmt_receive_config_t::signal_range_max_ns
    ESP_ERROR_CHECK(rmt_receive(
    		dhtc->dht_rx_chan,
			dhtc->dht_rx_raw_symbols,
			sizeof(dhtc->dht_rx_raw_symbols),
			&dhtc->dht_rx_config));

    // wait for the RX-done signal
    printf("Waiting for received data\n");
    fflush(stdout);
    //xQueueReceive(dhtc->dht_rx_queue, &rx_data, portMAX_DELAY);
    xQueueReceive(dhtc->dht_rx_queue, &rx_data, pdMS_TO_TICKS(5000));

    // parse the received symbols
    printf("Data received, num_symbols: %d\n", rx_data.num_symbols);
    for (int i = 0; i < rx_data.num_symbols; i++) {
        printf("[%d] level0 %d, duration0 %d\n", i,
                rx_data.received_symbols[i].level0,
                rx_data.received_symbols[i].duration0);
        printf("[%d] level1 %d, duration1 %d\n", i,
                rx_data.received_symbols[i].level1,
                rx_data.received_symbols[i].duration1);
    }

    return DHT_OK;
}


void led_start(int gpio_led, int channel, int timer, int freq_hz)
{
    // Code copied from:
    // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/ledc.html
    // https://github.com/espressif/esp-idf/tree/3640dc86bb4b007da0c53500d90e318f0b7543ef/examples/peripherals/ledc/ledc_basic

	// Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = timer,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = freq_hz,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = channel,
        .timer_sel      = timer,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = gpio_led,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // Set duty to 50%
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, LEDC_DUTY));
    // Update duty to apply the new value from ledc_set_duty()
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, channel));
}

void led_stop(int channel)
{
    ESP_ERROR_CHECK(ledc_stop(LEDC_LOW_SPEED_MODE, channel, 0 /* idle_level */));
}

void led_fade_stop(int channel)
{
	ESP_ERROR_CHECK(ledc_set_fade_with_time(
			LEDC_LOW_SPEED_MODE,
			channel,
			0, /* target_duty idle_level */
			1000 /* max_fade_time_ms */ ));
	ESP_ERROR_CHECK(ledc_fade_start(LEDC_LOW_SPEED_MODE, channel, LEDC_FADE_NO_WAIT));
}

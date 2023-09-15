/*------------------------------------------------------------------------------
---------------------------------------------------------------------------------*/

#include "driver/gpio.h"
#include "driver/ledc.h"

#include "DHT22.h"

#define LEDC_DUTY_RES     LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY         (4095) // Set duty to 50%. ((2 ** 13) - 1) * 50% = 4095
#define LEDC_FREQUENCY    (5000) // Frequency in Hertz. Set frequency at 5 kHz

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

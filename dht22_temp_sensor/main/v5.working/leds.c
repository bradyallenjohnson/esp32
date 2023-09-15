/*------------------------------------------------------------------------------
---------------------------------------------------------------------------------*/

#include "driver/gpio.h"
#include "driver/ledc.h"

#include "DHT22.h"

typedef struct led_channel_timer_ {
	uint8_t channel;
	uint8_t timer;
	LED_IntensityPercent intensity;
	ledc_timer_config_t ledc_timer;
	ledc_channel_config_t ledc_channel;
} led_info;

// map the led gpio pin number to a led_channel_timer
#define MAX_LEDS 32
led_info led_info_map[MAX_LEDS] = {};
bool ledc_fade_func_installed = false;

// To update the intensity and/or frequence, just call led_init again
bool led_init(uint8_t gpio_led, uint8_t channel, uint8_t timer, LED_IntensityPercent intensity, uint16_t freq_hz)
{
	if (gpio_led >= MAX_LEDS) {
		printf("ERROR, gpio_led [%d] exceeds the max value [%d]\n", gpio_led, MAX_LEDS);
		return false;
	}

	if (!ledc_fade_func_installed) {
		ESP_ERROR_CHECK(ledc_fade_func_install(ESP_INTR_FLAG_LEVEL1));
        ledc_fade_func_installed = true;
	}

	led_info_map[gpio_led].channel    =  channel;
	led_info_map[gpio_led].timer      =  timer;
	led_info_map[gpio_led].intensity  =  intensity;

    // Code copied from:
    // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/ledc.html
    // https://github.com/espressif/esp-idf/tree/3640dc86bb4b007da0c53500d90e318f0b7543ef/examples/peripherals/ledc/ledc_basic

	// Prepare and then apply the LEDC PWM timer configuration
	led_info_map[gpio_led].ledc_timer.speed_mode       = LEDC_LOW_SPEED_MODE;
	led_info_map[gpio_led].ledc_timer.timer_num        = timer;
	led_info_map[gpio_led].ledc_timer.duty_resolution  = LEDC_TIMER_13_BIT; // Set duty resolution to 13 bits
	led_info_map[gpio_led].ledc_timer.freq_hz          = freq_hz;
	led_info_map[gpio_led].ledc_timer.clk_cfg          = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&led_info_map[gpio_led].ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
	led_info_map[gpio_led].ledc_channel.speed_mode     = LEDC_LOW_SPEED_MODE;
	led_info_map[gpio_led].ledc_channel.channel        = channel;
	led_info_map[gpio_led].ledc_channel.timer_sel      = timer;
	led_info_map[gpio_led].ledc_channel.intr_type      = LEDC_INTR_DISABLE;
	led_info_map[gpio_led].ledc_channel.gpio_num       = gpio_led;
	led_info_map[gpio_led].ledc_channel.duty           = 0; // Set duty to 0%
	led_info_map[gpio_led].ledc_channel.hpoint         = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&led_info_map[gpio_led].ledc_channel));

    return true;
}

bool led_start(uint8_t gpio_led)
{
	if (gpio_led >= MAX_LEDS) {
		printf("ERROR, gpio_led [%d] exceeds the max value [%d]\n", gpio_led, MAX_LEDS);
		return false;
	}

    // Set duty to 50%
    ESP_ERROR_CHECK(ledc_set_duty(
    		LEDC_LOW_SPEED_MODE,
			led_info_map[gpio_led].channel,
			led_info_map[gpio_led].intensity));

    // Update duty to apply the new value from ledc_set_duty()
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE,
    		led_info_map[gpio_led].channel));

    return true;
}

bool led_stop(uint8_t gpio_led)
{
	if (gpio_led >= MAX_LEDS) {
		printf("ERROR, gpio_led [%d] exceeds the max value [%d]\n", gpio_led, MAX_LEDS);
		return false;
	}

    ESP_ERROR_CHECK(ledc_stop(
    		LEDC_LOW_SPEED_MODE,
			led_info_map[gpio_led].channel,
			0 /* idle_level */));

    return true;
}

bool led_fade(uint8_t gpio_led, LED_FadeType fade_type, uint16_t fade_millis)
{
	if (gpio_led >= MAX_LEDS) {
		printf("ERROR, gpio_led [%d] exceeds the max value [%d]\n", gpio_led, MAX_LEDS);
		return false;
	}

	ESP_ERROR_CHECK(ledc_set_fade_with_time(
			LEDC_LOW_SPEED_MODE,
			led_info_map[gpio_led].channel,
			((fade_type == LED_FADE_ON) ? led_info_map[gpio_led].intensity : 0), /* target_duty idle_level */
			fade_millis));
	ESP_ERROR_CHECK(ledc_fade_start(
			LEDC_LOW_SPEED_MODE,
			led_info_map[gpio_led].channel,
			LEDC_FADE_NO_WAIT));

    return true;
}

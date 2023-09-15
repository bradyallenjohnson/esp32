/* 
 * Simple LED library wrapper around the ESP32 ledc APIs
 */

#ifndef LEDS_H_  
#define LEDS_H_

#include "driver/ledc.h"

#define RED_LED  GPIO_NUM_5
#define BLUE_LED GPIO_NUM_18

// LED Intensity, called duty: 50% = ((2 ** 13) - 1) * 50% = 4096
typedef enum {
    LED_INTENSITY_10PERCENT  = 819,
    LED_INTENSITY_25PERCENT  = 2048,
    LED_INTENSITY_50PERCENT  = 4096,
    LED_INTENSITY_75PERCENT  = 6144,
    LED_INTENSITY_100PERCENT = 8191
} LED_IntensityPercent;

typedef enum {
    LED_FADE_ON  = 1,
    LED_FADE_OFF = 2,
} LED_FadeType;

// To update the intensity and/or frequency, just call led_init() again
bool led_init(uint8_t gpio_led, uint8_t channel, uint8_t timer, LED_IntensityPercent intensity, uint16_t freq_hz);
bool led_start(uint8_t gpio_led);
bool led_stop(uint8_t gpio_led);
bool led_fade(uint8_t gpio_led, LED_FadeType fade_type, uint16_t fade_millis);

#endif

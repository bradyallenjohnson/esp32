/*
 *
 * DHT22 sensor reading test
 *
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "rom/ets_sys.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "DHT22.h"
#include "leds.h"

// Have to wait at least 1 second before communicating with the sensor
const int INITIAL_DELAY = 1500 / portTICK_PERIOD_MS;
// Interval between data collections
const int COLLECTION_INTERVAL = 60000 / portTICK_PERIOD_MS; // 60 seconds
//const int COLLECTION_INTERVAL = 10000 / portTICK_PERIOD_MS; // 10 seconds

void DHT_task(void *pvParameter)
{
    printf("Starting DHT Task\n\n");
    void *dht_handle = setup_DHT(DHT_PIN);

    while(1) {
        printf("=== Reading DHT ===\n" );
        led_stop(BLUE_LED);
        led_start(RED_LED);
        int ret = read_DHT(dht_handle);

        error_handler(ret);
        if (ret == DHT_OK) {
            printf("Hum %.1f\n", get_humidity(dht_handle));
            printf("Tmp %.1f\n", get_temperature(dht_handle));
        }

        // Minimal interval supported by the DHT22 is 2 seconds
        led_start(BLUE_LED);
        led_fade(RED_LED, LED_FADE_OFF, 1000);
        vTaskDelay(COLLECTION_INTERVAL);
    }
}

void app_main()
{
    //nvs_flash_init();
    vTaskDelay(INITIAL_DELAY);

    // Set the LEDC peripheral configuration
    led_init(RED_LED,  LEDC_CHANNEL_0, LEDC_TIMER_0, LED_INTENSITY_50PERCENT, 1000);
    led_init(BLUE_LED, LEDC_CHANNEL_1, LEDC_TIMER_1, LED_INTENSITY_10PERCENT, 1000);

    // Launch a task to communicate with the sensor
    // - consider using xTaskCreatePinnedToCore() to pin the task to a core
    xTaskCreate(
            &DHT_task,  // Task to execute
            "DHT_task", // Task name
            2048,       // Stack size in words
            NULL,       // Task input parameter
            5,          // Priority of the task
            NULL);      // Task handle
}


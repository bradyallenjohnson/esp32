/*

	DHT22 sensor reading test

	Jun 2007: Ricardo Timmermann, implemetation


*/

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "rom/ets_sys.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#include "DHT22.h"

// Have to wait at least 1 second before communicating with the sensor
const int INITIAL_DELAY = 1500 / portTICK_PERIOD_MS;
// 10 seconds between data collections
const int COLLECTION_INTERVAL = 10000 / portTICK_PERIOD_MS;

void DHT_task(void *pvParameter)
{
    printf("Starting DHT Task\n\n");
    void *dht_handle = setup_DHT(DHT_PIN);

    while(1) {

        printf("=== Reading DHT ===\n" );
        led_stop(LEDC_CHANNEL_1);
        led_start(RED_LED,  LEDC_CHANNEL_0, LEDC_TIMER_0, 1000);
        //led_fade_stop(LEDC_CHANNEL_0);
        int ret = read_DHT(dht_handle);

        error_handler(ret);
        if (ret == DHT_OK) {
            printf("Hum %.1f\n", get_humidity(dht_handle));
            printf("Tmp %.1f\n", get_temperature(dht_handle));
        }

        // -- wait at least 2 sec before reading again ------------
        led_start(BLUE_LED, LEDC_CHANNEL_1, LEDC_TIMER_1, 1000);
        led_stop(LEDC_CHANNEL_0);
        vTaskDelay(COLLECTION_INTERVAL);
    }
}

void app_main()
{
    //nvs_flash_init();
    vTaskDelay(INITIAL_DELAY);

    // Set the LEDC peripheral configuration
    //ESP_ERROR_CHECK(ledc_fade_func_install(ESP_INTR_FLAG_LEVEL1));
    led_start(RED_LED,  LEDC_CHANNEL_0, LEDC_TIMER_0, 1000);
    led_start(BLUE_LED, LEDC_CHANNEL_1, LEDC_TIMER_1, 1000);
    led_stop(LEDC_CHANNEL_0);
    led_stop(LEDC_CHANNEL_1);

    // Launch a task to communicate with the sensor
    // TODO consider using xTaskCreatePinnedToCore() to pin the task to a core
    xTaskCreate(
            &DHT_task,  // Task to execute
            "DHT_task", // Task name
            2048,       // Stack size in words
            NULL,       // Task input parameter
            5,          // Priority of the task
            NULL);      // Task handle
}


/*
 * Simple Interrupt Service Routine (ISR) listener to listen for level changes
 * on a selected GPIO pin.
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

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "driver/gpio.h"

#include "ISR_listener.h"

/*
 * Global variables
 */

/* Captured data at an event: pin up or pin down
 * The level is the new level, micros are the microseconds
 * since the last event.
 */
typedef struct isr_data_ {
    uint64_t micros;
    uint8_t  level;
} isr_data;

typedef struct isr_context_ {
    int isr_gpio_pin;
    bool stop_at_max; // When this limit is reached, stop capturing or loop back to 0
    bool is_capturing;
    uint8_t isr_data_index_max; // Max number of samples to collect
    uint8_t isr_data_index;
    isr_data *isr_data_list;
    portMUX_TYPE isr_spinlock;

} isr_context;

static bool ISR_SERVICE_INSTALLED = false;


/*
 * Internal ISR callback
 */

static void gpio_isr_edge_handler(void *arg)
{
    if (arg == NULL) {
        return;
    }
    isr_context *context = (isr_context *) arg;

    taskENTER_CRITICAL_ISR(&context->isr_spinlock);

    if (context->isr_data_index >= context->isr_data_index_max) {
        if (context->stop_at_max == true) {
            ISR_stop(arg);
            taskEXIT_CRITICAL_ISR(&context->isr_spinlock);
            return;
        } else {
            context->isr_data_index = 0;
        }
    }

    isr_data *isrd = &context->isr_data_list[context->isr_data_index++];
    isrd->micros = esp_timer_get_time();
    isrd->level  = gpio_get_level(context->isr_gpio_pin);

    taskEXIT_CRITICAL_ISR(&context->isr_spinlock);
}

/*
 * API functions
 */

void *ISR_setup_listener(int isr_gpio_pin, uint8_t max_captures, bool stop_at_max)
{
    isr_context *isrc = malloc(sizeof(isr_context));
    isrc->isr_data_list = malloc(sizeof(isr_data) * max_captures);

    isrc->isr_gpio_pin = isr_gpio_pin;
    isrc->isr_data_index_max = max_captures;
    isrc->isr_data_index = 0;
    isrc->stop_at_max = stop_at_max;
    isrc->is_capturing = false;
    portMUX_INITIALIZE(&isrc->isr_spinlock);

    ESP_ERROR_CHECK(gpio_set_level(isr_gpio_pin, 0));
    ESP_ERROR_CHECK(gpio_set_direction(isr_gpio_pin, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_intr_type(isr_gpio_pin, GPIO_INTR_ANYEDGE));

    if (ISR_SERVICE_INSTALLED == false) {
        printf("Installing isr service %d\n", ESP_INTR_FLAG_LEVEL3);
        ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3));
        ISR_SERVICE_INSTALLED = true;
    }

    return (void *) isrc;
}


void ISR_set_gpio_pullmode(int isr_gpio_pin, gpio_pull_mode_t mode)
{
    ESP_ERROR_CHECK(gpio_set_pull_mode(isr_gpio_pin, mode));
}


void ISR_delete(void *isrc)
{
    assert(isrc != NULL);
    isr_context *context = (isr_context *) isrc;

    taskENTER_CRITICAL(&context->isr_spinlock);

    ISR_stop(context);

    portMUX_TYPE isr_spinlock = context->isr_spinlock;
    free(context->isr_data_list);
    free(context);

    taskEXIT_CRITICAL(&isr_spinlock);
}


void ISR_start(void *isrc)
{
    assert(isrc != NULL);
    isr_context *context = (isr_context *) isrc;

	printf("ISR_start\n");

    taskENTER_CRITICAL(&context->isr_spinlock);

    ESP_ERROR_CHECK(gpio_isr_handler_add(context->isr_gpio_pin, gpio_isr_edge_handler, isrc));
    context->is_capturing = true;

    taskEXIT_CRITICAL(&context->isr_spinlock);
}


void ISR_stop(void *isrc)
{
    assert(isrc != NULL);
    isr_context *context = (isr_context *) isrc;

    taskENTER_CRITICAL(&context->isr_spinlock);

    ESP_ERROR_CHECK(gpio_isr_handler_remove(context->isr_gpio_pin));
    context->is_capturing = false;

    taskEXIT_CRITICAL(&context->isr_spinlock);

    //printf("ISR_stop\n");
}


void ISR_dump(void *isrc)
{
    assert(isrc != NULL);
    isr_context *context = (isr_context *) isrc;

    // Cant printf in a task critical section
    printf("ISR Data index: %d\n", context->isr_data_index);

    // Since its not possible to printf in a task critical section, instead of
    // locking and unlocking for each entry, just copy the list in a critical
    // section, then process the copy in an non-critical section

    taskENTER_CRITICAL(&context->isr_spinlock);
    if (context->isr_data_index == 0) {
        // Nothing to dump
        taskEXIT_CRITICAL(&context->isr_spinlock);
        return;
    }
    uint8_t isr_data_index = context->isr_data_index;
    isr_data *isr_data_list = malloc(sizeof(isr_data) * context->isr_data_index);
    memcpy(isr_data_list, context->isr_data_list, sizeof(isr_data) * context->isr_data_index);
    taskEXIT_CRITICAL(&context->isr_spinlock);

    for (int i = 0; i < isr_data_index; i++) {
        isr_data *isrd = &isr_data_list[i];
        uint64_t duration = ((i == 0) ? 0 : (isrd->micros - isr_data_list[i-1].micros));

        // Cant printf in a critical section
        // Notice we are negating the level in this output this is because the level
        // is the transition event, so the duration coincides with the previous level
        printf("\t [%02d] [%lld] duration %8lld, level %d\n",
               i, isrd->micros, duration, !isrd->level);
    }

    free(isr_data_list);
}


void ISR_reset(void *isrc)
{
    assert(isrc != NULL);
    isr_context *context = (isr_context *) isrc;

    taskENTER_CRITICAL(&context->isr_spinlock);
	context->isr_data_index = 0;
    taskEXIT_CRITICAL(&context->isr_spinlock);
}


uint8_t ISR_num_captures(void *isrc)
{
    assert(isrc != NULL);
    isr_context *context = (isr_context *) isrc;

    taskENTER_CRITICAL(&context->isr_spinlock);
    uint8_t index = context->isr_data_index;
    taskEXIT_CRITICAL(&context->isr_spinlock);

    return(index);
}


bool ISR_is_capturing(void *isrc)
{
    assert(isrc != NULL);
    isr_context *context = (isr_context *) isrc;

    taskENTER_CRITICAL(&context->isr_spinlock);
    bool is_capturing = context->is_capturing;
    taskEXIT_CRITICAL(&context->isr_spinlock);

    return(is_capturing);
}

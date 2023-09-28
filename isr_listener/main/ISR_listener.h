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

#ifndef ISR22_H_
#define ISR22_H_

#include <stdlib.h>  // uint8_t, bool

/* Notice:
 * All functions in this API are thread/interrupt safe by using a portMUX_TYPE.
 * https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos_idf.html?highlight=atomic#critical-sections
 */

/* Initialize event capturing, call ISR_start() to actually start capturing
 * When finished, call ISR_delete() to free resources
 */
void *ISR_setup_listener(
        int isr_gpio_pin,        // The GPIO pin to listen to
        uint8_t max_captures,    // max number of events to capture, determines how much memory to allocate
        bool stop_at_max);       // Stop capturing or start recording at index 0

/* Stop capturing and free allocated memory
 * Call after ISR_setup_listener()
 */
void ISR_delete(void *isrc);

/* Start capturing GPIO pin events
 * Call ISR_stop() to stop capturing events
 */
void ISR_start(void *isrc);

/* Stop capturing GPIO pint events
 * Call ISR_start() to start capturing events
 */
void ISR_stop(void *isrc);

/* Reset the event capture index to 0
 */
void ISR_reset(void *isrc);

/* Dump the captured data
 */
void ISR_dump(void *isrc);

/* Number of capture events
 */
uint8_t ISR_num_captures(void *isrc);

/* Returns if events are currently being captured
 */
bool ISR_is_capturing(void *isrc);

#endif

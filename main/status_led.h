#pragma once

#include "esp_err.h"

typedef enum {
    STATUS_LED_BOOTING,
    STATUS_LED_READY,
    STATUS_LED_CAPTURING,
    STATUS_LED_ERROR,
} status_led_state_t;

/** Initialise the single addressable RGB LED on GPIO45. */
esp_err_t status_led_init(void);

/** Set a low-brightness status colour. Safe to call when LED init failed. */
void status_led_set(status_led_state_t state);

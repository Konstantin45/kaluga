#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*input_capture_callback_t)(void);

/**
 * Start the TouchA capture input.
 *
 * Only TouchA's RECORD electrode (ESP32-S2 touch channel 5 / GPIO5) is
 * configured.  The callback runs in a FreeRTOS task, never in the touch ISR.
 */
esp_err_t input_service_start(input_capture_callback_t capture_cb);

#ifdef __cplusplus
}
#endif

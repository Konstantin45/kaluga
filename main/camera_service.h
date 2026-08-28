#pragma once

#include "esp_err.h"

/** Initialise the Kaluga OV2640 using the BSP's PSRAM RGB565 defaults. */
esp_err_t camera_service_start(void);

/** Queue one still-frame capture. The request is ignored while busy. */
esp_err_t camera_service_request_capture(void);

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "network_service.h"

/** Start the Kaluga ST7789 display and create the camera page. */
esp_err_t board_ui_init(void);

/** Replace the short status line below the preview. Thread-safe. */
void board_ui_show_status(const char *status);

/** Copy an RGB565 QVGA camera frame into the persistent LVGL preview. */
esp_err_t board_ui_show_camera_frame(const uint8_t *pixels, size_t length,
                                     uint16_t width, uint16_t height);

/** Show the strongest scanned Wi-Fi networks over the preview. Thread-safe. */
void board_ui_show_networks(const network_service_ap_t *aps, size_t count);

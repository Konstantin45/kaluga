#include <stdbool.h>

#include "esp_check.h"
#include "esp_log.h"

#include "board_ui.h"
#include "camera_service.h"
#include "input_service.h"
#include "network_service.h"
#include "status_led.h"

static const char *TAG = "kaluga_demo";

static void capture_requested(void)
{
    status_led_set(STATUS_LED_CAPTURING);
    const esp_err_t err = camera_service_request_capture();
    if (err == ESP_ERR_TIMEOUT) {
        board_ui_show_status("Capture already pending");
        status_led_set(STATUS_LED_READY);
    } else if (err != ESP_OK) {
        board_ui_show_status("Camera is not ready");
        status_led_set(STATUS_LED_ERROR);
    }
}

static void network_event(const network_service_event_t *event, void *context)
{
    (void)context;
    if (event->kind == NETWORK_SERVICE_EVENT_SCAN_DONE) {
        board_ui_show_networks(event->aps, event->ap_count);
    }
    if (event->message != NULL) {
        board_ui_show_status(event->message);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Kaluga camera demo");

    /* Wi-Fi needs a contiguous internal DMA pool. Reserve it before LCD and
     * camera allocate their transfer buffers; scan only after the UI exists. */
    esp_err_t err = network_service_start(network_event, NULL);
    const bool network_started = err == ESP_OK;
    if (!network_started) {
        ESP_LOGW(TAG, "Wi-Fi unavailable: %s", esp_err_to_name(err));
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(status_led_init());
    ESP_ERROR_CHECK(board_ui_init());
    err = camera_service_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera start failed: %s", esp_err_to_name(err));
        board_ui_show_status("Camera failed - check CAM cable");
        status_led_set(STATUS_LED_ERROR);
    } else {
        board_ui_show_status("Camera ready - press Record");
        status_led_set(STATUS_LED_READY);
    }

    err = input_service_start(capture_requested);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Touch input unavailable: %s", esp_err_to_name(err));
    }

    if (network_started) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(network_service_request_scan());
#if CONFIG_KALUGA_WIFI_CONNECT_ENABLED
        ESP_ERROR_CHECK_WITHOUT_ABORT(network_service_connect_configured());
#endif
    }
}

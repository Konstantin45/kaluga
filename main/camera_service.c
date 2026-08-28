#include "camera_service.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "bsp/esp-bsp.h"
#include "esp_camera.h"
#include "esp_check.h"
#include "esp_log.h"

#include "board_ui.h"
#include "status_led.h"

static const char *TAG = "camera_service";
static QueueHandle_t s_capture_queue;

static void camera_capture_task(void *argument)
{
    (void) argument;
    for (;;) {
        uint8_t request;
        xQueueReceive(s_capture_queue, &request, portMAX_DELAY);

        board_ui_show_status("Capturing...");
        camera_fb_t *frame = esp_camera_fb_get();
        if (frame == NULL) {
            ESP_LOGE(TAG, "Camera frame acquisition failed");
            board_ui_show_status("Capture failed - check CAM cable");
            status_led_set(STATUS_LED_ERROR);
            continue;
        }

        esp_err_t err = board_ui_show_camera_frame(frame->buf, frame->len,
                                                    frame->width, frame->height);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Camera frame displayed (%ux%u, %u bytes)",
                     frame->width, frame->height, (unsigned int)frame->len);
        }
        esp_camera_fb_return(frame);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Frame display failed: %s", esp_err_to_name(err));
            board_ui_show_status("Could not show photo");
            status_led_set(STATUS_LED_ERROR);
            continue;
        }
        board_ui_show_status("Photo captured - tap Capture again");
        status_led_set(STATUS_LED_READY);
    }
}

esp_err_t camera_service_start(void)
{
    /* The camera SCCB bus is shared with the Kaluga audio board.  The BSP
     * owns I2C1 on GPIO7/GPIO8; esp32-camera must attach to that existing
     * bus (the documented BSP configuration), rather than instantiate I2C0.
     */
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "I2C bus initialisation failed");

    camera_config_t config = BSP_CAMERA_DEFAULT_CONFIG;

    /* The current generic BSP pinout is for the later Kaluga revision.
     * Kaluga-1 v1.2 routes the CAM v1.0 D2/D3 labels to GPIO46/GPIO45.
     * The SCCB pins intentionally stay GPIO_NUM_NC so esp32-camera uses
     * config.sccb_i2c_port (BSP_I2C_NUM / I2C1) set by the BSP macro.
     */
    config.pin_d0 = GPIO_NUM_46;
    config.pin_d1 = GPIO_NUM_45;
    esp_err_t err = ESP_FAIL;
    for (unsigned int attempt = 1; attempt <= 2; ++attempt) {
        err = esp_camera_init(&config);
        if (err == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "Camera initialisation attempt %u failed: %s", attempt,
                 esp_err_to_name(err));
        (void)esp_camera_deinit();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_RETURN_ON_ERROR(err, TAG, "Camera initialisation failed");

    sensor_t *sensor = esp_camera_sensor_get();
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_FAIL, TAG, "Camera sensor handle is unavailable");
    ESP_RETURN_ON_FALSE(sensor->set_vflip(sensor, BSP_CAMERA_VFLIP) == 0,
                        ESP_FAIL, TAG, "Camera vertical orientation failed");
    ESP_RETURN_ON_FALSE(sensor->set_hmirror(sensor, BSP_CAMERA_HMIRROR) == 0,
                        ESP_FAIL, TAG, "Camera mirror configuration failed");
    ESP_LOGI(TAG, "Camera ready: sensor PID 0x%04x, QVGA RGB565 in PSRAM",
             sensor->id.PID);

    s_capture_queue = xQueueCreate(1, sizeof(uint8_t));
    if (s_capture_queue == NULL) {
        esp_camera_deinit();
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(camera_capture_task, "camera_capture", 4096, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_capture_queue);
        s_capture_queue = NULL;
        esp_camera_deinit();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t camera_service_request_capture(void)
{
    if (s_capture_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t request = 1;
    return xQueueSend(s_capture_queue, &request, 0) == pdPASS ? ESP_OK : ESP_ERR_TIMEOUT;
}

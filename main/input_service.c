#include "input_service.h"

#include <stdint.h>

#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/touch_sens.h"

/* TouchA RECORD is the only button not shared with LCD32 v1.1 or CAM v1.0. */
#define INPUT_CAPTURE_TOUCH_CHANNEL 5
#define INPUT_DEBOUNCE_MS            250
#define INPUT_TASK_STACK_SIZE        3072
#define INPUT_TASK_PRIORITY          5

static const char *TAG = "input_service";

static QueueHandle_t s_capture_events;
static input_capture_callback_t s_capture_cb;
static touch_sensor_handle_t s_touch;
static touch_channel_handle_t s_capture_channel;

static bool IRAM_ATTR input_on_touch_active(touch_sensor_handle_t sensor,
                                            const touch_active_event_data_t *event,
                                            void *user_ctx)
{
    (void)sensor;
    (void)user_ctx;

    if (event->chan_id != INPUT_CAPTURE_TOUCH_CHANNEL) {
        return false;
    }

    const uint8_t capture_event = 1;
    BaseType_t higher_priority_task_woken = pdFALSE;
    xQueueSendFromISR(s_capture_events, &capture_event, &higher_priority_task_woken);
    return higher_priority_task_woken == pdTRUE;
}

static void input_event_task(void *arg)
{
    (void)arg;
    uint8_t event;
    TickType_t last_capture = 0;

    for (;;) {
        if (xQueueReceive(s_capture_events, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        const TickType_t now = xTaskGetTickCount();
        if (last_capture != 0 && (now - last_capture) < pdMS_TO_TICKS(INPUT_DEBOUNCE_MS)) {
            continue;
        }

        last_capture = now;
        s_capture_cb();
    }
}

static void input_service_rollback(void)
{
    if (s_touch != NULL) {
        (void)touch_sensor_stop_continuous_scanning(s_touch);
        (void)touch_sensor_disable(s_touch);
    }
    if (s_capture_channel != NULL) {
        (void)touch_sensor_del_channel(s_capture_channel);
        s_capture_channel = NULL;
    }
    if (s_touch != NULL) {
        (void)touch_sensor_del_controller(s_touch);
        s_touch = NULL;
    }
    if (s_capture_events != NULL) {
        vQueueDelete(s_capture_events);
        s_capture_events = NULL;
    }
    s_capture_cb = NULL;
}

esp_err_t input_service_start(input_capture_callback_t capture_cb)
{
    ESP_RETURN_ON_FALSE(capture_cb != NULL, ESP_ERR_INVALID_ARG, TAG, "capture callback is required");
    ESP_RETURN_ON_FALSE(s_touch == NULL, ESP_ERR_INVALID_STATE, TAG, "input service already started");

    esp_err_t err;
    s_capture_events = xQueueCreate(4, sizeof(uint8_t));
    ESP_RETURN_ON_FALSE(s_capture_events != NULL, ESP_ERR_NO_MEM, TAG, "event queue allocation failed");
    s_capture_cb = capture_cb;

    touch_sensor_sample_config_t sample_config[TOUCH_SAMPLE_CFG_NUM] = {
        TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(500, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V2),
    };
    touch_sensor_config_t sensor_config =
        TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(TOUCH_SAMPLE_CFG_NUM, sample_config);
    touch_channel_config_t channel_config = {
        .active_thresh = { 300 },
        .charge_speed = TOUCH_CHARGE_SPEED_7,
        .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
    };

    err = touch_sensor_new_controller(&sensor_config, &s_touch);
    if (err != ESP_OK) {
        goto fail;
    }
    err = touch_sensor_new_channel(s_touch, INPUT_CAPTURE_TOUCH_CHANNEL,
                                   &channel_config, &s_capture_channel);
    if (err != ESP_OK) {
        goto fail;
    }

    touch_sensor_filter_config_t filter_config = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    filter_config.data.debounce_cnt = 3;
    err = touch_sensor_config_filter(s_touch, &filter_config);
    if (err != ESP_OK) {
        goto fail;
    }

    const touch_event_callbacks_t callbacks = {
        .on_active = input_on_touch_active,
    };
    err = touch_sensor_register_callbacks(s_touch, &callbacks, NULL);
    if (err != ESP_OK) {
        goto fail;
    }
    err = touch_sensor_enable(s_touch);
    if (err != ESP_OK) {
        goto fail;
    }
    err = touch_sensor_start_continuous_scanning(s_touch);
    if (err != ESP_OK) {
        goto fail;
    }

    if (xTaskCreate(input_event_task, "touch_capture", INPUT_TASK_STACK_SIZE, NULL,
                    INPUT_TASK_PRIORITY, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    ESP_LOGI(TAG, "TouchA RECORD capture input ready");
    return ESP_OK;

fail:
    input_service_rollback();
    return err;
}

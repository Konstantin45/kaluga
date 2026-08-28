#include "status_led.h"

#include "esp_log.h"

static const char *TAG = "status_led";

esp_err_t status_led_init(void)
{
    /* Kaluga-1 v1.2 routes camera D1 and the addressable RGB LED to GPIO45.
     * The camera demo owns that pin, so status is shown on the LCD instead. */
    ESP_LOGI(TAG, "RGB LED disabled: GPIO45 is reserved for camera D1 on Kaluga v1.2");
    return ESP_OK;
}

void status_led_set(status_led_state_t state)
{
    (void)state;
}

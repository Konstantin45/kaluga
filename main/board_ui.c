#include "board_ui.h"

#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "lvgl.h"

#include "camera_service.h"
#include "board_hardware.h"

static const char *TAG = "board_ui";

static lv_obj_t *s_status_label;
static lv_obj_t *s_image;
static lv_obj_t *s_network_panel;
static lv_obj_t *s_network_label;
static uint8_t *s_frame_buffer;
static size_t s_frame_buffer_size;
static lv_image_dsc_t s_frame_dsc;

static lv_display_t *board_ui_start_display(void)
{
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    const bsp_display_config_t panel_config = {
        .max_transfer_sz = BSP_LCD_H_RES * 20 * sizeof(uint16_t),
    };
    if (bsp_display_new(&panel_config, &panel, &io) != ESP_OK) {
        ESP_LOGE(TAG, "LCD panel initialisation failed");
        return NULL;
    }
    if (esp_lcd_panel_disp_on_off(panel, true) != ESP_OK) {
        ESP_LOGE(TAG, "LCD panel power-on failed");
        return NULL;
    }

    const lvgl_port_cfg_t lvgl_config = ESP_LVGL_PORT_INIT_CONFIG();
    if (lvgl_port_init(&lvgl_config) != ESP_OK) {
        ESP_LOGE(TAG, "LVGL initialisation failed");
        return NULL;
    }

    /* The LCD32 v1.1 mounted in this kit needs swap-XY with both mirror
     * flags cleared.  This matches the user's physical viewing direction
     * and keeps text readable rather than horizontally mirrored. */
    const lvgl_port_display_cfg_t display_config = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = BSP_LCD_H_RES * 20,
        .double_buffer = false,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = true,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .swap_bytes = BSP_LCD_BIGENDIAN ? true : false,
        },
    };

    return lvgl_port_add_disp(&display_config);
}

static void capture_event_cb(lv_event_t *event)
{
    (void) event;
    esp_err_t err = camera_service_request_capture();
    if (err == ESP_ERR_TIMEOUT) {
        board_ui_show_status("Capture already pending");
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not queue capture: %s", esp_err_to_name(err));
        board_ui_show_status("Could not start capture");
    }
}

esp_err_t board_ui_init(void)
{
    const gpio_config_t backlight_config = {
        .pin_bit_mask = 1ULL << BOARD_LCD_BACKLIGHT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&backlight_config), TAG,
                        "LCD backlight GPIO configuration failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_LCD_BACKLIGHT_GPIO,
                                       BOARD_LCD_BACKLIGHT_ON_LEVEL),
                        TAG, "LCD backlight enable failed");
    ESP_LOGI(TAG, "LCD32 v1.1 backlight enabled on GPIO%d (active low)",
             BOARD_LCD_BACKLIGHT_GPIO);

    lv_display_t *display = board_ui_start_display();
    if (display == NULL) {
        return ESP_FAIL;
    }
    if (!bsp_display_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }
    bsp_display_rotate(display, LV_DISPLAY_ROTATION_0);

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B5FA5), 0);
    lv_obj_set_style_pad_all(screen, 8, 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Kaluga Camera");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    s_image = lv_image_create(screen);
    lv_obj_set_size(s_image, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_align(s_image, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_image, lv_color_hex(0x263238), 0);
    lv_obj_set_style_bg_opa(s_image, LV_OPA_COVER, 0);

    s_status_label = lv_label_create(screen);
    lv_label_set_text(s_status_label, "Initialising camera...");
    lv_obj_set_width(s_status_label, BSP_LCD_H_RES - 110);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(s_status_label, lv_color_white(), 0);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *capture_button = lv_button_create(screen);
    lv_obj_set_size(capture_button, 96, 36);
    lv_obj_align(capture_button, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(capture_button, capture_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *capture_label = lv_label_create(capture_button);
    lv_label_set_text(capture_label, "Capture");
    lv_obj_center(capture_label);

    s_network_panel = lv_obj_create(screen);
    lv_obj_set_size(s_network_panel, BSP_LCD_H_RES - 32, BSP_LCD_V_RES - 70);
    lv_obj_align(s_network_panel, LV_ALIGN_CENTER, 0, 6);
    lv_obj_set_style_bg_color(s_network_panel, lv_color_hex(0x17232d), 0);
    lv_obj_set_style_bg_opa(s_network_panel, LV_OPA_90, 0);
    lv_obj_add_flag(s_network_panel, LV_OBJ_FLAG_HIDDEN);

    s_network_label = lv_label_create(s_network_panel);
    lv_label_set_text(s_network_label, "Scanning Wi-Fi...");
    lv_obj_set_width(s_network_label, BSP_LCD_H_RES - 56);
    lv_label_set_long_mode(s_network_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s_network_label, lv_color_white(), 0);
    lv_obj_align(s_network_label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_refr_now(NULL);
    bsp_display_unlock();
    return ESP_OK;
}

void board_ui_show_status(const char *status)
{
    if (s_status_label == NULL || status == NULL || !bsp_display_lock(0)) {
        return;
    }
    lv_label_set_text(s_status_label, status);
    bsp_display_unlock();
}

static inline uint16_t rgb565_swap_red_blue(uint16_t pixel)
{
    return (uint16_t)(((pixel & 0x001fU) << 11) |
                      (pixel & 0x07e0U) |
                      ((pixel & 0xf800U) >> 11));
}

esp_err_t board_ui_show_camera_frame(const uint8_t *pixels, size_t length,
                                     uint16_t width, uint16_t height)
{
    const size_t expected = (size_t) width * height * 2;
    if (pixels == NULL || width != BSP_LCD_H_RES || height != BSP_LCD_V_RES || length < expected) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_frame_buffer == NULL) {
        s_frame_buffer = heap_caps_malloc(expected, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_frame_buffer == NULL) {
            return ESP_ERR_NO_MEM;
        }
        s_frame_buffer_size = expected;
    }
    if (s_frame_buffer_size < expected) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t *corrected = s_frame_buffer;
    for (size_t offset = 0; offset < expected; offset += 2) {
        const uint16_t pixel = ((uint16_t)pixels[offset] << 8) | pixels[offset + 1];
        const uint16_t corrected_pixel = rgb565_swap_red_blue(pixel);
        corrected[offset] = (uint8_t)(corrected_pixel >> 8);
        corrected[offset + 1] = (uint8_t)corrected_pixel;
    }

    if (!bsp_display_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }
    s_frame_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    /* OV2640 delivers RGB565 with the two bytes of each pixel in wire order. */
    s_frame_dsc.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
    s_frame_dsc.header.w = width;
    s_frame_dsc.header.h = height;
    s_frame_dsc.data_size = expected;
    s_frame_dsc.data = s_frame_buffer;
    lv_image_set_src(s_image, &s_frame_dsc);
    lv_obj_add_flag(s_network_panel, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
    return ESP_OK;
}

void board_ui_show_networks(const network_service_ap_t *aps, size_t count)
{
    if (s_network_panel == NULL || s_network_label == NULL ||
        !bsp_display_lock(pdMS_TO_TICKS(1000))) {
        return;
    }

    char text[384];
    size_t used = strlcpy(text, "Nearby Wi-Fi\n", sizeof(text));
    const size_t shown = count > 6 ? 6 : count;
    for (size_t i = 0; i < shown && used < sizeof(text); ++i) {
        const int written = snprintf(text + used, sizeof(text) - used,
                                     "%s  %d dBm\n", aps[i].ssid, aps[i].rssi);
        if (written < 0 || (size_t)written >= sizeof(text) - used) {
            break;
        }
        used += (size_t)written;
    }
    if (shown == 0) {
        strlcpy(text, "Nearby Wi-Fi\nNo networks found", sizeof(text));
    }
    lv_label_set_text(s_network_label, text);
    lv_obj_remove_flag(s_network_panel, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
}

# Kaluga Camera Demo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a camera-first ESP-IDF demo for ESP32-S2-Kaluga-1 v1.2 that captures a still image to LCD and exposes Wi-Fi scanning through LVGL.

**Architecture:** The board UI module owns Kaluga BSP and LVGL. Camera and network workers receive queued requests; their UI results use LVGL-safe asynchronous updates. Audio is not initialised because camera and audio board share the ESP32-S2 I2S controller.

**Tech Stack:** ESP-IDF 6.1, C, FreeRTOS, ESP Component Manager, Kaluga BSP, esp32-camera, LVGL, ESP-Wi-Fi.

## Global Constraints

- Target: Kaluga-1 v1.2, LCD32 v1.1, TouchA v1.1, CAM v1.0, 8311A v1.2.
- Do not initialise audio or TouchA GPIO 1, 2, 3, 6, or 11. Use only
  TouchA Record on GPIO5 / Touch pad 5 for capture.
- Disconnect CAM v1.0 and 8311A v1.2 before flashing; reconnect CAM and power-cycle before testing.
- Wi-Fi credentials are menuconfig values and must not be committed.

---

### Task 1: Create the reproducible ESP-IDF project

**Files:**
- Create: `CMakeLists.txt`, `sdkconfig.defaults`, `main/CMakeLists.txt`, `main/idf_component.yml`, `main/Kconfig.projbuild`, `main/main.c`
- Test: ESP-IDF build output

**Interfaces:** Produces `void app_main(void)` for `esp32s2`; consumes ESP-IDF 6.1 and Component Manager.

- [ ] **Step 1: Add root and component CMake files**

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(kaluga_camera_demo)
```

```cmake
idf_component_register(SRCS "main.c" INCLUDE_DIRS ".")
```

- [ ] **Step 2: Declare project-local managed components**

```yaml
dependencies:
  idf: { version: ">=6.1" }
  espressif/esp32_s2_kaluga_kit: "*"
  espressif/esp32-camera: "*"
```

- [ ] **Step 3: Set target defaults and prove skeleton build**

```ini
CONFIG_IDF_TARGET="esp32s2"
CONFIG_ESP32S2_SPIRAM_SUPPORT=y
CONFIG_ESP32S2_SPIRAM_SPEED_80M=y
```

```c
#include "esp_log.h"
void app_main(void) { ESP_LOGI("kaluga_demo", "Kaluga camera demo starting"); }
```

Run: `. /home/ubuntu24/esp/esp-idf/export.sh && idf.py set-target esp32s2 build`

Expected: Component Manager downloads dependencies to `managed_components/`; build passes and creates `build/kaluga_camera_demo.bin`.

- [ ] **Step 4: Commit**

Run: `git add CMakeLists.txt sdkconfig.defaults main && git commit -m "feat: scaffold Kaluga camera demo"`

### Task 2: Start LCD and LVGL from the Kaluga BSP

**Files:**
- Create: `main/board_ui.h`, `main/board_ui.c`
- Modify: `main/main.c`
- Test: startup log and LCD message

**Interfaces:** Produces `esp_err_t board_ui_init(void)`, `void board_ui_show_status(const char *)`, and `void board_ui_show_jpeg(const uint8_t *, size_t)`; consumes Kaluga display BSP and LVGL locking.

- [ ] **Step 1: Add calls that initially fail to link**

```c
ESP_ERROR_CHECK(board_ui_init());
board_ui_show_status("Display ready");
```

Run: `. /home/ubuntu24/esp/esp-idf/export.sh && idf.py build`

Expected: FAIL with unresolved `board_ui_*` symbols.

- [ ] **Step 2: Implement board UI ownership**

```c
esp_err_t board_ui_init(void) { bsp_display_cfg_t cfg = { .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(), .buffer_size = BSP_LCD_H_RES * 40, .double_buffer = true }; BSP_ERROR_CHECK_RETURN_ERR(bsp_display_start_with_config(&cfg)); return ESP_OK; }
```

Create Camera and Network pages plus a persistent status label under the LVGL lock. Worker results must use `lv_async_call` before changing widgets.

- [ ] **Step 3: Build and measure**

Run: `. /home/ubuntu24/esp/esp-idf/export.sh && idf.py build size`

Expected: PASS and size output; no unresolved display symbols.

- [ ] **Step 4: Commit**

Run: `git add main/board_ui.c main/board_ui.h main/main.c && git commit -m "feat: initialise Kaluga LCD and LVGL"`

### Task 3: Capture a still image and render it on LCD

**Files:**
- Create: `main/camera_service.h`, `main/camera_service.c`
- Modify: `main/board_ui.h`, `main/board_ui.c`, `main/main.c`
- Test: capture log and rendered photo

**Interfaces:** Produces `camera_service_start` and `camera_service_request_capture`; consumes `board_ui_show_status` and `board_ui_show_jpeg`.

- [ ] **Step 1: Define and call the missing service**

```c
esp_err_t camera_service_start(void);
esp_err_t camera_service_request_capture(void);
```

Run: `. /home/ubuntu24/esp/esp-idf/export.sh && idf.py build`

Expected: FAIL until the service is compiled and registered.

- [ ] **Step 2: Initialise PSRAM-backed camera capture**

```c
camera_config_t config = BSP_CAMERA_DEFAULT_CONFIG; config.pixel_format = PIXFORMAT_JPEG; config.frame_size = FRAMESIZE_QVGA; config.jpeg_quality = 12; config.fb_count = 1; config.fb_location = CAMERA_FB_IN_PSRAM; ESP_RETURN_ON_ERROR(esp_camera_init(&config), TAG, "camera init failed");
```

Use a FreeRTOS queue of length one and a capture worker. It gets one JPEG frame, decodes it into a bounded PSRAM RGB565 canvas, asynchronously updates the LVGL image, and returns the frame buffer promptly. On failure, show `Camera capture failed — check CAM cable` without rebooting.

- [ ] **Step 3: Build and test the hardware route**

Run: `. /home/ubuntu24/esp/esp-idf/export.sh && idf.py build`

Expected: PASS and `esp_camera` links.

Run: disconnect CAM and 8311A, flash; reconnect CAM, power-cycle, then run `idf.py -p /dev/ttyUSB0 monitor` with the detected port.

Expected: `Camera ready`, then `Frame captured`, and a fresh image on LCD.

- [ ] **Step 4: Commit**

Run: `git add main/camera_service.c main/camera_service.h main/board_ui.c main/board_ui.h main/main.c && git commit -m "feat: capture and render Kaluga camera frames"`

### Task 4: Add safe TouchA capture input and screen control

**Files:**
- Create: `main/input_service.h`, `main/input_service.c`
- Modify: `main/board_ui.c`, `main/main.c`
- Test: one touch yields one capture request

**Interfaces:** Produces `esp_err_t input_service_start(void (*capture_cb)(void))`; consumes `camera_service_request_capture`.

- [ ] **Step 1: Define the input boundary**

```c
typedef void (*input_capture_callback_t)(void);
esp_err_t input_service_start(input_capture_callback_t capture_cb);
```

- [ ] **Step 2: Implement only non-conflicting TouchA**

```c
static const touch_pad_t k_capture_touch = TOUCH_PAD_NUM5;
```

Verify this against the installed BSP’s named macro and use the macro when available. The ISR only notifies a task; that task debounces and invokes `capture_cb`. Never initialise GPIO 1, 2, 3, 6, or 11. Do not use Touch pad 4 (guard) or 14 (shield) as input buttons.

- [ ] **Step 3: Add an LVGL capture button**

```c
lv_obj_t *capture = lv_button_create(camera_screen); lv_obj_add_event_cb(capture, capture_button_event, LV_EVENT_CLICKED, NULL);
```

The callback calls only `camera_service_request_capture()`.

- [ ] **Step 4: Build and verify forbidden pins**

Run: `. /home/ubuntu24/esp/esp-idf/export.sh && idf.py build && rg 'GPIO_(NUM_)?(1|2|3|6|11)' main/input_service.c`

Expected: build PASS; search returns no configured conflicting pins.

- [ ] **Step 5: Commit**

Run: `git add main/input_service.c main/input_service.h main/board_ui.c main/main.c && git commit -m "feat: add safe touch capture control"`

### Task 5: Add Wi-Fi scan and connection status

**Files:**
- Create: `main/network_service.h`, `main/network_service.c`
- Modify: `main/Kconfig.projbuild`, `main/board_ui.h`, `main/board_ui.c`, `main/main.c`
- Test: scan output and Network page

**Interfaces:** Produces `network_service_start`, `network_service_request_scan`, and `network_service_connect`; consumes `CONFIG_KALUGA_WIFI_SSID`, `CONFIG_KALUGA_WIFI_PASSWORD`, and `board_ui_set_networks(const wifi_ap_record_t *, size_t)`.

- [ ] **Step 1: Add menuconfig-only credentials**

```kconfig
menu "Kaluga demo configuration"
config KALUGA_WIFI_SSID
    string "Wi-Fi SSID"
    default ""
config KALUGA_WIFI_PASSWORD
    string "Wi-Fi password"
    default ""
endmenu
```

- [ ] **Step 2: Implement bounded scan**

```c
esp_err_t network_service_request_scan(void) { network_request_t request = { .kind = NETWORK_REQUEST_SCAN }; return xQueueSend(s_request_queue, &request, 0) == pdPASS ? ESP_OK : ESP_ERR_TIMEOUT; }
```

Initialise NVS, `esp_netif`, the event loop, and STA Wi-Fi once. Scan at most 20 APs, sort by RSSI, copy records before publishing them, and display hidden SSIDs as `<hidden>`.

- [ ] **Step 3: Implement connection without logging secrets**

```c
strlcpy((char *)config.sta.ssid, CONFIG_KALUGA_WIFI_SSID, sizeof(config.sta.ssid)); strlcpy((char *)config.sta.password, CONFIG_KALUGA_WIFI_PASSWORD, sizeof(config.sta.password));
```

Render `Connecting`, `Connected`, or `Connection failed`; do not print passwords.

- [ ] **Step 4: Build and hardware verify**

Run: `. /home/ubuntu24/esp/esp-idf/export.sh && idf.py build`

Expected: PASS; empty SSID disables Connect but Scan is available.

Run: configure credentials with `idf.py menuconfig`, flash, and open Network.

Expected: nearby APs list without affecting Camera.

- [ ] **Step 5: Commit**

Run: `git add main/network_service.c main/network_service.h main/Kconfig.projbuild main/board_ui.c main/board_ui.h main/main.c && git commit -m "feat: add Wi-Fi scan and status UI"`

### Task 6: Document setup and run a clean build

**Files:**
- Create: `README.md`
- Test: clean build

**Interfaces:** Produces instructions for physical setup, build, flash, and operation.

- [ ] **Step 1: Document physical setup and hardware limits**

Describe stack order, separate TouchA and CAM FPC connections, camera-first audio exclusion, CAM/8311A pre-flash disconnection, Record as the capture button, and the five disabled TouchA signals.

- [ ] **Step 2: Document build and flash commands**

```bash
. /home/ubuntu24/esp/esp-idf/export.sh && idf.py set-target esp32s2 && idf.py build && idf.py -p /dev/ttyUSB0 flash monitor
```

State that the user replaces the example port with WSL’s detected serial port.

- [ ] **Step 3: Run clean build**

Run: `rm -rf build && . /home/ubuntu24/esp/esp-idf/export.sh && idf.py set-target esp32s2 build size`

Expected: PASS; binary exists and size report is included in handoff.

- [ ] **Step 4: Commit**

Run: `git add README.md && git commit -m "docs: explain Kaluga camera demo setup"`

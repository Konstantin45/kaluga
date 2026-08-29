# Camera RGB Channel Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Correct red and blue channel order in OV2640 RGB565 photographs without changing LVGL interface colors.

**Architecture:** Transform each captured RGB565 pixel while copying the camera frame into the existing PSRAM-backed LVGL image buffer. Preserve RGB565 byte order handling and all LCD settings.

**Tech Stack:** ESP-IDF 6.1, esp32-camera 2.1.7, LVGL 9.5, ESP32-S2 Kaluga BSP 5.0.1~1.

## Global Constraints

- Do not alter the LCD panel color order because the LVGL interface colors are already correct.
- Do not allocate another frame buffer.
- Keep the camera and LVGL buffers in RGB565 format.

---

### Task 1: Correct Camera Pixel Channels

**Files:**
- Modify: `main/board_ui.c`

**Interfaces:**
- Consumes: `board_ui_show_camera_frame(const uint8_t *pixels, size_t length, uint16_t width, uint16_t height)` and its existing PSRAM frame buffer.
- Produces: An LVGL RGB565 image in which red and blue match the photographed scene.

- [ ] **Step 1: Add a focused RGB565 channel-swap helper**

```c
static inline uint16_t rgb565_swap_red_blue(uint16_t pixel)
{
    return (uint16_t)(((pixel & 0x001fU) << 11) |
                      (pixel & 0x07e0U) |
                      ((pixel & 0xf800U) >> 11));
}
```

- [ ] **Step 2: Replace the raw frame copy with a single conversion pass**

Read each camera pixel from its two-byte wire order, apply `rgb565_swap_red_blue`, and store it in the same byte order consumed by `LV_COLOR_FORMAT_RGB565_SWAPPED`. Do not create another allocation.

- [ ] **Step 3: Build the firmware**

Run `idf.py build` in the initialized ESP-IDF 6.1 environment. Expected: `Project build complete` and an application image within the configured partition.

- [ ] **Step 4: Flash and verify on hardware**

Run `idf.py -p /dev/ttyUSB1 -b 460800 app-flash`. Expected: written-data hash verifies. Capture a known red object; expected: it remains red, while the orange Capture button remains orange.

- [ ] **Step 5: Commit and push**

Commit `main/board_ui.c`, the design document, and this implementation plan, then push the current main branch to the configured origin.

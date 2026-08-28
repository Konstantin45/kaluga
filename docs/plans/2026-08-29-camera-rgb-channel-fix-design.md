# Camera RGB Channel Fix Design

## Problem

The OV2640 camera frame is structurally correct, but red and blue are exchanged in the camera image. The LVGL interface colors are correct, so the LCD panel configuration must remain unchanged.

## Design

Keep the camera in QVGA RGB565 mode and keep the display in its current RGB565 mode. While copying the captured frame into the existing PSRAM-backed LVGL image buffer, decode each 16-bit pixel, exchange the five-bit red and blue fields, preserve the six-bit green field, and write the corrected pixel back in the byte order expected by LVGL.

This reuses the copy pass and existing buffer, requires no additional allocation, and affects camera frames only. The UI and display driver remain untouched.

## Verification

Build and flash the application, capture a scene containing a known red object, and verify that red remains red while the existing orange Capture button remains unchanged. Then commit and push the verified firmware change.

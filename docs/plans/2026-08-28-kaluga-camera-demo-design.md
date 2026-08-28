# ESP32-S2-Kaluga-1 Camera Demo Design

## Goal

Build a demonstrable ESP-IDF firmware for the ESP32-S2-Kaluga-1 v1.2 that
captures a still image from the ESP-LyraP-CAM v1.0 and renders it on the
ESP-LyraP-LCD32 v1.1.  The LCD is the primary interface; Wi-Fi scanning and
connection status are also demonstrated.  Audio is deliberately not started
in this first camera-focused build.

## Hardware and constraints

The target kit is composed of the Kaluga-1 v1.2 main board, LCD32 v1.1,
TouchA v1.1, CAM v1.0, and 8311A v1.2 audio board.

- The ESP32-S2 has one I2S peripheral shared by the camera and audio board;
  camera mode does not initialise the audio stack.
- In the camera + LCD + TouchA combination, TouchA GPIO 1, 2, 3, 6, and 11
  are multiplexed with the camera or LCD.  The firmware only uses the
  remaining non-conflicting touch input and describes the limitation in the
  UI.
- The camera board is disconnected while flashing because this hardware
  revision can interfere with boot strapping pins.  It is reconnected after
  flashing and power cycling.

## Architecture

`app_main` brings up NVS, Wi-Fi, the Kaluga board-support package, LCD, and
LVGL.  It then creates three small modules:

1. **Camera service**: owns esp32-camera initialisation, captures one JPEG
   frame on request, converts or decodes it for the LCD, releases the camera
   frame buffer promptly, and reports errors to the UI.
2. **UI service**: owns LVGL widgets and events.  Its Camera page contains a
   live status area, the last captured frame, and a capture action.  Its
   Network page scans Wi-Fi and shows available SSIDs and connection state.
3. **Input service**: maps only the electrically safe TouchA input to capture
   and offers the same capture action through the UI.  It does not configure
   any conflicting pins.

The UI posts capture and scan requests to worker tasks through FreeRTOS
queues.  Those workers update the UI only through an LVGL-safe handoff, so
camera DMA and network scans never run inside an input callback.

## Dependency strategy

The source project owns its dependencies in `main/idf_component.yml`.  The
ESP-IDF Component Manager resolves them into `managed_components/` during
the first build; nothing is manually copied into the shared ESP-IDF
installation.  Expected components are the Kaluga board-support package,
the ESP32 camera driver, and LVGL support supplied by the BSP.  Built-in
ESP-IDF provides Wi-Fi, NVS, FreeRTOS, and display primitives.

## Error handling and demonstration behavior

Camera start, frame capture, JPEG decode, Wi-Fi scan, and Wi-Fi association
all expose a short on-screen status.  A failed camera startup does not reset
the board: the Camera page remains available with diagnostic text.  A scan
can be retried.  Wi-Fi credentials are entered through `menuconfig` and are
not stored in source control.

## Verification

1. Build for `esp32s2` with ESP-IDF 6.1.
2. Flash with CAM v1.0 and 8311A v1.2 disconnected, then reconnect the CAM
   board and power cycle.
3. Confirm a capture action displays a fresh frame on LCD.
4. Confirm the safe TouchA action requests capture without spurious input.
5. Confirm Wi-Fi scan lists SSIDs and reports connection success or failure.
6. Confirm no audio driver is initialised in the serial log.

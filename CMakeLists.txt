cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Build only main and its dependency graph. This avoids compiling unrelated
# ESP-IDF subsystems and the LVGL example catalogue.
idf_build_set_property(MINIMAL_BUILD ON)

# Kaluga BSP 5.0.1 currently pins esp_codec_dev 1.3.x.  That component still
# uses the pre-IDF-6 driver include layout, so expose the split driver headers
# while the BSP is built against ESP-IDF 6.1.
include_directories(
    $ENV{IDF_PATH}/components/esp_driver_gpio/include
    $ENV{IDF_PATH}/components/esp_driver_i2c/include
    $ENV{IDF_PATH}/components/esp_driver_i2s/include
    $ENV{IDF_PATH}/components/esp_driver_spi/include
    $ENV{IDF_PATH}/components/esp_hal_gpspi/include
    $ENV{IDF_PATH}/components/esp_hal_gpspi/esp32s2/include
    $ENV{IDF_PATH}/components/esp_hal_i2s/include
    $ENV{IDF_PATH}/components/driver/i2c/include
    $ENV{IDF_PATH}/components/driver/i2s/include
)

project(kaluga_camera_demo)

# Compatibility is kept local to the two legacy managed components. Applying
# these headers globally would change ESP-IDF's own libc compilation.
idf_component_get_property(kaluga_bsp espressif__esp32_s2_kaluga_kit COMPONENT_LIB)
target_compile_options(${kaluga_bsp} PRIVATE
    "SHELL:-include ${CMAKE_CURRENT_LIST_DIR}/compat/kaluga_idf6_compat.h"
)

idf_component_get_property(codec_dev espressif__esp_codec_dev COMPONENT_LIB)
target_compile_options(${codec_dev} PRIVATE "SHELL:-include esp_idf_version.h")

# The BSP archive contains the legacy touch API, while this application only
# initializes ESP-IDF 6's new touch controller. Skip the link-time coexistence
# guard for this known-safe combination; both drivers are never initialized.
idf_component_get_property(legacy_driver driver COMPONENT_LIB)
target_compile_definitions(${legacy_driver} PRIVATE CONFIG_TOUCH_SKIP_LEGACY_CONFLICT_CHECK=1)

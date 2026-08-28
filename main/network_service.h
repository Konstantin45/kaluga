#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_wifi_types_generic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NETWORK_SERVICE_MAX_APS 16

typedef struct {
    char ssid[33];
    int8_t rssi;
    wifi_auth_mode_t authmode;
} network_service_ap_t;

typedef enum {
    NETWORK_SERVICE_EVENT_READY,
    NETWORK_SERVICE_EVENT_SCANNING,
    NETWORK_SERVICE_EVENT_SCAN_DONE,
    NETWORK_SERVICE_EVENT_CONNECTING,
    NETWORK_SERVICE_EVENT_CONNECTED,
    NETWORK_SERVICE_EVENT_DISCONNECTED,
    NETWORK_SERVICE_EVENT_ERROR,
} network_service_event_kind_t;

typedef struct {
    network_service_event_kind_t kind;
    const network_service_ap_t *aps;
    size_t ap_count;
    const char *message;
} network_service_event_t;

typedef void (*network_service_event_cb_t)(const network_service_event_t *event,
                                           void *context);

esp_err_t network_service_start(network_service_event_cb_t callback, void *context);
esp_err_t network_service_request_scan(void);
esp_err_t network_service_connect_configured(void);

#ifdef __cplusplus
}
#endif

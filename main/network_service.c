#include "network_service.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

typedef enum {
    NETWORK_REQUEST_SCAN,
    NETWORK_REQUEST_CONNECT,
} network_request_kind_t;

static const char *TAG = "network_service";
static QueueHandle_t s_request_queue;
static network_service_event_cb_t s_callback;
static void *s_callback_context;
static network_service_ap_t s_aps[NETWORK_SERVICE_MAX_APS];
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static bool s_started;

static void publish(network_service_event_kind_t kind, const char *message,
                    const network_service_ap_t *aps, size_t ap_count)
{
    if (s_callback == NULL) {
        return;
    }
    const network_service_event_t event = {
        .kind = kind,
        .aps = aps,
        .ap_count = ap_count,
        .message = message,
    };
    s_callback(&event, s_callback_context);
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id,
                               void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        publish(NETWORK_SERVICE_EVENT_DISCONNECTED, "Wi-Fi disconnected", NULL, 0);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        publish(NETWORK_SERVICE_EVENT_CONNECTED, "Wi-Fi connected", NULL, 0);
    }
}

static int compare_ap_rssi(const void *left, const void *right)
{
    const network_service_ap_t *a = left;
    const network_service_ap_t *b = right;
    return (int)b->rssi - (int)a->rssi;
}

static esp_err_t perform_scan(void)
{
    publish(NETWORK_SERVICE_EVENT_SCANNING, "Scanning Wi-Fi networks", NULL, 0);
    wifi_scan_config_t config = {
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    esp_err_t err = esp_wifi_scan_start(&config, true);
    if (err != ESP_OK) {
        publish(NETWORK_SERVICE_EVENT_ERROR, "Wi-Fi scan failed", NULL, 0);
        return err;
    }

    uint16_t count = NETWORK_SERVICE_MAX_APS;
    wifi_ap_record_t records[NETWORK_SERVICE_MAX_APS] = {0};
    err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        publish(NETWORK_SERVICE_EVENT_ERROR, "Cannot read Wi-Fi scan", NULL, 0);
        return err;
    }

    for (uint16_t i = 0; i < count; ++i) {
        const char *ssid = records[i].ssid[0] == '\0' ? "<hidden>" : (const char *)records[i].ssid;
        strlcpy(s_aps[i].ssid, ssid, sizeof(s_aps[i].ssid));
        s_aps[i].rssi = records[i].rssi;
        s_aps[i].authmode = records[i].authmode;
    }
    qsort(s_aps, count, sizeof(s_aps[0]), compare_ap_rssi);
    publish(NETWORK_SERVICE_EVENT_SCAN_DONE, "Wi-Fi scan complete", s_aps, count);
    return ESP_OK;
}

static esp_err_t perform_connect(void)
{
#if !CONFIG_KALUGA_WIFI_CONNECT_ENABLED
    publish(NETWORK_SERVICE_EVENT_ERROR,
            "Set Wi-Fi credentials in menuconfig", NULL, 0);
    return ESP_ERR_NOT_SUPPORTED;
#else
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, CONFIG_KALUGA_WIFI_SSID,
            sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, CONFIG_KALUGA_WIFI_PASSWORD,
            sizeof(config.sta.password));
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;

    publish(NETWORK_SERVICE_EVENT_CONNECTING, "Connecting to Wi-Fi", NULL, 0);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err == ESP_OK) {
        err = esp_wifi_connect();
    }
    if (err != ESP_OK) {
        publish(NETWORK_SERVICE_EVENT_ERROR, "Wi-Fi connection failed", NULL, 0);
    }
    return err;
#endif
}

static void network_task(void *argument)
{
    (void)argument;
    network_request_kind_t request;
    while (xQueueReceive(s_request_queue, &request, portMAX_DELAY) == pdTRUE) {
        if (request == NETWORK_REQUEST_SCAN) {
            ESP_LOGI(TAG, "Starting Wi-Fi scan");
            perform_scan();
        } else {
            ESP_LOGI(TAG, "Connecting using menuconfig credentials");
            perform_connect();
        }
    }
}

esp_err_t network_service_start(network_service_event_cb_t callback, void *context)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }
    if ((err = esp_netif_init()) != ESP_OK ||
        (err = esp_event_loop_create_default()) != ESP_OK) {
        return err;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&init)) != ESP_OK ||
        (err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   wifi_event_handler, NULL,
                                                   &s_wifi_handler)) != ESP_OK ||
        (err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                   wifi_event_handler, NULL,
                                                   &s_ip_handler)) != ESP_OK ||
        (err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK ||
        (err = esp_wifi_start()) != ESP_OK) {
        return err;
    }

    s_request_queue = xQueueCreate(2, sizeof(network_request_kind_t));
    if (s_request_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_callback = callback;
    s_callback_context = context;
    if (xTaskCreate(network_task, "network", 4096, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(s_request_queue);
        s_request_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    publish(NETWORK_SERVICE_EVENT_READY, "Wi-Fi ready", NULL, 0);
    return ESP_OK;
}

static esp_err_t enqueue(network_request_kind_t request)
{
    if (!s_started || s_request_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueSend(s_request_queue, &request, 0) == pdPASS
        ? ESP_OK
        : ESP_ERR_TIMEOUT;
}

esp_err_t network_service_request_scan(void)
{
    return enqueue(NETWORK_REQUEST_SCAN);
}

esp_err_t network_service_connect_configured(void)
{
    return enqueue(NETWORK_REQUEST_CONNECT);
}

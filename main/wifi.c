#include "wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <string.h>

#define WIFI_CONNECT_RETRY_ATTEMPT     5
#define WIFI_GROUP_EVENT_BIT_CONNECTED BIT0
#define WIFI_GROUP_EVENT_BIT_FAIL      BIT1

struct type_info_t
{
    char* type;
    char* info;
};

struct wifi_t
{
    EventGroupHandle_t wifi_group_events;
    esp_netif_t*       esp_netif;
    uint32_t           retry_count;
    bool               initialized;
};

static struct wifi_t wifi;

struct type_info_t esp_wifi_event_type_description[WIFI_EVENT_MAX] = {

    [WIFI_EVENT_WIFI_READY]          = {"WIFI_EVENT_WIFI_READY", "ESP32 WiFi ready"},
    [WIFI_EVENT_SCAN_DONE]           = {"WIFI_EVENT_SCAN_DONE", "ESP32 finish scanning AP"},
    [WIFI_EVENT_STA_START]           = {"WIFI_EVENT_STA_START", "ESP32 station start"},
    [WIFI_EVENT_STA_STOP]            = {"WIFI_EVENT_STA_STOP", "ESP32 station stop"},
    [WIFI_EVENT_STA_CONNECTED]       = {"WIFI_EVENT_STA_CONNECTED", "ESP32 station connected to AP"},
    [WIFI_EVENT_STA_DISCONNECTED]    = {"WIFI_EVENT_STA_DISCONNECTED", "ESP32 station disconnected from AP"},
    [WIFI_EVENT_STA_AUTHMODE_CHANGE] = {"WIFI_EVENT_STA_AUTHMODE_CHANGE",
                                        "the auth mode of AP connected by ESP32 station changed"},
    [WIFI_EVENT_STA_WPS_ER_SUCCESS]  = {"WIFI_EVENT_STA_WPS_ER_SUCCESS", "ESP32 station wps succeeds in enrollee mode"},
    [WIFI_EVENT_STA_WPS_ER_FAILED]   = {"WIFI_EVENT_STA_WPS_ER_FAILED", "ESP32 station wps fails in enrollee mode"},
    [WIFI_EVENT_STA_WPS_ER_TIMEOUT]  = {"WIFI_EVENT_STA_WPS_ER_TIMEOUT", "ESP32 station wps timeout in enrollee mode"},
    [WIFI_EVENT_STA_WPS_ER_PIN]      = {"WIFI_EVENT_STA_WPS_ER_PIN", "ESP32 station wps pin code in enrollee mode"},
    [WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP] = {"WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP",
                                           "ESP32 station wps overlap in enrollee mode"},
    [WIFI_EVENT_AP_START]               = {"WIFI_EVENT_AP_START", "ESP32 soft-AP start"},
    [WIFI_EVENT_AP_STOP]                = {"WIFI_EVENT_AP_STOP", "ESP32 soft-AP stop"},
    [WIFI_EVENT_AP_STACONNECTED]        = {"WIFI_EVENT_AP_STACONNECTED", "a station connected to ESP32 soft-AP"},
    [WIFI_EVENT_AP_STADISCONNECTED] = {"WIFI_EVENT_AP_STADISCONNECTED", "a station disconnected from ESP32 soft-AP"},
    [WIFI_EVENT_AP_PROBEREQRECVED]  = {"WIFI_EVENT_AP_PROBEREQRECVED",
                                      "Receive probe request packet in soft-AP interface"},
    [WIFI_EVENT_FTM_REPORT]         = {"WIFI_EVENT_FTM_REPORT", "Receive report of FTM procedure"},
    [WIFI_EVENT_STA_BSS_RSSI_LOW]   = {"WIFI_EVENT_STA_BSS_RSSI_LOW", "AP's RSSI crossed configured threshold"},
    [WIFI_EVENT_ACTION_TX_STATUS]   = {"WIFI_EVENT_ACTION_TX_STATUS", "Status indication of Action Tx operation"},
    [WIFI_EVENT_ROC_DONE]           = {"WIFI_EVENT_ROC_DONE", "Remain-on-Channel operation complete"},
    [WIFI_EVENT_STA_BEACON_TIMEOUT] = {"WIFI_EVENT_STA_BEACON_TIMEOUT", "ESP32 station beacon timeout"},
};

static void wifi_event_any_id_cb(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id,
                                 void* event_data);
static void ip_event_sta_got_ip_cb(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id,
                                   void* event_data);

void wifi_init(void)
{
    if (wifi.initialized == true)
    {
        return;
    }

    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_init_config);

    esp_wifi_set_storage(WIFI_STORAGE_FLASH);

    wifi.initialized = true;
}

void wifi_scan(void)
{
    return;
}

wifi_err_t wifi_connect_sta(char* ssid, char* password)
{

    if (wifi.initialized == false)
    {
        return WIFI_ERROR;
    }

    wifi.wifi_group_events = xEventGroupCreate();

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_any_id_cb, NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_sta_got_ip_cb, NULL,
                                                        &instance_got_ip));

    wifi.esp_netif = esp_netif_create_default_wifi_sta();

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));

    strncpy((char*)&wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)&wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    /* Setting a password implies station will connect to all security modes including WEP/WPA.
     * However these modes are deprecated and not advisable to be used. Incase your Access point
     * doesn't support WPA2, these mode can be enabled by commenting below line */
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    wifi_config.sta.pmf_cfg.capable  = true;
    wifi_config.sta.pmf_cfg.required = false;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    wifi.retry_count = 0;

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits =
        xEventGroupWaitBits(wifi.wifi_group_events, WIFI_GROUP_EVENT_BIT_CONNECTED | WIFI_GROUP_EVENT_BIT_FAIL, pdTRUE,
                            pdFALSE, portMAX_DELAY);

    wifi_err_t wifi_err;
    if (bits & WIFI_GROUP_EVENT_BIT_CONNECTED)
    {
        ESP_LOGI(__FUNCTION__, "connected to ap SSID:%s password:%s", ssid, password);
        wifi_err = WIFI_OK;
    }
    else
    {
        ESP_LOGE(__FUNCTION__, "Failed to connect to SSID:%s, password:%s", ssid, password);
        wifi_err = WIFI_ERROR;
    }

    /* The event will not be processed after unregister */
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip);
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id);
    vEventGroupDelete(wifi.wifi_group_events);

    return wifi_err;
}

void wifi_disconnect(void)
{
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_netif_destroy(wifi.esp_netif);
    return;
}

static void wifi_event_any_id_cb(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id,
                                 void* event_data)
{
    if (event_id >= WIFI_EVENT_MAX)
    {
        return;
    }

    ESP_LOGI(__FUNCTION__, "%s - %s", esp_wifi_event_type_description[event_id].type,
             esp_wifi_event_type_description[event_id].info);

    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        break;

    case WIFI_EVENT_STA_DISCONNECTED:
        if (wifi.retry_count < WIFI_CONNECT_RETRY_ATTEMPT)
        {
            wifi.retry_count++;
            esp_wifi_connect();
            ESP_LOGI(__FUNCTION__, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(wifi.wifi_group_events, WIFI_GROUP_EVENT_BIT_FAIL);
        }
        break;
    default:
        break;
    };
    return;
}

static void ip_event_sta_got_ip_cb(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id,
                                   void* event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(__FUNCTION__, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi.wifi_group_events, WIFI_GROUP_EVENT_BIT_CONNECTED);
    }
    return;
}
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "wifi.h"
#include <stdio.h>
void task_wifi_connect(void* params);

void app_main(void)
{
    nvs_flash_init();
    wifi_init();
    wifi_connect_ap("esp32-wifi", "password");

    for (int i = 20; i > 0; i--)
    {
        vTaskDelay(100);
        ESP_LOGI(__FUNCTION__, "disconnecting wifi in %d seconds", i);
    }

    wifi_disconnect();

    ESP_LOGI(__FUNCTION__, "reconnecting wifi ap mode");

    wifi_connect_ap("esp32-wifi", "password");
}

void task_wifi_connect(void* params)
{
    wifi_init();

    while (1)
    {
        wifi_err_t wifi_err = wifi_connect_sta("myya", "Qu@!z@r15068");

        if (wifi_err != WIFI_OK)
        {
            ESP_LOGE(__FUNCTION__, "fail to connect to station");
            vTaskDelete(NULL);
        }

        ESP_LOGI(__FUNCTION__, "disconnecting wifi in 5 seconds");
        for (int i = 0; i < 5; i++)
        {
            vTaskDelay(100);
        }

        wifi_disconnect();
    }
}
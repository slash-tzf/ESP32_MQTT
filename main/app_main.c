/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "usbh_modem_wifi.h"
#include "led.h"
#include "mqtt.h"
#include "modem_4g.h"
#include "gps.h"
#include "sensors.h"
#include "data_model.h"
#include "time_sync.h"

#ifdef CONFIG_EXAMPLE_ENABLE_WEB_ROUTER
    #include "modem_http_config.h"
#endif
static const char *TAG = "app_main";
static modem_wifi_config_t s_modem_wifi_config = MODEM_WIFI_DEFAULT_CONFIG();


// 数据汇总任务，定期将所有数据整合发送
static void data_publish_task(void *pvParameter)
{
    esp_mqtt_client_handle_t mqtt_client = (esp_mqtt_client_handle_t)pvParameter;
    data_model_t *data_model = data_model_get_latest();
    
    while (1) {
        if (mqtt_client != NULL && data_model != NULL) {
            // 发布完整数据模型
            mqtt_publish_data_model(mqtt_client, data_model, NULL);
            ESP_LOGI(TAG, "已发布完整数据模型到MQTT");
        }
        
        // 每30秒发布一次完整数据
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

void app_main(void)
{
    /* Initialize led indicator */
    _led_indicator_init();
    
    /* Initialize NVS for Wi-Fi storage */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        /* NVS partition was truncated and needs to be erased
         * Retry nvs_flash_init */
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* Initialize default TCP/IP stack */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 初始化4G模块
    modem_4g_init();
    
#ifdef CONFIG_EXAMPLE_ENABLE_WEB_ROUTER
    modem_http_get_nvs_wifi_config(&s_modem_wifi_config);
    modem_http_init(&s_modem_wifi_config);
#endif
    esp_netif_t *ap_netif = modem_wifi_ap_init();
    assert(ap_netif != NULL);
    ESP_ERROR_CHECK(modem_wifi_set(&s_modem_wifi_config));

    // 初始化时间同步
    time_sync_init();

    // 启动MQTT客户端
    esp_mqtt_client_handle_t mqtt_client = mqtt_app_start();
    
    // 初始化数据模型
    data_model_t *model = NULL;
    model = data_model_get_latest();
    ESP_ERROR_CHECK(data_model_init(model));

    // 初始化传感器
    ESP_ERROR_CHECK(sensors_init());

    // 启动GPS模块
    ESP_ERROR_CHECK(gps_start());

    // 创建传感器任务
    ESP_ERROR_CHECK(sensors_task_init());
    
    // 创建数据发布任务
    xTaskCreate(data_publish_task, "data_publish", 8192, mqtt_client, 5, NULL);
}

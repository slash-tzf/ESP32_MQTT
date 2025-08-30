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
#include "freertos/event_groups.h"
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
#include "network_manager.h"
#include "wifi_manager.h"
#include "ota.h"
#include "modem_http_config.h"

static const char *TAG = "app_main";
static modem_wifi_config_t wifi_AP_config = MODEM_WIFI_DEFAULT_CONFIG();


void app_main(void)
{
    // 系统检查
    run_diagnostic();
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

    // 初始化网络管理器
    ESP_ERROR_CHECK(network_manager_init());
    
    // 初始化按钮处理
    ESP_ERROR_CHECK(network_manager_init_button());

    network_mode_t current_mode = network_manager_get_mode();

    if (current_mode == NETWORK_MODE_4G) {
        // 初始化4G模块
        
        esp_netif_t *ap_netif = modem_wifi_ap_init();
        assert(ap_netif != NULL);
        ESP_ERROR_CHECK(modem_wifi_set(&wifi_AP_config));
        modem_http_init(&wifi_AP_config);
        modem_4g_init();
    } else if (current_mode == NETWORK_MODE_WIFI_STA_AP) {
        // 初始化WiFi STA AP模式
        wifi_apsta_init(&wifi_AP_config);
        modem_http_init(&wifi_AP_config);
    }

    // 初始化时间同步
    time_sync_init();

    // 初始化数据模型
    data_model_t *model = NULL;
    model = data_model_get_latest();
    ESP_ERROR_CHECK(data_model_init(model));

    // 初始化传感器
    //ESP_ERROR_CHECK(sensors_init());
    sensors_init();

    // 启动GPS模块
    ESP_ERROR_CHECK(gps_start());

    // 创建传感器任务
    ESP_ERROR_CHECK(sensors_task_init());
    
    // 启动MQTT客户端
    mqtt_app_start();
}

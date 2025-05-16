#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_netif.h"
#include "usbh_modem_wifi.h"

/**
 * @brief 初始化WiFi APSTA模式
 * 
 * 初始化并配置ESP32为APSTA模式(SoftAP+STA)，SoftAP提供局域网接入，
 * STA连接到外部WiFi网络，并通过NAPT提供网络共享
 * 
 * @param wifi_AP_config SoftAP配置参数
 * @return esp_err_t ESP_OK表示成功，否则表示错误
 */
esp_err_t wifi_apsta_init(modem_wifi_config_t *wifi_AP_config);

/**
 * @brief 配置并连接到外部STA WiFi网络
 * 
 * @param ssid SSID
 * @param password 密码
 * @return esp_err_t ESP_OK表示成功，否则表示错误
 */
esp_err_t wifi_connect_sta(const char *ssid, const char *password);

/**
 * @brief 断开并停止WiFi
 * 
 * @return esp_err_t ESP_OK表示成功，否则表示错误
 */
esp_err_t wifi_stop();

#endif /* WIFI_MANAGER_H */ 
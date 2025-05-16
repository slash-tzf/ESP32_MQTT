#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "esp_err.h"

// 定义网络模式
typedef enum {
    NETWORK_MODE_4G = 0,          // 使用4G网络
    NETWORK_MODE_WIFI_STA_AP,     // 使用WiFi STA连接
} network_mode_t;

/**
 * @brief 初始化网络管理模块
 * 
 * @return esp_err_t 
 */
esp_err_t network_manager_init();

/**
 * @brief 切换网络模式
 * 
 * @param mode 要切换到的网络模式
 * @return esp_err_t 
 */
esp_err_t network_manager_set_mode(network_mode_t mode);

/**
 * @brief 获取当前网络模式
 * 
 * @return network_mode_t 当前网络模式
 */
network_mode_t network_manager_get_mode();

/**
 * @brief 设置WiFi连接参数
 * 
 * @param ssid WiFi SSID
 * @param password WiFi密码
 * @return esp_err_t 
 */
esp_err_t network_manager_set_wifi_config(const char *ssid, const char *password);

/**
 * @brief 初始化按钮检测，用于双击切换网络模式
 * 
 * @return esp_err_t 
 */
esp_err_t network_manager_init_button();

#endif /* NETWORK_MANAGER_H */ 
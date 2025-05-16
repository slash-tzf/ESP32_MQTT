#ifndef MODEM_4G_H
#define MODEM_4G_H

#include "esp_err.h"

/**
 * @brief 初始化4G模块
 * 
 */
void modem_4g_init();

/**
 * @brief 断开4G模块网络连接
 * 
 * @return esp_err_t 
 */
esp_err_t modem_4g_disconnect();

/**
 * @brief 重新连接4G模块网络
 * 
 * @return esp_err_t 
 */
esp_err_t modem_4g_connect();

#endif
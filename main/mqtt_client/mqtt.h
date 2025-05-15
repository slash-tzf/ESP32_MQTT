#ifndef MQTT_H
#define MQTT_H

#include <stdio.h>
#include "mqtt_client.h"
#include "data_model.h"

/**
 * @brief 初始化MQTT客户端并启动
 * 
 * @return esp_mqtt_client_handle_t MQTT客户端句柄
 */
esp_mqtt_client_handle_t mqtt_app_start(void);

/**
 * @brief 发布数据模型到MQTT主题
 * 
 * @param client MQTT客户端句柄
 * @param model 数据模型指针
 * @param topic 主题名称，如果为NULL则使用默认主题
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t mqtt_publish_data_model(esp_mqtt_client_handle_t client, 
                                  const data_model_t *model, 
                                  const char *topic);

/**
 * @brief 发布传感器数据到MQTT主题
 * 
 * @param client MQTT客户端句柄
 * @param sensor_data 传感器数据指针
 * @param topic 主题名称，如果为NULL则使用默认主题
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t mqtt_publish_sensor_data(esp_mqtt_client_handle_t client, 
                                  const sensor_data_t *sensor_data, 
                                  const char *topic);

/**
 * @brief 发布GPS数据到MQTT主题
 * 
 * @param client MQTT客户端句柄
 * @param gps_data GPS数据指针
 * @param topic 主题名称，如果为NULL则使用默认主题
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t mqtt_publish_gps_data(esp_mqtt_client_handle_t client, 
                               const gps_data_t *gps_data, 
                               const char *topic);

/**
 * @brief 获取当前MQTT客户端句柄
 * 
 * @return esp_mqtt_client_handle_t MQTT客户端句柄
 */
esp_mqtt_client_handle_t mqtt_get_client(void);

#endif

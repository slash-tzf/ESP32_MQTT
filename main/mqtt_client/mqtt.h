#ifndef MQTT_H
#define MQTT_H

#include <stdio.h>
#include "mqtt_client.h"
#include "data_model.h"

#define MAX_MQTT_TOPICS         20
#define MAX_TOPIC_LENGTH        64

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

/**
 * @brief 重新连接MQTT客户端
 * 
 * 当网络状态变化时调用此函数，会先停止现有连接，然后重新初始化并启动连接
 * 
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t mqtt_reconnect(void);

/**
 * @brief 获取已订阅的MQTT主题列表
 * 
 * @param topics 主题列表存储缓冲区
 * @param max_topics 最大主题数量
 * @param topic_count 实际主题数量的存储指针
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t mqtt_get_subscribed_topics(char topics[][64], int max_topics, int *topic_count);

/**
 * @brief 订阅MQTT主题
 * 
 * @param topic 要订阅的主题
 * @param save_to_nvs 是否保存到NVS中
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t mqtt_subscribe_topic(const char *topic, bool save_to_nvs);

/**
 * @brief 取消订阅MQTT主题
 * 
 * @param topic 要取消订阅的主题
 * @param remove_from_nvs 是否从NVS中删除
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t mqtt_unsubscribe_topic(const char *topic, bool remove_from_nvs);

/**
 * @brief 发布消息到指定MQTT主题
 * 
 * @param topic 目标主题
 * @param message 消息内容
 * @param qos 服务质量 (0-最多一次, 1-至少一次, 2-只有一次)
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t mqtt_publish_message(const char *topic, const char *message, int qos);

/**
 * @brief 从NVS加载已保存的MQTT主题并订阅
 * 
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t mqtt_load_topics_from_nvs(void);

#endif

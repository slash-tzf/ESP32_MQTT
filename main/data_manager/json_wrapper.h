#ifndef JSON_WRAPPER_H
#define JSON_WRAPPER_H

#include "esp_err.h"
#include "data_model.h"
#include "json_generator.h"

/**
 * @brief 将数据模型转换为JSON字符串
 * 
 * @param model 数据模型指针
 * @param json_str 输出的JSON字符串
 * @param json_str_size JSON字符串缓冲区大小
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t json_generate_from_data_model(const data_model_t *model, char *json_str, size_t json_str_size);

/**
 * @brief 将传感器数据转换为JSON字符串
 * 
 * @param sensor_data 传感器数据指针
 * @param json_str 输出的JSON字符串
 * @param json_str_size JSON字符串缓冲区大小
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t json_generate_from_sensor_data(const sensor_data_t *sensor_data, char *json_str, size_t json_str_size);

/**
 * @brief 将GPS数据转换为JSON字符串
 * 
 * @param gps_data GPS数据指针
 * @param json_str 输出的JSON字符串
 * @param json_str_size JSON字符串缓冲区大小
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t json_generate_from_gps_data(const gps_data_t *gps_data, char *json_str, size_t json_str_size);

/**
 * @brief 将传感器和GPS数据封装成通用JSON格式
 * 
 * @param model 数据模型指针
 * @param jstr 已初始化的JSON生成器
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t json_add_data_to_generator(const data_model_t *model, json_gen_str_t *jstr);

#endif // JSON_WRAPPER_H 
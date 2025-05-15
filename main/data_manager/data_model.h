#ifndef DATA_MODEL_H
#define DATA_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

// 设备信息结构体
typedef struct {
    char device_id[32];        // 设备ID
    char firmware_version[16]; // 固件版本
} device_info_t;

// 传感器数据结构体
typedef struct {
    float temperature;         // 温度值，°C
    float humidity;            // 湿度值，%
    float light_intensity;     // 光照强度，lx
    bool sensors_valid;        // 传感器数据是否有效
} sensor_data_t;

// GPS数据结构体
typedef struct {
    double latitude;           // 纬度，十进制度格式
    double longitude;          // 经度，十进制度格式
    char ns_indicator;         // 北/南指示符: 'N'=北, 'S'=南
    char ew_indicator;         // 东/西指示符: 'E'=东, 'W'=西
    float altitude;            // 高度，单位为米
    float speed;               // 地面速度，单位为节
    float course;              // 航向，单位为度
    int data_source;           // 数据来源: 0=GNSS, 1=LBS
    bool gps_valid;            // GPS数据是否有效
} gps_data_t;

// 聚合的数据模型结构体
typedef struct {
    device_info_t device;      // 设备信息
    sensor_data_t sensors;     // 传感器数据
    gps_data_t gps;            // GPS数据
    time_t timestamp;          // 数据时间戳
} data_model_t;

/**
 * @brief 初始化数据模型
 * 
 * @param model 数据模型指针
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t data_model_init(data_model_t *model);

/**
 * @brief 更新传感器数据
 * 
 * @param model 数据模型指针
 * @param temperature 温度值
 * @param humidity 湿度值
 * @param light 光照强度
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t data_model_update_sensor_data(data_model_t *model, 
                                       float temperature, 
                                       float humidity, 
                                       float light);

/**
 * @brief 更新GPS数据
 * 
 * @param model 数据模型指针
 * @param gps_info GPS信息结构体指针
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t data_model_update_gps_data(data_model_t *model, void *gps_info);

/**
 * @brief 获取最新的数据模型
 * 
 * @return data_model_t* 数据模型指针，如果未初始化则返回NULL
 */
data_model_t* data_model_get_latest(void);

#endif // DATA_MODEL_H 
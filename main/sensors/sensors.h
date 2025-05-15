#pragma once

#include "esp_err.h"
#include "data_model.h"

// 初始化所有传感器
esp_err_t sensors_init(void);

// 读取光照强度
esp_err_t read_light_intensity(float *light);

// 读取温湿度
esp_err_t read_temperature_humidity(float *temperature, float *humidity);

// 更新数据模型中的传感器数据
esp_err_t sensors_update_data_model(data_model_t *model);

// 传感器任务
void sensors_task(void *pvParameters); 

// 传感器任务初始化
esp_err_t sensors_task_init(void);
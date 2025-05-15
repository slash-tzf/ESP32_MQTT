#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "json_wrapper.h"
#include "json_generator.h"

static const char *TAG = "json_wrapper";

esp_err_t json_generate_from_data_model(const data_model_t *model, char *json_str, size_t json_str_size)
{
    if (model == NULL || json_str == NULL || json_str_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 初始化JSON生成器
    json_gen_str_t jstr;
    json_gen_str_start(&jstr, json_str, json_str_size, NULL, NULL);
    
    // 开始JSON对象
    json_gen_start_object(&jstr);
    
    // 添加设备信息
    json_gen_push_object(&jstr, "device");
    json_gen_obj_set_string(&jstr, "id", model->device.device_id);
    json_gen_obj_set_string(&jstr, "version", model->device.firmware_version);
    json_gen_pop_object(&jstr);
    
    // 添加时间戳
    json_gen_obj_set_int(&jstr, "timestamp", model->timestamp);
    
    // 添加传感器数据
    if (model->sensors.sensors_valid) {
        json_gen_push_object(&jstr, "sensors");
        json_gen_obj_set_float(&jstr, "temperature", model->sensors.temperature);
        json_gen_obj_set_float(&jstr, "humidity", model->sensors.humidity);
        json_gen_obj_set_float(&jstr, "light", model->sensors.light_intensity);
        json_gen_pop_object(&jstr);
    }
    
    // 添加GPS数据
    if (model->gps.gps_valid) {
        json_gen_push_object(&jstr, "gps");
        json_gen_obj_set_float(&jstr, "latitude", model->gps.latitude);
        json_gen_obj_set_float(&jstr, "longitude", model->gps.longitude);
        
        // 添加方向指示符
        char lat_str[16], lon_str[16];
        snprintf(lat_str, sizeof(lat_str), "%.6f%c", model->gps.latitude, model->gps.ns_indicator);
        snprintf(lon_str, sizeof(lon_str), "%.6f%c", model->gps.longitude, model->gps.ew_indicator);
        json_gen_obj_set_string(&jstr, "lat_display", lat_str);
        json_gen_obj_set_string(&jstr, "lon_display", lon_str);
        
        // 其他GPS信息
        json_gen_obj_set_float(&jstr, "altitude", model->gps.altitude);
        json_gen_obj_set_float(&jstr, "speed", model->gps.speed);
        json_gen_obj_set_float(&jstr, "course", model->gps.course);
        json_gen_obj_set_int(&jstr, "source", model->gps.data_source);
        json_gen_pop_object(&jstr);
    }
    
    // 结束JSON对象
    json_gen_end_object(&jstr);
    json_gen_str_end(&jstr);
    
    return ESP_OK;
}

esp_err_t json_generate_from_sensor_data(const sensor_data_t *sensor_data, char *json_str, size_t json_str_size)
{
    if (sensor_data == NULL || json_str == NULL || json_str_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 初始化JSON生成器
    json_gen_str_t jstr;
    json_gen_str_start(&jstr, json_str, json_str_size, NULL, NULL);
    
    // 开始JSON对象
    json_gen_start_object(&jstr);
    
    // 添加传感器数据
    json_gen_obj_set_float(&jstr, "temperature", sensor_data->temperature);
    json_gen_obj_set_float(&jstr, "humidity", sensor_data->humidity);
    json_gen_obj_set_float(&jstr, "light", sensor_data->light_intensity);
    
    // 结束JSON对象
    json_gen_end_object(&jstr);
    json_gen_str_end(&jstr);
    
    return ESP_OK;
}

esp_err_t json_generate_from_gps_data(const gps_data_t *gps_data, char *json_str, size_t json_str_size)
{
    if (gps_data == NULL || json_str == NULL || json_str_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 初始化JSON生成器
    json_gen_str_t jstr;
    json_gen_str_start(&jstr, json_str, json_str_size, NULL, NULL);
    
    // 开始JSON对象
    json_gen_start_object(&jstr);
    
    // 添加GPS数据
    json_gen_obj_set_float(&jstr, "latitude", gps_data->latitude);
    json_gen_obj_set_float(&jstr, "longitude", gps_data->longitude);
    
    // 添加方向指示符
    char lat_str[16], lon_str[16];
    snprintf(lat_str, sizeof(lat_str), "%.6f%c", gps_data->latitude, gps_data->ns_indicator);
    snprintf(lon_str, sizeof(lon_str), "%.6f%c", gps_data->longitude, gps_data->ew_indicator);
    json_gen_obj_set_string(&jstr, "lat_display", lat_str);
    json_gen_obj_set_string(&jstr, "lon_display", lon_str);
    
    // 其他GPS信息
    json_gen_obj_set_float(&jstr, "altitude", gps_data->altitude);
    json_gen_obj_set_float(&jstr, "speed", gps_data->speed);
    json_gen_obj_set_float(&jstr, "course", gps_data->course);
    json_gen_obj_set_int(&jstr, "source", gps_data->data_source);
    
    // 结束JSON对象
    json_gen_end_object(&jstr);
    json_gen_str_end(&jstr);
    
    return ESP_OK;
}

esp_err_t json_add_data_to_generator(const data_model_t *model, json_gen_str_t *jstr)
{
    if (model == NULL || jstr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 添加设备信息
    json_gen_push_object(jstr, "device");
    json_gen_obj_set_string(jstr, "id", model->device.device_id);
    json_gen_obj_set_string(jstr, "version", model->device.firmware_version);
    json_gen_pop_object(jstr);
    
    // 添加时间戳
    json_gen_obj_set_int(jstr, "timestamp", model->timestamp);
    
    // 添加传感器数据
    if (model->sensors.sensors_valid) {
        json_gen_push_object(jstr, "sensors");
        json_gen_obj_set_float(jstr, "temperature", model->sensors.temperature);
        json_gen_obj_set_float(jstr, "humidity", model->sensors.humidity);
        json_gen_obj_set_float(jstr, "light", model->sensors.light_intensity);
        json_gen_pop_object(jstr);
    }
    
    // 添加GPS数据
    if (model->gps.gps_valid) {
        json_gen_push_object(jstr, "gps");
        json_gen_obj_set_float(jstr, "latitude", model->gps.latitude);
        json_gen_obj_set_float(jstr, "longitude", model->gps.longitude);
        
        // 添加方向指示符
        char lat_str[16], lon_str[16];
        snprintf(lat_str, sizeof(lat_str), "%.6f%c", model->gps.latitude, model->gps.ns_indicator);
        snprintf(lon_str, sizeof(lon_str), "%.6f%c", model->gps.longitude, model->gps.ew_indicator);
        json_gen_obj_set_string(jstr, "lat_display", lat_str);
        json_gen_obj_set_string(jstr, "lon_display", lon_str);
        
        // 其他GPS信息
        json_gen_obj_set_float(jstr, "altitude", model->gps.altitude);
        json_gen_obj_set_float(jstr, "speed", model->gps.speed);
        json_gen_obj_set_float(jstr, "course", model->gps.course);
        json_gen_obj_set_int(jstr, "source", model->gps.data_source);
        json_gen_pop_object(jstr);
    }
    
    return ESP_OK;
} 
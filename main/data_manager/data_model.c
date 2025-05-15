#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "data_model.h"
#include "gps.h"
#include "esp_mac.h"
static const char *TAG = "data_model";
static data_model_t s_data_model = {0};
static bool s_model_initialized = false;

esp_err_t data_model_init(data_model_t *model)
{
    if (model == NULL) {
        model = &s_data_model;
    }
    
    // 清空数据模型
    memset(model, 0, sizeof(data_model_t));
    
    // 设置设备信息
    uint8_t mac[6];
    char device_id[32];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_id, sizeof(device_id), "ESP32S3_%02X%02X%02X", mac[3], mac[4], mac[5]);
    strncpy(model->device.device_id, device_id, sizeof(model->device.device_id) - 1);
    strncpy(model->device.firmware_version, "1.0.0", sizeof(model->device.firmware_version) - 1);
    
    // 初始化数据有效性标志
    model->sensors.sensors_valid = false;
    model->gps.gps_valid = false;
    
    // 设置初始时间戳
    model->timestamp = 0;
    
    s_model_initialized = true;
    ESP_LOGI(TAG, "数据模型初始化完成，设备ID: %s", model->device.device_id);
    
    return ESP_OK;
}

esp_err_t data_model_update_sensor_data(data_model_t *model, 
                                      float temperature, 
                                      float humidity, 
                                      float light)
{
    if (!s_model_initialized) {
        ESP_LOGW(TAG, "数据模型未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (model == NULL) {
        model = &s_data_model;
    }
    
    // 更新传感器数据
    model->sensors.temperature = temperature;
    model->sensors.humidity = humidity;
    model->sensors.light_intensity = light;
    model->sensors.sensors_valid = true;
    
    // 更新时间戳
    model->timestamp = time(NULL);
    
    return ESP_OK;
}

esp_err_t data_model_update_gps_data(data_model_t *model, void *gps_info)
{
    if (!s_model_initialized) {
        ESP_LOGW(TAG, "数据模型未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (model == NULL) {
        model = &s_data_model;
    }
    
    if (gps_info == NULL) {
        ESP_LOGW(TAG, "GPS数据为空");
        return ESP_ERR_INVALID_ARG;
    }
    
    gps_info_t *info = (gps_info_t *)gps_info;
    
    // 检查GPS数据是否有效
    if (info->valid) {
        // 将经纬度从ddmm.mmmm格式转换为十进制度格式
        int lat_deg = (int)(info->latitude / 100.0);
        double lat_min = info->latitude - lat_deg * 100.0;
        model->gps.latitude = lat_deg + lat_min / 60.0;
        
        int lon_deg = (int)(info->longitude / 100.0);
        double lon_min = info->longitude - lon_deg * 100.0;
        model->gps.longitude = lon_deg + lon_min / 60.0;
        
        // 根据北南东西指示符调整经纬度正负值
        if (info->ns_indicator == 'S') {
            model->gps.latitude = -model->gps.latitude;
        }
        
        if (info->ew_indicator == 'W') {
            model->gps.longitude = -model->gps.longitude;
        }
        
        // 保存其他GPS数据
        model->gps.ns_indicator = info->ns_indicator;
        model->gps.ew_indicator = info->ew_indicator;
        model->gps.altitude = info->altitude;
        model->gps.speed = info->speed;
        model->gps.course = info->course;
        model->gps.data_source = info->data_source;
        model->gps.gps_valid = true;
        
        // 更新时间戳
        model->timestamp = time(NULL);
        
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "GPS数据无效");
        return ESP_ERR_INVALID_STATE;
    }
}

data_model_t* data_model_get_latest(void)
{
    if (!s_model_initialized) {
        ESP_LOGW(TAG, "数据模型未初始化");
        return NULL;
    }
    
    return &s_data_model;
} 
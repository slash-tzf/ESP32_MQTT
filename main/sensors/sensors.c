#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "bh1750.h"
#include "dht.h"
#include "sensors.h"
#include "data_model.h"
#include "mqtt.h"

static const char *TAG = "sensors";

// I2C配置
#define I2C_MASTER_SCL_IO           CONFIG_BH1750_I2C_SCL_PIN      // SCL引脚
#define I2C_MASTER_SDA_IO           CONFIG_BH1750_I2C_SDA_PIN      // SDA引脚
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          CONFIG_BH1750_I2C_FREQ_HZ  // I2C主频率

// DHT11配置
#define DHT11_GPIO_PIN              CONFIG_DHT11_GPIO_PIN       // DHT11数据引脚

// 传感器数据更新间隔(毫秒)
#define SENSOR_UPDATE_INTERVAL_MS   10000

static bh1750_handle_t bh1750_dev = NULL;

// 初始化I2C
static esp_err_t i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    esp_err_t ret = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (ret != ESP_OK) {
        return ret;
    }
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

// 初始化传感器
esp_err_t sensors_init(void)
{
    // 初始化I2C
    esp_err_t ret = i2c_master_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C初始化失败");
        return ret;
    }

    // 初始化BH1750
    bh1750_dev = bh1750_create(I2C_MASTER_NUM, BH1750_I2C_ADDRESS_DEFAULT);
    if (bh1750_dev == NULL) {
        ESP_LOGE(TAG, "BH1750初始化失败");
        return ESP_FAIL;
    }

    // 设置BH1750测量模式
    ret = bh1750_set_measure_mode(bh1750_dev, BH1750_CONTINUE_1LX_RES);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BH1750设置测量模式失败");
        return ret;
    }

    return ESP_OK;
}

// 读取光照强度
esp_err_t read_light_intensity(float *light)
{
    if (bh1750_dev == NULL) {
        return ESP_FAIL;
    }
    return bh1750_get_data(bh1750_dev, light);
}

// 读取温湿度
esp_err_t read_temperature_humidity(float *temperature, float *humidity)
{
    return dht_read_float_data(DHT_TYPE_DHT11, DHT11_GPIO_PIN, humidity, temperature);
}

// 更新数据模型中的传感器数据
esp_err_t sensors_update_data_model(data_model_t *model)
{
    if (model == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    float temperature = 0.0f;
    float humidity = 0.0f;
    float light = 0.0f;
    
    // 读取温湿度
    esp_err_t temp_hum_ret = read_temperature_humidity(&temperature, &humidity);
    
    // 读取光照强度
    esp_err_t light_ret = read_light_intensity(&light);
    
    // 只要有一个数据读取成功，就更新数据模型
    if (temp_hum_ret == ESP_OK || light_ret == ESP_OK) {
        return data_model_update_sensor_data(model, temperature, humidity, light);
    } else {
        ESP_LOGW(TAG, "未能读取任何传感器数据");
        return ESP_FAIL;
    }
}

// 传感器任务
void sensors_task(void *pvParameters)
{
    float light, temperature, humidity;
    data_model_t *data_model = data_model_get_latest();
    
    while (1) {
        // 读取光照强度
        if (read_light_intensity(&light) == ESP_OK) {
            ESP_LOGI(TAG, "光照强度: %.2f lx", light);
        }

        // 读取温湿度
        if (read_temperature_humidity(&temperature, &humidity) == ESP_OK) {
            ESP_LOGI(TAG, "温度: %.1f°C, 湿度: %.1f%%", temperature, humidity);
        }
        
        // 更新数据模型
        if (data_model != NULL) {
            sensors_update_data_model(data_model);
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_UPDATE_INTERVAL_MS)); // 每10秒读取一次
    }
} 

esp_err_t sensors_task_init(void)
{
    BaseType_t ret = xTaskCreate(sensors_task, "sensors_task", 4096, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "传感器任务创建失败");
        return ESP_FAIL;
    }
    return ESP_OK;
}
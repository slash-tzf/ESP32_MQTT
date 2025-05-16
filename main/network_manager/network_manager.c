#include "network_manager.h"
#include "modem_4g.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "usbh_modem_wifi.h"
#include "string.h"

#define BOOT_BUTTON_GPIO_NUM  0       // GPIO0为BOOT按钮
#define BOOT_BUTTON_ACTIVE_LEVEL 0    // 低电平为按下

static const char *TAG = "NETWORK_MGR";
static network_mode_t s_current_mode = NETWORK_MODE_4G;  // 默认使用4G模式
static button_handle_t s_boot_button = NULL;

// WiFi配置 - 全局变量，可从外部访问
char wifi_ssid[33] = {0};
char wifi_password[65] = {0};

// NVS存储相关
#define NVS_NAMESPACE "network"
#define NVS_KEY_MODE "net_mode"
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASSWORD "wifi_pwd"

static esp_err_t save_current_mode_to_nvs()
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS namespace: %s", esp_err_to_name(err));
        return err;
    }
    
    // 保存当前模式
    err = nvs_set_u8(nvs_handle, NVS_KEY_MODE, (uint8_t)s_current_mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error setting mode in NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // 提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS changes: %s", esp_err_to_name(err));
    }
    
    nvs_close(nvs_handle);
    return err;
}

static esp_err_t load_mode_from_nvs()
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS namespace: %s", esp_err_to_name(err));
        return err;
    }
    
    // 获取保存的模式
    uint8_t mode;
    err = nvs_get_u8(nvs_handle, NVS_KEY_MODE, &mode);
    if (err == ESP_OK) {
        s_current_mode = (network_mode_t)mode;
        ESP_LOGI(TAG, "从NVS加载网络模式: %d", s_current_mode);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        // 如果未找到，保存当前默认值
        ESP_LOGI(TAG, "NVS中未找到网络模式，使用默认值: %d", s_current_mode);
        nvs_set_u8(nvs_handle, NVS_KEY_MODE, (uint8_t)s_current_mode);
        nvs_commit(nvs_handle);
    } else {
        ESP_LOGE(TAG, "从NVS获取网络模式时出错: %s", esp_err_to_name(err));
    }
    
    nvs_close(nvs_handle);
    return ESP_OK;
}

static esp_err_t load_wifi_config_from_nvs()
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS namespace: %s", esp_err_to_name(err));
        return err;
    }
    
    // 读取WiFi SSID
    size_t ssid_len = sizeof(wifi_ssid);
    err = nvs_get_str(nvs_handle, NVS_KEY_WIFI_SSID, wifi_ssid, &ssid_len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Error getting WiFi SSID from NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // 读取WiFi密码
    size_t pwd_len = sizeof(wifi_password);
    err = nvs_get_str(nvs_handle, NVS_KEY_WIFI_PASSWORD, wifi_password, &pwd_len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Error getting WiFi password from NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    nvs_close(nvs_handle);
    
    if (wifi_ssid[0] != '\0') {
        ESP_LOGI(TAG, "从NVS加载WiFi配置: SSID=%s", wifi_ssid);
    } else {
        ESP_LOGI(TAG, "NVS中未找到WiFi配置");
    }
    
    return ESP_OK;
}


static esp_err_t switch_to_4g_mode()
{
    ESP_LOGI(TAG, "切换到4G模式");

    
    // 更新当前模式并保存
    s_current_mode = NETWORK_MODE_4G;
    save_current_mode_to_nvs();
    
    // 重启ESP32以应用新的网络模式
    ESP_LOGI(TAG, "已切换到4G模式，重启设备...");
    vTaskDelay(pdMS_TO_TICKS(1000)); // 等待1秒确保NVS写入完成
    esp_restart();
    
    return ESP_OK;
}

static esp_err_t switch_to_wifi_sta_mode()
{
    ESP_LOGI(TAG, "切换到WiFi STA+AP模式");
    

    
    // 更新当前模式并保存
    s_current_mode = NETWORK_MODE_WIFI_STA_AP;
    save_current_mode_to_nvs();
    
    // 重启ESP32以应用新的网络模式
    ESP_LOGI(TAG, "已切换到WiFi STA+AP模式，重启设备...");
    vTaskDelay(pdMS_TO_TICKS(1000)); // 等待1秒确保NVS写入完成
    esp_restart();
    
    return ESP_OK;
}

// 双击按钮回调函数
static void button_double_click_cb(void *arg, void *usr_data)
{
    ESP_LOGI(TAG, "检测到双击事件，切换网络模式");
    
    if (s_current_mode == NETWORK_MODE_4G) {
        // 当前是4G模式，切换到WiFi STA模式
        switch_to_wifi_sta_mode();
    } else {
        // 当前是WiFi STA模式，切换到4G模式
        switch_to_4g_mode();
    }
}

esp_err_t network_manager_init()
{
    // 加载网络模式设置和WiFi配置
    ESP_ERROR_CHECK(load_mode_from_nvs());
    ESP_ERROR_CHECK(load_wifi_config_from_nvs());
    
    // 根据保存的模式初始化网络
    if (s_current_mode == NETWORK_MODE_4G) {
        ESP_LOGI(TAG, "初始化为4G模式");
    } else if (s_current_mode == NETWORK_MODE_WIFI_STA_AP) {
        ESP_LOGI(TAG, "初始化为WiFi STA模式");
    }
    
    return ESP_OK;
}

esp_err_t network_manager_set_mode(network_mode_t mode)
{
    if (mode == s_current_mode) {
        ESP_LOGI(TAG, "当前已经是目标模式，无需切换");
        return ESP_OK;
    }
    
    esp_err_t ret = ESP_OK;
    if (mode == NETWORK_MODE_4G) {
        ret = switch_to_4g_mode();
    } else if (mode == NETWORK_MODE_WIFI_STA_AP) {
        ret = switch_to_wifi_sta_mode();
    } else {
        ESP_LOGE(TAG, "无效的网络模式");
        return ESP_ERR_INVALID_ARG;
    }
    
    return ret;
}

network_mode_t network_manager_get_mode()
{
    return s_current_mode;
}

esp_err_t network_manager_set_wifi_config(const char *ssid, const char *password)
{
    if (ssid == NULL) {
        ESP_LOGE(TAG, "SSID不能为空");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 保存WiFi配置
    strncpy(wifi_ssid, ssid, sizeof(wifi_ssid) - 1);
    if (password != NULL) {
        strncpy(wifi_password, password, sizeof(wifi_password) - 1);
    } else {
        wifi_password[0] = '\0';
    }
    
    // 保存到NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS namespace: %s", esp_err_to_name(err));
        return err;
    }
    
    err = nvs_set_str(nvs_handle, NVS_KEY_WIFI_SSID, wifi_ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving WiFi SSID to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_set_str(nvs_handle, NVS_KEY_WIFI_PASSWORD, wifi_password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving WiFi password to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS changes: %s", esp_err_to_name(err));
    }
    nvs_close(nvs_handle);
    
    return ESP_OK;
}

esp_err_t network_manager_init_button()
{
    // 初始化按钮
    const button_config_t boot_btn_cfg = {
        .long_press_time = 1000,      // 长按阈值1秒
        .short_press_time = 180       // 短按阈值180毫秒
    };
    
    const button_gpio_config_t boot_gpio_cfg = {
        .gpio_num = BOOT_BUTTON_GPIO_NUM,
        .active_level = BOOT_BUTTON_ACTIVE_LEVEL,
    };
    
    esp_err_t ret = iot_button_new_gpio_device(&boot_btn_cfg, &boot_gpio_cfg, &s_boot_button);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建按钮设备失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 注册双击事件回调
    ret = iot_button_register_cb(s_boot_button, BUTTON_DOUBLE_CLICK, NULL, button_double_click_cb, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "注册按钮回调失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "按钮初始化成功，双击BOOT键可切换网络模式");
    return ESP_OK;
}
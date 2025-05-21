#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include <string.h>
#include "led.h"

// 外部变量，用于从network_manager.c获取WiFi配置
extern char wifi_ssid[33];
extern char wifi_password[65];

static const char *TAG = "WIFI_MGR";

// WiFi事件组
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// WiFi事件处理函数
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
        led_set_color(0, 0, 255);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" left, AID=%d",
                 MAC2STR(event->mac), event->aid);
        led_set_color(0, 0, 0);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "连接断开，尝试重新连接...");
        esp_wifi_connect();
        led_set_color(255, 0, 0);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "获取IP地址:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        led_set_color(0,128,0);
    }
}

// 初始化SoftAP
static esp_netif_t *wifi_init_softap(modem_wifi_config_t *wifi_AP_config)
{
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_ap_config = {
        .ap = {
            .max_connection = wifi_AP_config->max_connection,
            .authmode = wifi_AP_config->authmode,
        },
    };

    strncpy((char *)wifi_ap_config.ap.ssid, wifi_AP_config->ssid, sizeof(wifi_ap_config.ap.ssid));
    strncpy((char *)wifi_ap_config.ap.password, wifi_AP_config->password, sizeof(wifi_ap_config.ap.password));
    wifi_ap_config.ap.ssid_hidden = wifi_AP_config->ssid_hidden;
    
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(ESP_IF_WIFI_AP, wifi_AP_config->bandwidth));

    ESP_LOGI(TAG, "WiFi SoftAP初始化完成. SSID:%s 密码:%s 信道:%zu",
             wifi_AP_config->ssid, wifi_AP_config->password, wifi_AP_config->channel);

    return esp_netif_ap;
}

// 初始化WiFi Station
static esp_netif_t *wifi_init_sta(void)
{
    esp_netif_t *esp_netif_sta = esp_netif_create_default_wifi_sta();

    wifi_config_t wifi_sta_config = {
        .sta = {
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    
    // 复制SSID和密码
    if (wifi_ssid[0] != '\0') {
        strncpy((char*)wifi_sta_config.sta.ssid, wifi_ssid, sizeof(wifi_sta_config.sta.ssid) - 1);
        strncpy((char*)wifi_sta_config.sta.password, wifi_password, sizeof(wifi_sta_config.sta.password) - 1);
    } else {
        ESP_LOGW(TAG, "WiFi SSID未设置，STA接口将无法连接");
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config));

    ESP_LOGI(TAG, "WiFi STA初始化完成. SSID:%s", wifi_ssid);

    return esp_netif_sta;
}

// 初始化WiFi APSTA模式
esp_err_t wifi_apsta_init(modem_wifi_config_t *wifi_AP_config)
{
    s_wifi_event_group = xEventGroupCreate();

    // 注册事件处理程序
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                   ESP_EVENT_ANY_ID,
                   &wifi_event_handler,
                   NULL,
                   NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                   IP_EVENT_STA_GOT_IP,
                   &wifi_event_handler,
                   NULL,
                   NULL));

    // 初始化WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 设置为APSTA模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // 初始化AP
    esp_netif_t *esp_netif_ap = wifi_init_softap(wifi_AP_config);

    // 初始化STA
    esp_netif_t *esp_netif_sta = wifi_init_sta();

    // 启动WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    // 等待连接或失败
    if (wifi_ssid[0] != '\0') {
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE,
                                            pdFALSE,
                                            pdMS_TO_TICKS(10000));
        
        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "已连接到AP SSID:%s", wifi_ssid);
        } else {
            ESP_LOGW(TAG, "连接到AP失败. SSID:%s", wifi_ssid);
        }
    }
    
    // 设置STA为默认接口
    esp_netif_set_default_netif(esp_netif_sta);
    
    // 在AP接口上启用NAPT
    if (esp_netif_napt_enable(esp_netif_ap) != ESP_OK) {
        ESP_LOGE(TAG, "在AP接口上启用NAPT失败");
        return ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "NAPT已启用，设备可通过AP接口访问互联网");
    }

    return ESP_OK;
}


esp_err_t wifi_connect_sta(const char *ssid, const char *password)
{
    if (ssid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    wifi_config_t wifi_sta_config = {0};
    
    // 复制SSID和密码
    strncpy((char*)wifi_sta_config.sta.ssid, ssid, sizeof(wifi_sta_config.sta.ssid) - 1);
    if (password != NULL) {
        strncpy((char*)wifi_sta_config.sta.password, password, sizeof(wifi_sta_config.sta.password) - 1);
    }
    
    wifi_sta_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_sta_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config));
    ESP_ERROR_CHECK(esp_wifi_connect());
    
    ESP_LOGI(TAG, "尝试连接到WiFi: SSID=%s", ssid);
    
    return ESP_OK;
}

esp_err_t wifi_stop()
{
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "断开WiFi连接失败: %s", esp_err_to_name(err));
        // 继续尝试停止WiFi
    }
    
    err = esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "停止WiFi失败: %s", esp_err_to_name(err));
        return err;
    }
    
    return ESP_OK;
} 
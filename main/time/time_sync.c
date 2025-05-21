#include "time_sync.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "time.h"

static const char *TAG = "TIME_SYNC";

void time_sync_init(void)
{
    // 初始化SNTP 同步时间
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
    esp_netif_sntp_init(&config);

    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update system time within 10s timeout");
    }else{
        ESP_LOGI(TAG, "Time updated successfully");
    }
    setenv("TZ", "CST-8", 1);
    tzset();
}


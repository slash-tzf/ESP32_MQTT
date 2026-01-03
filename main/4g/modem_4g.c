#include "modem_4g.h"
#include "usbh_modem_board.h"
#include "esp_log.h"
#include "led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "freertos/task.h"

static const char *TAG = "MODEM_4G";

static bool s_sim_present = true;
static bool s_usb_connected = true;
static bool s_modem_net_connected = false;
static int s_modem_wifi_sta_count = 0;

static void modem_led_update(void)
{
    if (!s_usb_connected) {
        led_set_color(255, 255, 0);
        return;
    }
    if (!s_sim_present) {
        led_set_color(255, 0, 0);
        return;
    }
    if (s_modem_wifi_sta_count > 0) {
        led_set_color(0, 0, 255);
        return;
    }
    if (s_modem_net_connected) {
        led_set_color(0, 128, 0);
        return;
    }
    led_set_color(0, 0, 0);
}


static void on_modem_event(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    if (event_base == MODEM_BOARD_EVENT)
    {
        if (event_id == MODEM_EVENT_SIMCARD_DISCONN)
        {
            ESP_LOGW(TAG, "Modem Board Event: SIM Card disconnected");
            s_sim_present = false;
            modem_led_update();
        }
        else if (event_id == MODEM_EVENT_SIMCARD_CONN)
        {
            ESP_LOGI(TAG, "Modem Board Event: SIM Card Connected");
            s_sim_present = true;
            modem_led_update();
        }
        else if (event_id == MODEM_EVENT_DTE_DISCONN)
        {
            ESP_LOGW(TAG, "Modem Board Event: USB disconnected");
            s_usb_connected = false;
            modem_led_update();
        }
        else if (event_id == MODEM_EVENT_DTE_CONN)
        {
            ESP_LOGI(TAG, "Modem Board Event: USB connected");
            s_usb_connected = true;
            modem_led_update();
        }
        else if (event_id == MODEM_EVENT_DTE_RESTART)
        {
            ESP_LOGW(TAG, "Modem Board Event: Hardware restart");
            s_usb_connected = false;
            modem_led_update();
        }
        else if (event_id == MODEM_EVENT_DTE_RESTART_DONE)
        {
            ESP_LOGI(TAG, "Modem Board Event: Hardware restart done");
            s_usb_connected = true;
            modem_led_update();
        }
        else if (event_id == MODEM_EVENT_NET_CONN)
        {
            ESP_LOGI(TAG, "Modem Board Event: Network connected");
            s_modem_net_connected = true;
            modem_led_update();
        }
        else if (event_id == MODEM_EVENT_NET_DISCONN)
        {
            ESP_LOGW(TAG, "Modem Board Event: Network disconnected");
            s_modem_net_connected = false;
            modem_led_update();
        }
        else if (event_id == MODEM_EVENT_WIFI_STA_CONN)
        {
            ESP_LOGI(TAG, "Modem Board Event: Station connected");
            s_modem_wifi_sta_count++;
            modem_led_update();
        }
        else if (event_id == MODEM_EVENT_WIFI_STA_DISCONN)
        {
            ESP_LOGW(TAG, "Modem Board Event: All stations disconnected");
            if (s_modem_wifi_sta_count > 0) {
                s_modem_wifi_sta_count--;
            }
            modem_led_update();
        }
    }
}

void modem_4g_init()
{
    /* Initialize modem board. Dial-up internet */
    modem_config_t modem_config = MODEM_DEFAULT_CONFIG();

// #ifndef CONFIG_EXAMPLE_ENTER_PPP_DURING_INIT
//     /* if Not enter ppp, modem will enter command mode after init */
//     modem_config.flags |= MODEM_FLAGS_INIT_NOT_ENTER_PPP;
//     /* if Not waiting for modem ready, just return after modem init */
//     modem_config.flags |= MODEM_FLAGS_INIT_NOT_BLOCK;
// #endif

    modem_config.handler = on_modem_event;
    modem_board_init(&modem_config);
}

/**
 * @brief 断开4G模块网络连接
 * 
 * @return esp_err_t 
 */
esp_err_t modem_4g_disconnect()
{
    ESP_LOGI(TAG, "正在断开4G网络连接");
    modem_board_ppp_stop(5000);
    return ESP_OK;
}

/**
 * @brief 重新连接4G模块网络
 * 
 * @return esp_err_t 
 */
esp_err_t modem_4g_connect()
{
    ESP_LOGI(TAG, "正在连接4G网络");
    modem_board_ppp_start(5000);
    return ESP_OK;
}
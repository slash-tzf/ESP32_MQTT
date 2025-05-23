/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include "esp_eth.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_tls_crypto.h"
#include "esp_vfs.h"
#include "json_parser.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "modem_http_config.h"
#include "data_model.h"
#include "network_manager.h"
#include "mqtt.h"
#include "cJSON.h"

/* A simple example that demonstrates how to create GET and POST
 * handlers for the web server.
 */
static const char *TAG = "4g_router_server";
#define HTTPD_401 "401 UNAUTHORIZED" /*!< HTTP Response 401 */

#define REST_CHECK(a, str, goto_tag, ...)                                         \
    do {                                                                          \
        if (!(a)) {                                                               \
            ESP_LOGE(TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            goto goto_tag;                                                        \
        }                                                                         \
    } while (0)

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (10240)

#define CHECK_FILE_EXTENSION(filename, ext) (strcasecmp(&filename[strlen(filename) - strlen(ext)], ext) == 0)

/**
 * @brief Store the currently connected sta
 *
 */
#define STA_CHECK(a, str, ret_value)                                           \
    if (!(a))                                                                  \
    {                                                                          \
        ESP_LOGE(TAG, "%s(%d): %s", __FUNCTION__, __LINE__, str); \
        return (ret_value);                                                    \
    }
#define STA_CHECK_GOTO(a, str, label)                                          \
    if (!(a)) {                                                                \
        ESP_LOGE(TAG, "%s:%d (%s):%s", __FILE__, __LINE__, __FUNCTION__, str); \
        goto label;                                                            \
    }

#define STA_NODE_MUTEX_TICKS_TO_WAIT (pdMS_TO_TICKS(300))
static SemaphoreHandle_t s_sta_node_mutex = NULL;
static modem_wifi_config_t *s_modem_wifi_config = NULL;
static modem_http_list_head_t s_sta_list_head = SLIST_HEAD_INITIALIZER(s_sta_list_head);

#define NVS_MQTT_BROKER_KEY        "mqtt_broker"
#define NVS_MQTT_USERNAME_KEY      "mqtt_username"
#define NVS_MQTT_PASSWORD_KEY      "mqtt_password"
#define NVS_MQTT_NAMESPACE         "mqtt_config"

static void delete_char(char *str, char target)
{
    int i, j;
    for (i = j = 0; str[i] != '\0'; i++) {
        if (str[i] != target) {
            str[j++] = str[i];
        }
    }
    str[j] = '\0';
}

/**
 * @brief URL解码函数，将URL编码的字符串转换为普通字符串
 * 
 * @param input 输入的URL编码字符串
 * @param output 输出的解码后字符串
 */
static void url_decode(const char *input, char *output)
{
    char *out = output;
    const char *in = input;
    
    while (*in) {
        if (*in == '%' && *(in + 1) && *(in + 2)) {
            // 处理百分号编码
            char hex[3] = {*(in + 1), *(in + 2), 0};
            *out++ = (char)strtol(hex, NULL, 16);
            in += 3;
        } else if (*in == '+') {
            // 将加号转换为空格
            *out++ = ' ';
            in++;
        } else {
            // 其他字符直接复制
            *out++ = *in++;
        }
    }
    *out = '\0';
}

static void restart()
{
    ESP_LOGI(TAG, "Restarting now.\n");
    fflush(stdout);
    esp_restart();
}

typedef struct rest_server_context {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

typedef struct {
    rest_server_context_t *rest_context;
} ctx_info_t;

void nvs_get_str_log(esp_err_t err, char *key, char *value)
{
    switch (err) {
    case ESP_OK:
        ESP_LOGI(TAG, "%s = %s", key, value);
        break;
    case ESP_ERR_NVS_NOT_FOUND:
        ESP_LOGI(TAG, "%s : Can't find in NVS!", key);
        break;
    default:
        ESP_LOGE(TAG, "Error (%s) reading!", esp_err_to_name(err));
    }
}

static esp_err_t from_nvs_set_value(char *key, char *value)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("memory", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        return ESP_FAIL;
    } else {
        err = nvs_set_str(my_handle, key, value);
        ESP_LOGI(TAG, "set %s is %s!,the err is %d\n", key, (err == ESP_OK) ? "succeed" : "failed", err);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "NVS close Done\n");
    }
    return ESP_OK;
}

static esp_err_t from_nvs_get_value(char *key, char *value, size_t *size)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("memory", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        return ESP_FAIL;
    } else {
        err = nvs_get_str(my_handle, key, value, size);
        nvs_get_str_log(err, key, value);
        nvs_close(my_handle);
    }
    return err;
}

esp_err_t modem_http_print_nodes(modem_http_list_head_t *head)
{
    struct modem_netif_sta_info *node;
    SLIST_FOREACH(node, head, field) {
        ESP_LOGI(TAG, "MAC is " MACSTR ", IP is " IPSTR ", start_time is %lld ", MAC2STR(node->mac),
                 IP2STR(&node->ip), node->start_time);
    }
    return ESP_OK;
}

static esp_err_t stalist_update()
{
    if (pdTRUE == xSemaphoreTake(s_sta_node_mutex, STA_NODE_MUTEX_TICKS_TO_WAIT)) {
        struct modem_netif_sta_info *node;
        SLIST_FOREACH(node, &s_sta_list_head, field) {
            if (node->ip.addr == 0) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
                esp_netif_pair_mac_ip_t pair_mac_ip = { 0 };
                memcpy(pair_mac_ip.mac, node->mac, 6);
                esp_netif_dhcps_get_clients_by_mac(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), 1, &pair_mac_ip);
                node->ip = pair_mac_ip.ip;
#else
                dhcp_search_ip_on_mac(node->mac, (ip4_addr_t *)&node->ip);
#endif
            }
            char mac_addr[18] = "";
            size_t name_size = sizeof(node->name);
            sprintf(mac_addr, "%02x%02x%02x%02x%02x%02x", node->mac[0], node->mac[1], node->mac[2], node->mac[3], node->mac[4], node->mac[5]);
            from_nvs_get_value(mac_addr, node->name, &name_size);
        }
        if (!(pdTRUE == xSemaphoreGive(s_sta_node_mutex))) {
            ESP_LOGE(TAG, "give semaphore failed");
        };
    }
    modem_http_print_nodes(&s_sta_list_head);
    return ESP_OK;
}

modem_http_list_head_t *modem_http_get_stalist(){
    return &s_sta_list_head;
}

static esp_err_t stalist_add_node(uint8_t mac[6])
{
    // STA_CHECK(sta != NULL, "sta pointer can not be NULL", ESP_ERR_INVALID_ARG);
    struct modem_netif_sta_info *node = calloc(1, sizeof(struct modem_netif_sta_info));
    STA_CHECK(node != NULL, "calloc node failed", ESP_ERR_NO_MEM);
    STA_CHECK_GOTO(pdTRUE == xSemaphoreTake(s_sta_node_mutex, STA_NODE_MUTEX_TICKS_TO_WAIT), "take semaphore timeout", cleanupnode);
    node->start_time = esp_timer_get_time();
    memcpy(node->mac, mac, 6);
    char mac_addr[18] = "";
    size_t name_size = sizeof(node->name);
    sprintf(mac_addr, "%02x%02x%02x%02x%02x%02x", node->mac[0], node->mac[1], node->mac[2], node->mac[3], node->mac[4], node->mac[5]);
    esp_err_t err = from_nvs_get_value(mac_addr, node->name, &name_size); // name
    if (err == ESP_ERR_NVS_NOT_FOUND ) {
        memcpy(node->name, mac_addr, 12);
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_netif_pair_mac_ip_t pair_mac_ip = { 0 };
    memcpy(pair_mac_ip.mac, node->mac, 6);
    esp_netif_dhcps_get_clients_by_mac(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), 1, &pair_mac_ip);
    node->ip = pair_mac_ip.ip;
#else
    dhcp_search_ip_on_mac(node->mac, (ip4_addr_t *)&node->ip);
#endif
    SLIST_INSERT_HEAD(&s_sta_list_head, node, field);
    STA_CHECK_GOTO(pdTRUE == xSemaphoreGive(s_sta_node_mutex), "give semaphore failed", cleanupnode);
    return ESP_OK;
cleanupnode:
    free(node);
    return ESP_FAIL;
}

static esp_err_t sta_remove_node(uint8_t mac[6])
{
    struct modem_netif_sta_info *node;
    STA_CHECK(pdTRUE == xSemaphoreTake(s_sta_node_mutex, STA_NODE_MUTEX_TICKS_TO_WAIT), "take semaphore timeout", ESP_ERR_TIMEOUT);
    SLIST_FOREACH(node, &s_sta_list_head, field) {
        if (!memcmp(node->mac, mac, 6)) {
            ESP_LOGI(TAG, "remove MAC is " MACSTR ", IP is " IPSTR ", start_time is %lld ", MAC2STR(node->mac),
                     IP2STR(&node->ip), node->start_time);
            SLIST_REMOVE(&s_sta_list_head, node, modem_netif_sta_info, field);
            free(node);
            break;
        }
    }
    if (!(pdTRUE == xSemaphoreGive(s_sta_node_mutex))) {
        ESP_LOGE(TAG, "give semaphore failed");
    };
    return ESP_OK;
}

static esp_err_t wlan_general_get_handler(httpd_req_t *req)
{
    const char *user_ssid = s_modem_wifi_config->ssid;
    const char *user_password = s_modem_wifi_config->password;
    const char *user_hide_ssid ;
    if (s_modem_wifi_config->ssid_hidden == 0) {
        user_hide_ssid = "flase";
    } else {
        user_hide_ssid = "true";
    }

    const char *user_auth_mode;
    switch (s_modem_wifi_config->authmode) {
    case WIFI_AUTH_OPEN:
        user_auth_mode = "OPEN";
        break;

    case WIFI_AUTH_WEP:
        user_auth_mode = "WEP";
        break;

    case WIFI_AUTH_WPA2_PSK:
        user_auth_mode = "WAP2_PSK";
        break;

    case WIFI_AUTH_WPA_WPA2_PSK:
        user_auth_mode = "WPA_WPA2_PSK";
        break;

    default:
        user_auth_mode = "WPA_WPA2_PSK";
        break;
    }

    char *json_str = NULL;
    size_t size = 0;
    size = asprintf(&json_str,
                    "{\"status\":\"200\", \"ssid\":\"%s\", \"if_hide_ssid\":\"%s\", "
                    "\"auth_mode\":\"%s\", \"password\":\"%s\"}",
                    user_ssid, user_hide_ssid, user_auth_mode, user_password);

    /**
     * @brief Set the HTTP status code
     */
    esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
    ESP_ERROR_CHECK(ret);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    /**
     * @brief Set the HTTP content type
     */
    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    ESP_ERROR_CHECK(ret);

    /**
     * @brief Set some custom headers
     */

    ret = httpd_resp_send(req, json_str, size);
    free(json_str);
    ESP_ERROR_CHECK(ret);
    return ESP_OK;
}

static esp_err_t wlan_general_post_handler(httpd_req_t *req)
{
    char user_ssid[32] = "";
    char user_password[64] = "";
    char user_hide_ssid[8] = "";
    char user_auth_mode[16] = "";

    char buf[256] = { 0 };
    int len_ret, remaining = req->content_len;

    if (remaining > sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "string too long");
        return ESP_FAIL;
    }

    while (remaining > 0) {
        /* Read the data for the request */
        if ((len_ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
            if (len_ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }

            return ESP_FAIL;
        }

        remaining -= len_ret;

        /* Log data received */
        ESP_LOGI(TAG, "=========== RECEIVED DATA ==========");
        ESP_LOGI(TAG, "%.*s", len_ret, buf);
        ESP_LOGI(TAG, "====================================");
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    jparse_ctx_t jctx;
    int ps_ret = json_parse_start(&jctx, buf, strlen(buf));

    if (ps_ret != OS_SUCCESS) {
        ESP_LOGE(TAG, "Parser failed\n");
        return ESP_FAIL;
    }

    char str_val[64];

    if (json_obj_get_string(&jctx, "ssid", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(user_ssid, sizeof(user_ssid), "%.*s", sizeof(user_ssid) - 1, str_val);
        ESP_LOGI(TAG, "ssid %s\n", user_ssid);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if (json_obj_get_string(&jctx, "if_hide_ssid", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(user_hide_ssid, sizeof(user_hide_ssid), "%.*s", sizeof(user_hide_ssid) - 1, str_val);
        ESP_LOGI(TAG, "if_hide_ssid %s\n", user_hide_ssid);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if (json_obj_get_string(&jctx, "auth_mode", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(user_auth_mode, sizeof(user_auth_mode), "%.*s", sizeof(user_auth_mode) - 1, str_val);
        ESP_LOGI(TAG, "auth_mode %s\n", user_auth_mode);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if (json_obj_get_string(&jctx, "password", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(user_password, sizeof(user_password), "%.*s", sizeof(user_password) - 1, str_val);
        ESP_LOGI(TAG, "password %s\n", user_password);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    char *json_str = NULL;
    size_t size = 0;
    size = asprintf(&json_str,
                    "{\"status\":\"200\", \"ssid\":\"%s\", \"if_hide_ssid\":\"%s\", "
                    "\"auth_mode\":\"%s\", \"password\":\"%s\"}",
                    user_ssid, user_hide_ssid, user_auth_mode, user_password);
    esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
    ESP_ERROR_CHECK(ret);
    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    ESP_ERROR_CHECK(ret);
    ret = httpd_resp_send(req, json_str, size);
    free(json_str);
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(from_nvs_set_value("ssid", user_ssid));
    ESP_ERROR_CHECK(from_nvs_set_value("hide_ssid", user_hide_ssid));
    ESP_ERROR_CHECK(from_nvs_set_value("auth_mode", user_auth_mode));
    ESP_ERROR_CHECK(from_nvs_set_value("password", user_password));

    restart();
    return ESP_OK;
}

static esp_err_t wlan_advance_get_handler(httpd_req_t *req)
{
    char *json_str = NULL;
    size_t size;
    size_t user_bandwidth;
    if (s_modem_wifi_config->bandwidth == WIFI_BW_HT20) {
        user_bandwidth = 20;
    } else {
        user_bandwidth = 40;
    }
    size = asprintf(&json_str, "{\"status\":\"200\", \"bandwidth\":\"%d\", \"channel\":\"%d\"}", user_bandwidth,
                    s_modem_wifi_config->channel);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    /**
     * @brief Set the HTTP status code
     */
    esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
    ESP_ERROR_CHECK(ret);

    /**
     * @brief Set the HTTP content type
     */
    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    ESP_ERROR_CHECK(ret);

    /**
     * @brief Set some custom headers
     */

    ret = httpd_resp_send(req, json_str, size);
    free(json_str);
    ESP_ERROR_CHECK(ret);
    return ESP_OK;
}

static esp_err_t wlan_advance_post_handler(httpd_req_t *req)
{
    char user_channel[4] = "";
    char user_bandwidth[4] = "";

    char buf[256] = { 0 };
    int len_ret, remaining = req->content_len;

    if (remaining > sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "string too long");
        return ESP_FAIL;
    }

    while (remaining > 0) {
        /* Read the data for the request */
        if ((len_ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
            if (len_ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }

            return ESP_FAIL;
        }

        remaining -= len_ret;

        /* Log data received */
        ESP_LOGI(TAG, "=========== RECEIVED DATA ==========");
        ESP_LOGI(TAG, "%.*s", len_ret, buf);
        ESP_LOGI(TAG, "====================================");
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    jparse_ctx_t jctx;
    int ps_ret = json_parse_start(&jctx, buf, strlen(buf));

    if (ps_ret != OS_SUCCESS) {
        ESP_LOGE(TAG, "Parser failed\n");
        return ESP_FAIL;
    }

    char str_val[64];

    if (json_obj_get_string(&jctx, "bandwidth", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(user_bandwidth, sizeof(user_bandwidth), "%.*s", sizeof(user_bandwidth) - 1, str_val);
        ESP_LOGI(TAG, "bandwidth: %s\n", user_bandwidth);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if (json_obj_get_string(&jctx, "channel", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(user_channel, sizeof(user_channel), "%.*s", sizeof(user_channel) - 1, str_val);
        ESP_LOGI(TAG, "channel: %s\n", user_channel);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    char *json_str = NULL;
    size_t size = 0;
    size = asprintf(&json_str, "{\"status\":\"200\", \"bandwidth\":\"%s\", \"channel\":\"%s\"}", user_bandwidth,
                    user_channel);
    esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
    ESP_ERROR_CHECK(ret);
    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    ESP_ERROR_CHECK(ret);
    ret = httpd_resp_send(req, json_str, size);
    free(json_str);
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(from_nvs_set_value("channel", user_channel));
    ESP_ERROR_CHECK(from_nvs_set_value("bandwidth", user_bandwidth));
    restart();

    return ESP_OK;
}

static esp_err_t system_station_get_handler(httpd_req_t *req)
{
    char *json_str = NULL;
    char *json_str_old = NULL;
    size_t size = 0;
    struct modem_netif_sta_info *node;
    size = asprintf(&json_str, "{\"station_list\":[");
    SLIST_FOREACH(node, &s_sta_list_head, field) {
        asprintf(&json_str_old, "%s", json_str);
        free(json_str);
        json_str = NULL;
        size = asprintf(&json_str,
                        "%s{\"name_str\":\"%s\",\"mac_str\":\"" MACSTR "\",\"ip_str\":\"" IPSTR
                        "\",\"online_time_s\":\"%lld\"}%c",
                        json_str_old, node->name, MAC2STR(node->mac), IP2STR(&node->ip),
                        node->start_time, node->field.sle_next ? ',' : '\0');
        free(json_str_old);
        json_str_old = NULL;
    }
    size = asprintf(&json_str_old, "%s],\"now_time\":\"%lld\"}", json_str, esp_timer_get_time());

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    /**
     * @brief Set the HTTP status code
     */
    esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
    ESP_ERROR_CHECK(ret);

    /**
     * @brief Set the HTTP content type
     */
    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    ESP_ERROR_CHECK(ret);

    /**
     * @brief Set some custom headers
     */

    ret = httpd_resp_send(req, json_str_old, size);
    ESP_LOGD(TAG, "%s", json_str_old);
    free(json_str);
    free(json_str_old);
    ESP_ERROR_CHECK(ret);
    return ESP_OK;
}

static esp_err_t system_station_delete_device_post_handler(httpd_req_t *req)
{
    char buf[256] = { 0 };
    char mac_str[18] = "";
    int value[6] = { 0 };
    uint8_t mac_byte[6] = { 0 };
    uint16_t aid = 0;
    int len_ret, remaining = req->content_len;

    if (remaining > sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "string too long");
        return ESP_FAIL;
    }

    while (remaining > 0) {
        /* Read the data for the request */
        if ((len_ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
            if (len_ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }
            return ESP_FAIL;
        }

        remaining -= len_ret;

        /* Log data received */
        ESP_LOGI(TAG, "=========== RECEIVED DATA ==========");
        ESP_LOGI(TAG, "%.*s", len_ret, buf);
        ESP_LOGI(TAG, "====================================");
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    jparse_ctx_t jctx;
    int ps_ret = json_parse_start(&jctx, buf, strlen(buf));

    if (ps_ret != OS_SUCCESS) {
        ESP_LOGE(TAG, "Parser failed\n");
        return ESP_FAIL;
    }

    char str_val[64];

    if (json_obj_get_string(&jctx, "mac_str", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(mac_str, sizeof(mac_str), "%.*s", sizeof(mac_str) - 1, str_val);
        ESP_LOGI(TAG, "mac_str: %s\n", mac_str);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if (6 == sscanf(mac_str, "%x:%x:%x:%x:%x:%x%*c", &value[0], &value[1], &value[2], &value[3], &value[4], &value[5])) {
        for (size_t i = 0; i < 6; i++) {
            mac_byte[i] = (uint8_t)value[i];
        }
        ESP_LOGI(TAG, "trans mac_addr from str to uint8_t ok");
    } else {
        ESP_LOGE(TAG, "trans mac_addr from str to uint8_t fail");
    }

    ESP_ERROR_CHECK(esp_wifi_ap_get_sta_aid(mac_byte, &aid));
    ESP_LOGI(TAG, "remove aid is %d", aid);
    esp_err_t res_deauth_sta;
    res_deauth_sta = esp_wifi_deauth_sta(aid);
    if (res_deauth_sta == ESP_OK) {
        ESP_LOGI(TAG, "deauth OK");
    } else {
        ESP_LOGI(TAG, "deauth failed");
    }

    esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
    ESP_ERROR_CHECK(ret);
    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    ESP_ERROR_CHECK(ret);
    ret = httpd_resp_send(req, NULL, 0);
    ESP_ERROR_CHECK(ret);

    return ESP_OK;
}

static esp_err_t system_station_change_name_post_handler(httpd_req_t *req)
{
    char buf[256] = { 0 };
    int len_ret, remaining = req->content_len;

    if (remaining > sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "string too long");
        return ESP_FAIL;
    }

    while (remaining > 0) {
        /* Read the data for the request */
        if ((len_ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
            if (len_ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }

            return ESP_FAIL;
        }

        remaining -= len_ret;

        /* Log data received */
        ESP_LOGI(TAG, "=========== RECEIVED DATA ==========");
        ESP_LOGI(TAG, "%.*s", len_ret, buf);
        ESP_LOGI(TAG, "====================================");
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    jparse_ctx_t jctx;
    int ps_ret = json_parse_start(&jctx, buf, strlen(buf));

    if (ps_ret != OS_SUCCESS) {
        ESP_LOGE(TAG, "Parser failed\n");
        return ESP_FAIL;
    }

    char str_val[64];
    char name_str[36] = "";
    char mac_str[18] = "";

    if (json_obj_get_string(&jctx, "name_str", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(name_str, sizeof(name_str), "%.*s", sizeof(name_str) - 1, str_val);
        ESP_LOGI(TAG, "name_str: %s\n", name_str);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }

    if (json_obj_get_string(&jctx, "mac_str", str_val, sizeof(str_val)) == OS_SUCCESS) {
        snprintf(mac_str, sizeof(mac_str), "%.*s", sizeof(mac_str) - 1, str_val);
        ESP_LOGI(TAG, "mac_str: %s\n", mac_str);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid post");
        return ESP_FAIL;
    }
    /**
     * @brief The length of the control key is within 15 bytes
     *    00:01:02:03:04:05 --> 000102030405
     */
    delete_char(mac_str, ':');

    ESP_ERROR_CHECK(from_nvs_set_value(mac_str, name_str));

    esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
    ESP_ERROR_CHECK(ret);
    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    ESP_ERROR_CHECK(ret);
    ret = httpd_resp_send(req, NULL, 0);
    ESP_ERROR_CHECK(ret);
    stalist_update();
    return ESP_OK;
}

static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filepath)
{
    const char *type = "text/plain";
    if (CHECK_FILE_EXTENSION(filepath, ".html")) {
        type = "text/html";
    } else if (CHECK_FILE_EXTENSION(filepath, ".js")) {
        type = "application/javascript";
    } else if (CHECK_FILE_EXTENSION(filepath, ".css")) {
        type = "text/css";
    } else if (CHECK_FILE_EXTENSION(filepath, ".png")) {
        type = "image/png";
    } else if (CHECK_FILE_EXTENSION(filepath, ".ico")) {
        type = "image/x-icon";
    } else if (CHECK_FILE_EXTENSION(filepath, ".svg")) {
        type = "text/xml";
    }
    return httpd_resp_set_type(req, type);
}

static esp_err_t rest_common_get_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "(%s) %s", __func__, req->uri);
    char filepath[FILE_PATH_MAX];

    ctx_info_t *ctx_info = (ctx_info_t *)req->user_ctx;
    rest_server_context_t *rest_context = ctx_info->rest_context;
    strlcpy(filepath, rest_context->base_path, sizeof(filepath));
    if (req->uri[strlen(req->uri) - 1] == '/') {
        strlcat(filepath, "/index.html", sizeof(filepath));
    } else {
        strlcat(filepath, req->uri, sizeof(filepath));
    }
    int fd = open(filepath, O_RDONLY, 0);
    if (fd == -1) {
        ESP_LOGE(TAG, "Failed to open file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    set_content_type_from_file(req, filepath);

    char *chunk = rest_context->scratch;
    ssize_t read_bytes;
    do {
        /* Read file in chunks into the scratch buffer */
        read_bytes = read(fd, chunk, SCRATCH_BUFSIZE);
        if (read_bytes == -1) {
            ESP_LOGE(TAG, "Failed to read file : %s", filepath);
        } else if (read_bytes > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                close(fd);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }
    } while (read_bytes > 0);
    /* Close file after sending complete */
    close(fd);
    ESP_LOGD(TAG, "File sending complete");
    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t sensors_data_get_handler(httpd_req_t *req)
{
    char *json_str = NULL;
    size_t size = 0;
    
    // 获取最新的数据模型
    data_model_t *model = data_model_get_latest();
    if (model == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No sensor data available");
        return ESP_FAIL;
    }
    
    // 构建JSON响应
    size = asprintf(&json_str, 
                    "{"
                    "\"temperature\":%.2f,"
                    "\"humidity\":%.2f,"
                    "\"light_intensity\":%.2f,"
                    "\"sensors_valid\":%s,"
                    "\"latitude\":%.6f,"
                    "\"longitude\":%.6f,"
                    "\"ns_indicator\":\"%c\","
                    "\"ew_indicator\":\"%c\","
                    "\"altitude\":%.2f,"
                    "\"speed\":%.2f,"
                    "\"course\":%.2f,"
                    "\"data_source\":%d,"
                    "\"gps_valid\":%s,"
                    "\"timestamp\":%ld"
                    "}",
                    model->sensors.temperature,
                    model->sensors.humidity,
                    model->sensors.light_intensity,
                    model->sensors.sensors_valid ? "true" : "false",
                    model->gps.latitude,
                    model->gps.longitude,
                    model->gps.ns_indicator,
                    model->gps.ew_indicator,
                    model->gps.altitude,
                    model->gps.speed,
                    model->gps.course,
                    model->gps.data_source,
                    model->gps.gps_valid ? "true" : "false",
                    (long)model->timestamp);
    
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    
    // 设置HTTP状态码
    esp_err_t ret = httpd_resp_set_status(req, HTTPD_200);
    ESP_ERROR_CHECK(ret);
    
    // 设置HTTP内容类型
    ret = httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    ESP_ERROR_CHECK(ret);
    
    // 发送响应
    ret = httpd_resp_send(req, json_str, size);
    ESP_LOGD(TAG, "%s", json_str);
    free(json_str);
    ESP_ERROR_CHECK(ret);
    
    return ESP_OK;
}

static esp_err_t wifi_sta_get_handler(httpd_req_t *req)
{
    // 重定向到静态HTML文件
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/wifi_sta.html");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t wifi_sta_post_handler(httpd_req_t *req)
{
    // 获取POST数据
    char *buf = malloc(1024);
    if (buf == NULL) {
        ESP_LOGE(TAG, "内存分配失败");
        return ESP_FAIL;
    }
    
    int total_len = req->content_len;
    int cur_len = 0;
    int received = 0;
    
    if (total_len >= 1024) {
        ESP_LOGE(TAG, "内容太长");
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "内容太长");
        return ESP_FAIL;
    }
    
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            ESP_LOGE(TAG, "接收数据失败");
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "接收数据失败");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';
    
    ESP_LOGI(TAG, "收到的WiFi配置数据: %s", buf);
    
    // 解析SSID和密码
    char ssid[33] = {0};
    char password[65] = {0};
    
    char *ssid_ptr = strstr(buf, "ssid=");
    char *password_ptr = strstr(buf, "password=");
    
    if (ssid_ptr) {
        ssid_ptr += 5; // 跳过 "ssid="
        char *end = strchr(ssid_ptr, '&');
        if (end) {
            strncpy(ssid, ssid_ptr, end - ssid_ptr);
        } else {
            strcpy(ssid, ssid_ptr);
        }
    }
    
    if (password_ptr) {
        password_ptr += 9; // 跳过 "password="
        char *end = strchr(password_ptr, '&');
        if (end) {
            strncpy(password, password_ptr, end - password_ptr);
        } else {
            strcpy(password, password_ptr);
        }
    }
    
    free(buf);
    
    // URL解码
    char decoded_ssid[33] = {0};
    char decoded_password[65] = {0};
    
    url_decode(ssid, decoded_ssid);
    url_decode(password, decoded_password);
    
    ESP_LOGI(TAG, "解析的WiFi配置 - SSID: %s", decoded_ssid);
    
    // 保存配置并尝试连接
    esp_err_t ret = network_manager_set_wifi_config(decoded_ssid, decoded_password);
    
    // 返回JSON结果
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    if (ret == ESP_OK) {
        httpd_resp_sendstr(req, "{\"status\":\"success\",\"message\":\"WiFi配置已保存并尝试连接\"}");
    } else {
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"保存WiFi配置失败\"}");
    }
    
    return ESP_OK;
}

// 添加网络模式获取API
static esp_err_t network_mode_get_handler(httpd_req_t *req)
{
    // 获取当前网络模式
    network_mode_t current_mode = network_manager_get_mode();
    
    // 返回JSON结果
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    char *resp_str = NULL;
    asprintf(&resp_str, "{\"mode\":%d}", current_mode);
    
    httpd_resp_sendstr(req, resp_str);
    free(resp_str);
    
    return ESP_OK;
}

/* MQTT主题列表处理函数 */
static esp_err_t mqtt_topics_get_handler(httpd_req_t *req)
{
    char resp_str[512];
    char topics[MAX_MQTT_TOPICS][64];
    int topic_count = 0;

    // 获取已订阅的主题列表
    esp_err_t ret = mqtt_get_subscribed_topics(topics, MAX_MQTT_TOPICS, &topic_count);
    if (ret != ESP_OK) {
        snprintf(resp_str, sizeof(resp_str), "{\"success\":false,\"message\":\"获取主题列表失败\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp_str, strlen(resp_str));
        return ESP_OK;
    }

    // 创建JSON响应
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    
    cJSON *topics_array = cJSON_AddArrayToObject(root, "topics");
    for (int i = 0; i < topic_count; i++) {
        cJSON_AddItemToArray(topics_array, cJSON_CreateString(topics[i]));
    }

    char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    cJSON_Delete(root);
    free(json_str);
    
    return ESP_OK;
}

/* 添加MQTT主题处理函数 */
static esp_err_t mqtt_topic_add_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = malloc(total_len + 1);
    if (buf == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    int received = 0;
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            free(buf);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    
    char resp_str[256];
    
    if (root == NULL) {
        snprintf(resp_str, sizeof(resp_str), "{\"success\":false,\"message\":\"无效的JSON数据\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp_str, strlen(resp_str));
        return ESP_OK;
    }

    cJSON *topic_json = cJSON_GetObjectItem(root, "topic");
    if (!cJSON_IsString(topic_json) || (topic_json->valuestring == NULL)) {
        snprintf(resp_str, sizeof(resp_str), "{\"success\":false,\"message\":\"无效的主题\"}");
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp_str, strlen(resp_str));
        return ESP_OK;
    }

    // 添加并订阅主题
    esp_err_t ret = mqtt_subscribe_topic(topic_json->valuestring, true);
    cJSON_Delete(root);
    
    if (ret == ESP_OK) {
        snprintf(resp_str, sizeof(resp_str), "{\"success\":true,\"message\":\"主题添加成功\"}");
    } else if (ret == ESP_ERR_NO_MEM) {
        snprintf(resp_str, sizeof(resp_str), "{\"success\":false,\"message\":\"主题数量已达上限\"}");
    } else {
        snprintf(resp_str, sizeof(resp_str), "{\"success\":false,\"message\":\"主题添加失败\"}");
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, strlen(resp_str));
    
    return ESP_OK;
}

/* 删除MQTT主题处理函数 */
static esp_err_t mqtt_topic_delete_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = malloc(total_len + 1);
    if (buf == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    int received = 0;
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            free(buf);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    
    char resp_str[256];
    
    if (root == NULL) {
        snprintf(resp_str, sizeof(resp_str), "{\"success\":false,\"message\":\"无效的JSON数据\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp_str, strlen(resp_str));
        return ESP_OK;
    }

    cJSON *topic_json = cJSON_GetObjectItem(root, "topic");
    if (!cJSON_IsString(topic_json) || (topic_json->valuestring == NULL)) {
        snprintf(resp_str, sizeof(resp_str), "{\"success\":false,\"message\":\"无效的主题\"}");
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp_str, strlen(resp_str));
        return ESP_OK;
    }

    // 取消订阅并删除主题
    esp_err_t ret = mqtt_unsubscribe_topic(topic_json->valuestring, true);
    cJSON_Delete(root);
    
    if (ret == ESP_OK) {
        snprintf(resp_str, sizeof(resp_str), "{\"success\":true,\"message\":\"主题删除成功\"}");
    } else if (ret == ESP_ERR_NOT_FOUND) {
        snprintf(resp_str, sizeof(resp_str), "{\"success\":false,\"message\":\"未找到指定主题\"}");
    } else {
        snprintf(resp_str, sizeof(resp_str), "{\"success\":false,\"message\":\"主题删除失败\"}");
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, strlen(resp_str));
    
    return ESP_OK;
}

/* 发布MQTT消息处理函数 */
static esp_err_t mqtt_publish_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = malloc(total_len + 1);
    if (buf == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    int received = 0;
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            free(buf);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    
    char resp_str[256];
    
    if (root == NULL) {
        snprintf(resp_str, sizeof(resp_str), "{\"success\":false,\"message\":\"无效的JSON数据\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp_str, strlen(resp_str));
        return ESP_OK;
    }

    cJSON *topic_json = cJSON_GetObjectItem(root, "topic");
    cJSON *message_json = cJSON_GetObjectItem(root, "message");
    
    if (!cJSON_IsString(topic_json) || (topic_json->valuestring == NULL) || 
        !cJSON_IsString(message_json) || (message_json->valuestring == NULL)) {
        snprintf(resp_str, sizeof(resp_str), "{\"success\":false,\"message\":\"无效的主题或消息\"}");
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp_str, strlen(resp_str));
        return ESP_OK;
    }

    // 发布消息
    esp_err_t ret = mqtt_publish_message(topic_json->valuestring, message_json->valuestring, 1);  // QoS 1
    cJSON_Delete(root);
    
    if (ret == ESP_OK) {
        snprintf(resp_str, sizeof(resp_str), "{\"success\":true,\"message\":\"消息发布成功\"}");
    } else {
        snprintf(resp_str, sizeof(resp_str), "{\"success\":false,\"message\":\"消息发布失败\"}");
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, strlen(resp_str));
    
    return ESP_OK;
}

/**
 * @brief 获取MQTT设置的处理函数
 * 
 * @param req HTTP请求
 * @return esp_err_t 
 */
static esp_err_t mqtt_settings_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    // 打开NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_MQTT_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "打开NVS命名空间失败: %s", esp_err_to_name(err));
        cJSON *root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "message", "无法访问存储");
        
        const char *response = cJSON_Print(root);
        httpd_resp_sendstr(req, response);
        
        cJSON_Delete(root);
        free((void *)response);
        return ESP_OK;
    }
    
    // 创建响应JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    
    // 读取MQTT Broker地址
    char broker[128] = CONFIG_MQTT_BROKER_URI;  // 默认值
    size_t broker_len = sizeof(broker);
    
    if (err == ESP_OK) {
        err = nvs_get_str(nvs_handle, NVS_MQTT_BROKER_KEY, broker, &broker_len);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "读取MQTT Broker失败: %s", esp_err_to_name(err));
        }
    }
    cJSON_AddStringToObject(root, "broker", broker);
    
    // 读取MQTT用户名
    char username[64] = CONFIG_MQTT_BROKER_USERNAME;  // 默认值
    size_t username_len = sizeof(username);
    
    if (err == ESP_OK) {
        err = nvs_get_str(nvs_handle, NVS_MQTT_USERNAME_KEY, username, &username_len);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "读取MQTT用户名失败: %s", esp_err_to_name(err));
        }
    }
    cJSON_AddStringToObject(root, "username", username);
    
    // 读取MQTT密码
    char password[64] = CONFIG_MQTT_BROKER_PASSWORD;  // 默认值
    size_t password_len = sizeof(password);
    
    if (err == ESP_OK) {
        err = nvs_get_str(nvs_handle, NVS_MQTT_PASSWORD_KEY, password, &password_len);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "读取MQTT密码失败: %s", esp_err_to_name(err));
        }
    }
    cJSON_AddStringToObject(root, "password", password);
    
    // 关闭NVS
    if (err == ESP_OK) {
        nvs_close(nvs_handle);
    }
    
    // 发送响应
    const char *response = cJSON_Print(root);
    httpd_resp_sendstr(req, response);
    
    // 释放资源
    cJSON_Delete(root);
    free((void *)response);
    
    return ESP_OK;
}

/**
 * @brief 保存MQTT设置的处理函数
 * 
 * @param req HTTP请求
 * @return esp_err_t 
 */
static esp_err_t mqtt_settings_save_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = malloc(total_len + 1);
    if (buf == NULL) {
        ESP_LOGE(TAG, "内存分配失败");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    // 接收请求体
    int received = 0;
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            ESP_LOGE(TAG, "接收请求体失败");
            free(buf);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';
    
    // 解析JSON
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON解析失败");
        cJSON *err_root = cJSON_CreateObject();
        cJSON_AddBoolToObject(err_root, "success", false);
        cJSON_AddStringToObject(err_root, "message", "请求格式无效");
        
        const char *err_response = cJSON_Print(err_root);
        httpd_resp_sendstr(req, err_response);
        
        cJSON_Delete(err_root);
        free((void *)err_response);
        return ESP_OK;
    }
    
    // 打开NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_MQTT_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开NVS命名空间失败: %s", esp_err_to_name(err));
        cJSON *err_root = cJSON_CreateObject();
        cJSON_AddBoolToObject(err_root, "success", false);
        cJSON_AddStringToObject(err_root, "message", "无法访问存储");
        
        const char *err_response = cJSON_Print(err_root);
        httpd_resp_sendstr(req, err_response);
        
        cJSON_Delete(err_root);
        cJSON_Delete(root);
        free((void *)err_response);
        return ESP_OK;
    }
    
    // 获取并保存MQTT Broker
    cJSON *broker_json = cJSON_GetObjectItem(root, "broker");
    if (broker_json != NULL && cJSON_IsString(broker_json)) {
        err = nvs_set_str(nvs_handle, NVS_MQTT_BROKER_KEY, broker_json->valuestring);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "保存MQTT Broker失败: %s", esp_err_to_name(err));
        }
    }
    
    // 获取并保存MQTT用户名
    cJSON *username_json = cJSON_GetObjectItem(root, "username");
    if (username_json != NULL && cJSON_IsString(username_json)) {
        err = nvs_set_str(nvs_handle, NVS_MQTT_USERNAME_KEY, username_json->valuestring);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "保存MQTT用户名失败: %s", esp_err_to_name(err));
        }
    }
    
    // 获取并保存MQTT密码
    cJSON *password_json = cJSON_GetObjectItem(root, "password");
    if (password_json != NULL && cJSON_IsString(password_json)) {
        err = nvs_set_str(nvs_handle, NVS_MQTT_PASSWORD_KEY, password_json->valuestring);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "保存MQTT密码失败: %s", esp_err_to_name(err));
        }
    }
    
    // 提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "提交NVS更改失败: %s", esp_err_to_name(err));
    }
    
    // 关闭NVS
    nvs_close(nvs_handle);
    
    // 尝试重新连接MQTT
    mqtt_reconnect();
    
    // 构造响应
    cJSON *resp_root = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp_root, "success", true);
    cJSON_AddStringToObject(resp_root, "message", "MQTT设置已保存并重新连接");
    
    const char *response = cJSON_Print(resp_root);
    httpd_resp_sendstr(req, response);
    
    // 释放资源
    cJSON_Delete(root);
    cJSON_Delete(resp_root);
    free((void *)response);
    
    return ESP_OK;
}

/**
 * @brief 获取MQTT连接状态的处理函数
 * 
 * @param req HTTP请求
 * @return esp_err_t 
 */
static esp_err_t mqtt_status_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    // 获取MQTT连接状态
    mqtt_connection_status_t status = mqtt_get_connection_status();
    const char *error_message = mqtt_get_error_message();
    
    // 创建响应JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddNumberToObject(root, "status", (int)status);
    
    // 添加状态文本描述
    const char *status_text = "未知状态";
    switch(status) {
        case MQTT_CONNECTION_STATUS_DISCONNECTED:
            status_text = "未连接";
            break;
        case MQTT_CONNECTION_STATUS_CONNECTING:
            status_text = "连接中";
            break;
        case MQTT_CONNECTION_STATUS_CONNECTED:
            status_text = "已连接";
            break;
        case MQTT_CONNECTION_STATUS_FAILED_AUTH:
            status_text = "认证失败";
            break;
        case MQTT_CONNECTION_STATUS_FAILED_SERVER:
            status_text = "服务器连接失败";
            break;
        case MQTT_CONNECTION_STATUS_FAILED_NETWORK:
            status_text = "网络连接失败";
            break;
        case MQTT_CONNECTION_STATUS_FAILED_UNKNOWN:
            status_text = "未知错误";
            break;
    }
    
    cJSON_AddStringToObject(root, "status_text", status_text);
    
    // 添加错误消息，如果有
    if (error_message && error_message[0] != '\0') {
        cJSON_AddStringToObject(root, "error_message", error_message);
    } else {
        cJSON_AddStringToObject(root, "error_message", "");
    }
    
    // 发送响应
    const char *response = cJSON_Print(root);
    httpd_resp_sendstr(req, response);
    
    // 释放资源
    cJSON_Delete(root);
    free((void *)response);
    
    return ESP_OK;
}

static httpd_uri_t wlan_general = {
    .uri = "/wlan_general",
    .method = HTTP_GET,
    .handler = wlan_general_get_handler,
    /* Let's pass response string in user context to demonstrate it's usage */
    .user_ctx = NULL
};

static httpd_uri_t wlan_general_post = {
    .uri = "/wlan_general",
    .method = HTTP_POST,
    .handler = wlan_general_post_handler,
    /* Let's pass response string in user context to demonstrate it's usage */
    .user_ctx = NULL
};

static httpd_uri_t wlan_advance = {
    .uri = "/wlan_advance",
    .method = HTTP_GET,
    .handler = wlan_advance_get_handler,
    /* Let's pass response string in user context to demonstrate it's usage */
    .user_ctx = NULL
};

static httpd_uri_t wlan_advance_post = {
    .uri = "/wlan_advance",
    .method = HTTP_POST,
    .handler = wlan_advance_post_handler,
    /* Let's pass response string in user context to demonstrate it's usage */
    .user_ctx = NULL
};

static httpd_uri_t system_station_get = {
    .uri = "/system/station_state",
    .method = HTTP_GET,
    .handler = system_station_get_handler,
    /* Let's pass response string in user context to demonstrate it's usage */
    .user_ctx = NULL
};

static httpd_uri_t system_station_delete_device_post = {
    .uri = "/system/station_state/delete_device",
    .method = HTTP_POST,
    .handler = system_station_delete_device_post_handler,
    /* Let's pass response string in user context to demonstrate it's usage */
    .user_ctx = NULL
};

static httpd_uri_t system_station_change_name_post = {
    .uri = "/system/station_state/change_name",
    .method = HTTP_POST,
    .handler = system_station_change_name_post_handler,
    /* Let's pass response string in user context to demonstrate it's usage */
    .user_ctx = NULL
};

static httpd_uri_t sensors_data_get = {
    .uri       = "/sensors/data",
    .method    = HTTP_GET,
    .handler   = sensors_data_get_handler,
    .user_ctx  = NULL
};

static httpd_uri_t wifi_sta_get = {
    .uri = "/wifi_sta",
    .method = HTTP_GET,
    .handler = wifi_sta_get_handler,
    .user_ctx = NULL
};

static httpd_uri_t wifi_sta_post = {
    .uri = "/wifi_sta",
    .method = HTTP_POST,
    .handler = wifi_sta_post_handler,
    .user_ctx = NULL
};

static httpd_uri_t network_mode_get = {
    .uri = "/network_mode",
    .method = HTTP_GET,
    .handler = network_mode_get_handler,
    .user_ctx = NULL
};

static httpd_uri_t mqtt_topics_uri = {
    .uri       = "/api/mqtt/topics",
    .method    = HTTP_GET,
    .handler   = mqtt_topics_get_handler,
    .user_ctx  = NULL
};

static httpd_uri_t mqtt_add_topic_uri = {
    .uri       = "/api/mqtt/topics/add",
    .method    = HTTP_POST,
    .handler   = mqtt_topic_add_handler,
    .user_ctx  = NULL
};

static httpd_uri_t mqtt_delete_topic_uri = {
    .uri       = "/api/mqtt/topics/delete",
    .method    = HTTP_POST,
    .handler   = mqtt_topic_delete_handler,
    .user_ctx  = NULL
};

static httpd_uri_t mqtt_publish_uri = {
    .uri       = "/api/mqtt/publish",
    .method    = HTTP_POST,
    .handler   = mqtt_publish_handler,
    .user_ctx  = NULL
};

static httpd_uri_t mqtt_settings_uri = {
    .uri       = "/api/mqtt/settings",
    .method    = HTTP_GET,
    .handler   = mqtt_settings_get_handler,
    .user_ctx  = NULL
};

static httpd_uri_t mqtt_settings_save_uri = {
    .uri       = "/api/mqtt/settings/save",
    .method    = HTTP_POST,
    .handler   = mqtt_settings_save_handler,
    .user_ctx  = NULL
};

static httpd_uri_t mqtt_status_uri = {
    .uri       = "/api/mqtt/status",
    .method    = HTTP_GET,
    .handler   = mqtt_status_get_handler,
    .user_ctx  = NULL
};

static httpd_handle_t start_webserver(const char *base_path)
{
    ctx_info_t *ctx_info = calloc(1, sizeof(ctx_info_t));
    REST_CHECK(base_path, "wrong base path", err);
    ctx_info->rest_context = calloc(1, sizeof(rest_server_context_t));
    REST_CHECK(ctx_info->rest_context, "No memory for rest context", err);
    strlcpy(ctx_info->rest_context->base_path, base_path, sizeof(ctx_info->rest_context->base_path));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 20;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);

    if (httpd_start(&server, &config) == ESP_OK) {

        // Set URI handlers & Add user_ctx
        ESP_LOGI(TAG, "Registering URI handlers");
        wlan_general.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &wlan_general);
        wlan_general_post.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &wlan_general_post);
        wlan_advance.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &wlan_advance);
        wlan_advance_post.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &wlan_advance_post);
        system_station_get.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &system_station_get);
        system_station_delete_device_post.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &system_station_delete_device_post);
        system_station_change_name_post.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &system_station_change_name_post);
        sensors_data_get.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &sensors_data_get);
        wifi_sta_get.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &wifi_sta_get);
        wifi_sta_post.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &wifi_sta_post);
        network_mode_get.user_ctx = ctx_info;
        httpd_register_uri_handler(server, &network_mode_get);

        httpd_register_uri_handler(server, &mqtt_topics_uri);
        
        httpd_register_uri_handler(server, &mqtt_add_topic_uri);
        
        httpd_register_uri_handler(server, &mqtt_delete_topic_uri);
        
        httpd_register_uri_handler(server, &mqtt_publish_uri);
        
        httpd_register_uri_handler(server, &mqtt_settings_uri);
        httpd_register_uri_handler(server, &mqtt_settings_save_uri);
        httpd_register_uri_handler(server, &mqtt_status_uri);

        httpd_uri_t common_get_uri = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = rest_common_get_handler,
            .user_ctx = ctx_info
        };
        httpd_register_uri_handler(server, &common_get_uri);

        return server;
    }

    ESP_LOGE(TAG, "Error starting server!");
    return NULL;
err:
    return NULL;
}

static void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}

static bool s_if_init = false;

esp_err_t modem_http_get_nvs_wifi_config(modem_wifi_config_t *wifi_config)
{
    char str[64] = "";
    size_t str_size = sizeof(str);

    esp_err_t err = from_nvs_get_value("ssid", str, &str_size);
    if ( err == ESP_OK ) {
        strncpy(wifi_config->ssid, str, str_size);
    }
    str_size = sizeof(str);

    err = from_nvs_get_value("password", str, &str_size);
    if ( err == ESP_OK ) {
        strncpy(wifi_config->password, str, sizeof(wifi_config->password));
    }
    str_size = sizeof(str);

    err = from_nvs_get_value("auth_mode", str, &str_size);
    if ( err == ESP_OK ) {
         if ( !strcmp(str, "OPEN") ) {
            wifi_config->authmode = WIFI_AUTH_OPEN;
        } else if ( !strcmp(str, "WEP") ) {
            wifi_config->authmode = WIFI_AUTH_WEP;
        } else if ( !strcmp(str, "WPA2_PSK") ) {
            wifi_config->authmode = WIFI_AUTH_WPA2_PSK;
        } else if ( !strcmp(str, "WPA_WPA2_PSK") ) {
            wifi_config->authmode = WIFI_AUTH_WPA_WPA2_PSK;
        } else {
            ESP_LOGE(TAG, "auth_mode %s is not define", str);
        }
    }
    str_size = sizeof(str);

    err = from_nvs_get_value("channel", str, &str_size);
    if ( err == ESP_OK ) {
        wifi_config->channel = atoi(str);
    }
    str_size = sizeof(str);

    from_nvs_get_value("hide_ssid", str, &str_size);
    if ( err == ESP_OK ) {
        if ( !strcmp(str, "true") ) {
            wifi_config->ssid_hidden = 1;
        } else {
            wifi_config->ssid_hidden = 0;
        }
    }
    str_size = sizeof(str);

    err = from_nvs_get_value("bandwidth", str, &str_size);
    if ( err == ESP_OK ) {
        if (!strcmp(str, "40")) {
            wifi_config->bandwidth = WIFI_BW_HT40;
        } else {
            wifi_config->bandwidth = WIFI_BW_HT20;
        }
    }

    err = from_nvs_get_value("max_connection", str, &str_size);
    if ( err == ESP_OK ) {
        wifi_config->max_connection = atoi(str);
    }

    return ESP_OK;
}

static esp_err_t init_fs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_EXAMPLE_WEB_MOUNT_POINT,
        .partition_label = NULL,
        /*Maximum files that could be open at the same time.*/
        .max_files = 5,
        .format_if_mount_failed = true
    };
    /*Register and mount SPIFFS to VFS with given path prefix.*/
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    return ESP_OK;
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
        sta_remove_node(event->mac);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
        stalist_add_node(event->mac);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_AP_STAIPASSIGNED) {
        stalist_update();
    }
}

static httpd_handle_t s_server = NULL;

esp_err_t modem_http_deinit(httpd_handle_t server)
{
    if(s_if_init == true){
        s_modem_wifi_config = NULL;
        stop_webserver(server);
        s_if_init = false;
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t modem_http_init(modem_wifi_config_t *wifi_config)
{
    if(s_if_init == false){
        s_modem_wifi_config = wifi_config;
        SLIST_INIT(&s_sta_list_head);
        /* Start the server for the first time */
        ESP_ERROR_CHECK(init_fs());
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
        s_server = start_webserver(CONFIG_EXAMPLE_WEB_MOUNT_POINT);
        ESP_LOGI(TAG, "Starting webserver");
        s_sta_node_mutex = xSemaphoreCreateMutex();
        STA_CHECK(s_sta_node_mutex != NULL, "sensor_node xSemaphoreCreateMutex failed", ESP_FAIL);
        s_if_init = true;
        return ESP_OK;
    }
    ESP_LOGI(TAG, "http server already initialized");
    return ESP_FAIL;
}

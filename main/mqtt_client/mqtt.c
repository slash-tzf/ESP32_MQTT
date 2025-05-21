#include <string.h>
#include "mqtt_client.h"
#include "mqtt.h"
#include "esp_log.h"
#include "json_wrapper.h"
#include "ota.h"
#include "nvs_flash.h"
#include "nvs.h"
static const char *TAG = "MQTT";

#define MQTT_BROKER_URI         CONFIG_MQTT_BROKER_URI
#define MQTT_BROKER_USERNAME    CONFIG_MQTT_BROKER_USERNAME
#define MQTT_BROKER_PASSWORD    CONFIG_MQTT_BROKER_PASSWORD
#define MQTT_SUBSCRIBE_TOPIC    CONFIG_MQTT_SUBSCRIBE_TOPIC
#define MQTT_PUBLISH_TOPIC      CONFIG_MQTT_PUBLISH_TOPIC
#define MQTT_PUBLISH_DATA_TOPIC CONFIG_MQTT_DATA_TOPIC
#define MQTT_OTA_TOPIC          CONFIG_MQTT_OTA_TOPIC

#define JSON_BUFFER_SIZE        2048
#define MAX_MQTT_TOPICS         20
#define MAX_TOPIC_LENGTH        64
#define NVS_MQTT_NAMESPACE      "mqtt_topics"
#define NVS_TOPIC_COUNT_KEY     "topic_count"
#define NVS_TOPIC_KEY_PREFIX    "topic_"

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static char s_topics[MAX_MQTT_TOPICS][MAX_TOPIC_LENGTH];
static int s_topic_count = 0;

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        esp_mqtt_client_subscribe(client, MQTT_SUBSCRIBE_TOPIC, 0);
        esp_mqtt_client_subscribe(client, MQTT_OTA_TOPIC, 0);
        ESP_LOGI(TAG, "Subscribed to topics: %s, %s", MQTT_SUBSCRIBE_TOPIC, MQTT_OTA_TOPIC);
        
        // 加载并订阅保存在NVS中的主题
        mqtt_load_topics_from_nvs();
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, MQTT_PUBLISH_TOPIC, "test", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        if (strncmp(event->topic, MQTT_OTA_TOPIC, strlen(MQTT_OTA_TOPIC)) == 0) {
            mqtt_ota_handler((const char*)event->data, event->data_len);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

esp_mqtt_client_handle_t mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.username = MQTT_BROKER_USERNAME,
        .credentials.authentication.password = MQTT_BROKER_PASSWORD,
        //.session.protocol_ver = MQTT_PROTOCOL_V_5,
        // .credentials.client_id = "ESP32-bupt",
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);
    
    return s_mqtt_client;
}

esp_err_t mqtt_publish_data_model(esp_mqtt_client_handle_t client, 
                                 const data_model_t *model, 
                                 const char *topic)
{
    if (client == NULL || model == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 使用默认主题如果未指定
    if (topic == NULL) {
        topic = MQTT_PUBLISH_DATA_TOPIC;
    }
    
    // 创建JSON字符串缓冲区
    char json_buffer[JSON_BUFFER_SIZE];
    
    // 将数据模型转换为JSON
    esp_err_t ret = json_generate_from_data_model(model, json_buffer, JSON_BUFFER_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "生成JSON数据失败: %d", ret);
        return ret;
    }
    
    // 发布到MQTT
    int msg_id = esp_mqtt_client_publish(client, topic, json_buffer, strlen(json_buffer), 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "发布数据失败");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "发布数据成功，msg_id=%d", msg_id);
    return ESP_OK;
}

esp_err_t mqtt_publish_sensor_data(esp_mqtt_client_handle_t client, 
                                  const sensor_data_t *sensor_data, 
                                  const char *topic)
{
    if (client == NULL || sensor_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 使用默认主题如果未指定
    if (topic == NULL) {
        topic = MQTT_PUBLISH_DATA_TOPIC "/sensors";
    }
    
    // 创建JSON字符串缓冲区
    char json_buffer[JSON_BUFFER_SIZE];
    
    // 将传感器数据转换为JSON
    esp_err_t ret = json_generate_from_sensor_data(sensor_data, json_buffer, JSON_BUFFER_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "生成传感器JSON数据失败: %d", ret);
        return ret;
    }
    
    // 发布到MQTT
    int msg_id = esp_mqtt_client_publish(client, topic, json_buffer, strlen(json_buffer), 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "发布传感器数据失败");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "发布传感器数据成功，msg_id=%d", msg_id);
    return ESP_OK;
}

esp_err_t mqtt_publish_gps_data(esp_mqtt_client_handle_t client, 
                               const gps_data_t *gps_data, 
                               const char *topic)
{
    if (client == NULL || gps_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 使用默认主题如果未指定
    if (topic == NULL) {
        topic = MQTT_PUBLISH_DATA_TOPIC "/gps";
    }
    
    // 创建JSON字符串缓冲区
    char json_buffer[JSON_BUFFER_SIZE];
    
    // 将GPS数据转换为JSON
    esp_err_t ret = json_generate_from_gps_data(gps_data, json_buffer, JSON_BUFFER_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "生成GPS JSON数据失败: %d", ret);
        return ret;
    }
    
    // 发布到MQTT
    int msg_id = esp_mqtt_client_publish(client, topic, json_buffer, strlen(json_buffer), 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "发布GPS数据失败");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "发布GPS数据成功，msg_id=%d", msg_id);
    return ESP_OK;
}

esp_mqtt_client_handle_t mqtt_get_client(void)
{
    return s_mqtt_client;
}

esp_err_t mqtt_reconnect(void)
{
    ESP_LOGI(TAG, "尝试重新连接MQTT客户端");
    
    // 如果客户端存在，先停止它
    if (s_mqtt_client != NULL) {
        ESP_LOGI(TAG, "停止当前MQTT连接");
        esp_err_t err = esp_mqtt_client_stop(s_mqtt_client);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "停止MQTT客户端失败: %s", esp_err_to_name(err));
            // 继续执行，不返回错误
        }
        
        // 销毁旧的客户端
        err = esp_mqtt_client_destroy(s_mqtt_client);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "销毁MQTT客户端失败: %s", esp_err_to_name(err));
            // 继续执行，不返回错误
        }
        
        s_mqtt_client = NULL;
    }
    
    // 重新创建并启动客户端
    s_mqtt_client = mqtt_app_start();
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "重新创建MQTT客户端失败");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "MQTT客户端重新连接成功");
    return ESP_OK;
}


/**
 * @brief 保存MQTT主题到NVS
 * 
 * @return esp_err_t ESP_OK成功，其他值失败
 */
static esp_err_t mqtt_save_topics_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    // 打开NVS命名空间
    err = nvs_open(NVS_MQTT_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开NVS命名空间失败: %s", esp_err_to_name(err));
        return err;
    }
    
    // 保存主题数量
    err = nvs_set_i32(nvs_handle, NVS_TOPIC_COUNT_KEY, s_topic_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "保存主题数量失败: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // 保存每个主题
    char key[32];
    for (int i = 0; i < s_topic_count; i++) {
        snprintf(key, sizeof(key), "%s%d", NVS_TOPIC_KEY_PREFIX, i);
        err = nvs_set_str(nvs_handle, key, s_topics[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "保存主题 %s 失败: %s", s_topics[i], esp_err_to_name(err));
            nvs_close(nvs_handle);
            return err;
        }
    }
    
    // 提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "提交NVS更改失败: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "成功保存 %d 个主题到NVS", s_topic_count);
    return ESP_OK;
}

esp_err_t mqtt_load_topics_from_nvs(void)
{
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT客户端未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    // 打开NVS命名空间
    err = nvs_open(NVS_MQTT_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "未找到MQTT主题命名空间，无已保存主题");
            return ESP_OK;
        }
        ESP_LOGE(TAG, "打开NVS命名空间失败: %s", esp_err_to_name(err));
        return err;
    }
    
    // 获取主题数量
    int32_t topic_count = 0;
    err = nvs_get_i32(nvs_handle, NVS_TOPIC_COUNT_KEY, &topic_count);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "未找到主题计数，无已保存主题");
            nvs_close(nvs_handle);
            return ESP_OK;
        }
        ESP_LOGE(TAG, "获取主题数量失败: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // 清空现有主题缓存
    s_topic_count = 0;
    memset(s_topics, 0, sizeof(s_topics));
    
    // 加载每个主题并订阅
    char key[32];
    size_t required_size;
    for (int i = 0; i < topic_count && i < MAX_MQTT_TOPICS; i++) {
        snprintf(key, sizeof(key), "%s%d", NVS_TOPIC_KEY_PREFIX, i);
        
        // 获取主题字符串长度
        err = nvs_get_str(nvs_handle, key, NULL, &required_size);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "获取主题%d长度失败: %s", i, esp_err_to_name(err));
            continue;
        }
        
        // 检查主题长度是否超出限制
        if (required_size > MAX_TOPIC_LENGTH) {
            ESP_LOGW(TAG, "主题%d长度(%zu)超过限制(%d)", i, required_size, MAX_TOPIC_LENGTH);
            continue;
        }
        
        // 获取主题字符串
        err = nvs_get_str(nvs_handle, key, s_topics[s_topic_count], &required_size);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "获取主题%d失败: %s", i, esp_err_to_name(err));
            continue;
        }
        
        // 订阅主题
        int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, s_topics[s_topic_count], 0);
        if (msg_id < 0) {
            ESP_LOGW(TAG, "订阅主题 %s 失败", s_topics[s_topic_count]);
            continue;
        }
        
        ESP_LOGI(TAG, "已自动订阅主题: %s", s_topics[s_topic_count]);
        s_topic_count++;
    }
    
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "成功从NVS加载并订阅 %d 个主题", s_topic_count);
    return ESP_OK;
}

esp_err_t mqtt_get_subscribed_topics(char topics[][64], int max_topics, int *topic_count)
{
    if (topics == NULL || topic_count == NULL || max_topics <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *topic_count = 0;
    
    // 复制主题到输出数组
    for (int i = 0; i < s_topic_count && i < max_topics; i++) {
        strncpy(topics[i], s_topics[i], 64 - 1);
        topics[i][64 - 1] = '\0';  // 确保字符串以空字符结尾
        (*topic_count)++;
    }
    
    return ESP_OK;
}

esp_err_t mqtt_subscribe_topic(const char *topic, bool save_to_nvs)
{
    if (topic == NULL || strlen(topic) == 0 || strlen(topic) >= MAX_TOPIC_LENGTH) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT客户端未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 检查主题是否已存在
    for (int i = 0; i < s_topic_count; i++) {
        if (strcmp(s_topics[i], topic) == 0) {
            ESP_LOGI(TAG, "主题 %s 已订阅", topic);
            return ESP_OK;
        }
    }
    
    // 检查主题数量是否已达上限
    if (s_topic_count >= MAX_MQTT_TOPICS) {
        ESP_LOGE(TAG, "主题数量已达上限 %d", MAX_MQTT_TOPICS);
        return ESP_ERR_NO_MEM;
    }
    
    // 订阅主题
    int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, topic, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "订阅主题 %s 失败", topic);
        return ESP_FAIL;
    }
    
    // 将主题添加到缓存
    strncpy(s_topics[s_topic_count], topic, MAX_TOPIC_LENGTH - 1);
    s_topics[s_topic_count][MAX_TOPIC_LENGTH - 1] = '\0';  // 确保字符串以空字符结尾
    s_topic_count++;
    
    ESP_LOGI(TAG, "成功订阅主题: %s", topic);
    
    // 如果需要，保存到NVS
    if (save_to_nvs) {
        return mqtt_save_topics_to_nvs();
    }
    
    return ESP_OK;
}

esp_err_t mqtt_unsubscribe_topic(const char *topic, bool remove_from_nvs)
{
    if (topic == NULL || strlen(topic) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT客户端未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 查找主题索引
    int index = -1;
    for (int i = 0; i < s_topic_count; i++) {
        if (strcmp(s_topics[i], topic) == 0) {
            index = i;
            break;
        }
    }
    
    if (index == -1) {
        ESP_LOGW(TAG, "未找到主题 %s", topic);
        return ESP_ERR_NOT_FOUND;
    }
    
    // 取消订阅主题
    int msg_id = esp_mqtt_client_unsubscribe(s_mqtt_client, topic);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "取消订阅主题 %s 失败", topic);
        return ESP_FAIL;
    }
    
    // 从缓存中移除主题
    if (index < s_topic_count - 1) {
        // 将后面的主题前移
        for (int i = index; i < s_topic_count - 1; i++) {
            strncpy(s_topics[i], s_topics[i + 1], MAX_TOPIC_LENGTH);
        }
    }
    s_topic_count--;
    
    ESP_LOGI(TAG, "成功取消订阅主题: %s", topic);
    
    // 如果需要，更新NVS
    if (remove_from_nvs) {
        return mqtt_save_topics_to_nvs();
    }
    
    return ESP_OK;
}

esp_err_t mqtt_publish_message(const char *topic, const char *message, int qos)
{
    if (topic == NULL || message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT客户端未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 限制QoS值范围
    if (qos < 0) qos = 0;
    if (qos > 2) qos = 2;
    
    // 发布消息
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, message, strlen(message), qos, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "发布消息到主题 %s 失败", topic);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "成功发布消息到主题 %s, msg_id=%d", topic, msg_id);
    return ESP_OK;
}

#include <string.h>
#include "mqtt_client.h"
#include "mqtt.h"
#include "esp_log.h"
#include "json_wrapper.h"

static const char *TAG = "MQTT";

#define MQTT_BROKER_URI         CONFIG_MQTT_BROKER_URI
#define MQTT_BROKER_USERNAME    CONFIG_MQTT_BROKER_USERNAME
#define MQTT_BROKER_PASSWORD    CONFIG_MQTT_BROKER_PASSWORD
#define MQTT_SUBSCRIBE_TOPIC    CONFIG_MQTT_SUBSCRIBE_TOPIC
#define MQTT_PUBLISH_TOPIC      CONFIG_MQTT_PUBLISH_TOPIC
#define MQTT_PUBLISH_DATA_TOPIC CONFIG_MQTT_DATA_TOPIC

#define JSON_BUFFER_SIZE        2048

static esp_mqtt_client_handle_t s_mqtt_client = NULL;

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
        msg_id = esp_mqtt_client_subscribe(client, MQTT_SUBSCRIBE_TOPIC, 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
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

#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "sdkconfig.h"
#include "string.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "gps.h"


static const char *TAG = "GPS";

#define UART_NUM                CONFIG_GNSS_UART_NUM
#define UART_RX_PIN             CONFIG_GNSS_UART_RX
#define UART_TX_PIN             CONFIG_GNSS_UART_TX
#define UART_RX_BUF_SIZE        (2048)
#define ESP_GPS_BUF_SIZE        2048
#define GPS_EVENT_LOOP_QUEUE_SIZE (16)
//AT commands For GNSS
//more details in SIM76XX AT command manual
#define GNSSPWRON       "AT+CGNSSPWR=1\r\n"
#define GNSSPWROFF      "AT+CGNSSPWR=0\r\n"
#define GPSINFO         "AT+CGPSINFO=15\r\n"
#define GNSSPORTSWITCH  "AT+CGNSSPORTSWITCH=1,0\r\n"
#define LBSINFO         "AT+CLBS=4,,,,2\r\n"


ESP_EVENT_DEFINE_BASE(ESP_GPS_EVENT);



// GPS模块内部结构体
typedef struct {
    QueueHandle_t event_queue;         // UART事件队列句柄
    esp_event_loop_handle_t event_loop_hdl;  // 事件循环句柄
    TaskHandle_t tsk_hdl;              // GPS解析任务句柄
    uart_port_t uart_port;             // UART端口号
    uint8_t *buffer;                   // 运行时缓冲区
    gps_info_t gps_data;               // GPS数据
} esp_gps_t;

/**
 * @brief 将GPS坐标从度分格式(ddmm.mmmm)转换为十进制度格式
 * 
 * @param coord_ddmm 度分格式的坐标
 * @return double 十进制度格式的坐标
 */
double convert_to_decimal_degrees(double coord_ddmm)
{
    int degrees = (int)(coord_ddmm / 100.0);
    double minutes = coord_ddmm - degrees * 100.0;
    return degrees + minutes / 60.0;
}


/**
 * @brief 将GPS的ddmmyy日期格式转换为yyyy/mm/dd格式
 * 
 * @param date_in GPS日期字符串，格式为ddmmyy
 * @param date_out 输出缓冲区，至少15字节
 * @return int 0表示成功，-1表示失败
 */
int convert_gps_date(const char *date_in, char *date_out) {
    if (date_in == NULL || date_out == NULL || strlen(date_in) < 6) {
        return -1;
    }
    
    // 提取日、月、年
    char day[3] = {0};
    char month[3] = {0};
    char year[3] = {0};
    
    // 复制前两位作为日
    strncpy(day, date_in, 2);
    // 复制中间两位作为月
    strncpy(month, date_in + 2, 2);
    // 复制最后两位作为年
    strncpy(year, date_in + 4, 2);
    
    // 格式化为yyyy/mm/dd，年份前加"20"表示21世纪
    sprintf(date_out, "20%s/%s/%s", year, month, day);
    
    return 0;
}

/**
 * @brief 将GPS的hhmmss.ss时间格式转换为hh:mm:ss格式
 * 
 * @param time_in GPS时间字符串，格式为hhmmss.ss
 * @param time_out 输出缓冲区，至少9字节
 * @return int 0表示成功，-1表示失败
 */
int convert_gps_time(const char *time_in, char *time_out) {
    if (time_in == NULL || time_out == NULL || strlen(time_in) < 6) {
        return -1;
    }
    
    // 提取时、分、秒
    char hour[3] = {0};
    char minute[3] = {0};
    char second[3] = {0};
    
    // 复制前两位作为时
    strncpy(hour, time_in, 2);
    // 复制中间两位作为分
    strncpy(minute, time_in + 2, 2);
    // 复制后两位作为秒，忽略小数点后的部分
    strncpy(second, time_in + 4, 2);
    
    // 格式化为hh:mm:ss
    sprintf(time_out, "%s:%s:%s", hour, minute, second);
    
    return 0;
}


/**
 * @brief 解析CGPSINFO响应字符串
 * 
 * @param line CGPSINFO响应字符串
 * @param gps_info GPS信息结构体指针
 * @return int 0表示成功解析有效数据，1表示成功解析但无GPS信号，负数表示解析失败
 */
int parse_gps_info(const char *line, gps_info_t *gps_info)
{
    if (!line || !gps_info) {
        return -1;
    }
    
    // 检查是否为CGPSINFO响应
    const char *cgpsinfo_start = strstr(line, "+CGPSINFO:");
    if (cgpsinfo_start == NULL) {
        return -2;
    }
    
    // 初始化为无效状态
    gps_info->valid = 0;
    
    // 跳过"+CGPSINFO:"前缀
    const char *data_start = cgpsinfo_start + strlen("+CGPSINFO:");
    
    // 检查是否为无信号响应（+CGPSINFO: ,,,,,,,,）
    if (strstr(data_start, ",,,,,,,,") != NULL) {
        //ESP_LOGW(TAG, "GPS无定位信号");
        // 清空所有数据字段
        memset(gps_info, 0, sizeof(gps_info_t));
        // 明确标记为无效
        gps_info->valid = 0;
        return 1;  // 返回1表示成功解析但无信号
    }
    
    // 分配临时存储空间
    char buffer[256] = {0};
    strncpy(buffer, data_start, sizeof(buffer) - 1);
    
    // 使用strtok分割字符串
    char *tokens[9] = {NULL};
    char *token = strtok(buffer, ",");
    int i = 0;
    
    // 提取所有的字段
    while (token != NULL && i < 9) {
        tokens[i++] = token;
        token = strtok(NULL, ",");
    }
    
    // 检查是否有足够的字段
    if (i != 9) {
        ESP_LOGW(TAG, "GPS信息字段不完整: 预期9个字段，实际获得%d个", i);
        return -3;
    }
    
    // 检查第一个字段是否为空，表示无经纬度数据
    if (strlen(tokens[0]) == 0) {
        ESP_LOGW(TAG, "GPS无定位数据");
        memset(gps_info, 0, sizeof(gps_info_t));
        gps_info->valid = 0;
        return 1;  // 无GPS定位信息但解析成功
    }
    
    // 解析所有字段
    gps_info->latitude = atof(tokens[0]);
    gps_info->ns_indicator = tokens[1][0]; // 'N' 或 'S'
    gps_info->longitude = atof(tokens[2]);
    gps_info->ew_indicator = tokens[3][0]; // 'E' 或 'W'
    
    // 复制日期和时间字符串
    convert_gps_date(tokens[4], gps_info->date);
    convert_gps_time(tokens[5], gps_info->utc_time);
    
    gps_info->altitude = atof(tokens[6]);
    gps_info->speed = atof(tokens[7]);
    gps_info->course = atof(tokens[8]);
    
    // 设置为有效状态
    gps_info->valid = 1;
    gps_info->data_source = FROM_GNSS; // 设置数据来源为GNSS
    // 调试输出
    // ESP_LOGI(TAG, "解析GPS数据成功: 纬度=%.6f%c, 经度=%.6f%c, 高度=%.1f米, 速度=%.1f节, 航向=%.1f度, 日期=%s, 时间=%s, 数据来源：GNSS",
    //          gps_info->latitude, gps_info->ns_indicator,
    //          gps_info->longitude, gps_info->ew_indicator,
    //          gps_info->altitude, gps_info->speed, gps_info->course,
    //          gps_info->date, gps_info->utc_time);
    
    return 0;
}


/**
 * @brief 解析LBS基站定位响应
 * 
 * @param line LBS响应字符串
 * @param gps_info GPS信息结构体指针，用于存储解析结果
 * @return int 0表示成功解析有效数据，非零表示解析失败
 */
int parse_lbs_info(const char *line, gps_info_t *gps_info)
{
    if (!line || !gps_info) {
        return -1;
    }
    
    // 检查是否为CLBS响应
    const char *clbs_start = strstr(line, "+CLBS:");
    if (clbs_start == NULL) {
        return -2;
    }
    
    // 跳过"+CLBS:"前缀
    const char *data_start = clbs_start + strlen("+CLBS:");
    
    // 分配临时存储空间
    char buffer[256] = {0};
    strncpy(buffer, data_start, sizeof(buffer) - 1);
    
    // 使用strtok分割字符串
    char *tokens[6] = {NULL};
    char *token = strtok(buffer, ",");
    int i = 0;
    
    // 提取所有的字段
    while (token != NULL && i < 6) {
        tokens[i++] = token;
        token = strtok(NULL, ",");
    }
    
    // 检查是否有足够的字段
    if (i != 6) {
        ESP_LOGW(TAG, "LBS信息字段不完整: 预期6个字段，实际获得%d个", i);
        return -3;
    }
    
    // 检查第一个字段是否为0，0表示成功
    int status = atoi(tokens[0]);
    if (status != 0) {
        ESP_LOGW(TAG, "LBS定位失败，状态码: %d", status);
        memset(gps_info, 0, sizeof(gps_info_t));
        gps_info->valid = 0;
        return -4;
    }
    
    // 解析经纬度（注意LBS返回的是十进制格式，不是度分格式）
    double latitude = atof(tokens[1]);
    double longitude = atof(tokens[2]);
    //int accuracy = atoi(tokens[3]);
    
    // 填充GPS信息结构体
    memset(gps_info, 0, sizeof(gps_info_t)); // 先清空结构体
    
    // 直接使用十进制度格式的经纬度
    // 我们需要将十进制度转换为ddmm.mmmm格式存储，以保持和GPS格式一致
    int lat_deg = (int)latitude;
    double lat_min = (latitude - lat_deg) * 60.0;
    gps_info->latitude = lat_deg * 100.0 + lat_min;
    
    int lon_deg = (int)longitude;
    double lon_min = (longitude - lon_deg) * 60.0;
    gps_info->longitude = lon_deg * 100.0 + lon_min;
    
    // 设置北/南、东/西指示符
    gps_info->ns_indicator = (latitude >= 0) ? 'N' : 'S';
    gps_info->ew_indicator = (longitude >= 0) ? 'E' : 'W';
    
    // 设置其他字段
    // 对于LBS定位，我们没有高度、速度和航向信息，设为0
    gps_info->altitude = 0;
    gps_info->speed = 0;
    gps_info->course = 0;
    gps_info->data_source = FROM_LBS;

    // 设置日期和时间
    memcpy(gps_info->date, tokens[4], sizeof(gps_info->date)-1);
    memcpy(gps_info->utc_time, tokens[5], sizeof(gps_info->utc_time)-1);
    // 设置为有效状态
    gps_info->valid = 1;
    
    // 调试输出
    // ESP_LOGI(TAG, "解析LBS数据成功: 纬度=%.6f%c (%.6f), 经度=%.6f%c (%.6f), 精度=%d米, 日期=%s, 时间=%s",
    //          gps_info->latitude, gps_info->ns_indicator, latitude,
    //          gps_info->longitude, gps_info->ew_indicator, longitude,
    //          accuracy, gps_info->date, gps_info->utc_time);
    
    return 0;
}


static void GNSS_module_init(void *arg){
    esp_gps_t *esp_gps = (esp_gps_t *)arg;
    uart_tx_chars(esp_gps->uart_port,"\r\n",strlen("\r\n"));
    vTaskDelay(1500);
    uart_tx_chars(esp_gps->uart_port,GNSSPWRON,strlen(GNSSPWRON));
    vTaskDelay(2000);
    uart_tx_chars(esp_gps->uart_port, GNSSPORTSWITCH, strlen(GNSSPORTSWITCH));
    vTaskDelay(1000);
    uart_tx_chars(esp_gps->uart_port, GPSINFO, strlen(GPSINFO));
    vTaskDelay(1000);

}

static void uart_get_lbs(void *arg){
    esp_gps_t *esp_gps = (esp_gps_t *)arg;
    uart_tx_chars(esp_gps->uart_port,LBSINFO,strlen(LBSINFO));
    vTaskDelay(1000);
}

/**
 * @brief 处理接收到+CGPSINFO响应的函数
 * 
 * @param buffer 接收缓冲区
 * @param length 数据长度
 */
static void process_cgps_info(void *arg, size_t length)
{
    esp_gps_t *esp_gps = (esp_gps_t *)arg;
    // 确保字符串以NULL结尾
    esp_gps->buffer[length] = '\0';
    //ESP_LOGI(TAG, "接收到数据: %s", buffer);
    
    // 解析GPS信息
    int result = parse_gps_info((char*)(esp_gps->buffer), &esp_gps->gps_data);
    
    if (result == 0) {
        // 解析成功且有有效GPS信息
        esp_event_post_to(esp_gps->event_loop_hdl, ESP_GPS_EVENT, GPS_DATA_UPDATE, &(esp_gps->gps_data), sizeof(gps_info_t), 100 / portTICK_PERIOD_MS);
    } else if (result == 1) {
        // 解析成功但无GPS信号
        ESP_LOGW(TAG, "当前无GPS信号或正在搜索卫星,将尝试使用基站定位");
        uart_get_lbs(esp_gps);
    } else {
        // 解析失败
        ESP_LOGW(TAG, "GPS信息解析失败，错误码: %d ，将尝试使用基站定位", result);
        uart_get_lbs(esp_gps);
    }
}


/**
* @brief 处理接收到+CGPSINFO响应的函数
* 
* @param buffer 接收缓冲区
* @param length 数据长度
*/
static void process_clbs_info(void *arg, size_t length)
{
    esp_gps_t *esp_gps = (esp_gps_t *)arg;
    // 确保字符串以NULL结尾
    esp_gps->buffer[length] = '\0';
    //ESP_LOGI(TAG, "接收到数据: %s", buffer);
    
    // 解析GPS信息
    int result = parse_lbs_info((char*)(esp_gps->buffer), &esp_gps->gps_data);
    
    if (result == 0) {
        // 解析成功且有有效GPS信息
        esp_event_post_to(esp_gps->event_loop_hdl, ESP_GPS_EVENT, GPS_DATA_UPDATE, &(esp_gps->gps_data), sizeof(gps_info_t), 100 / portTICK_PERIOD_MS);
    } else if (result == 1) {
        // 解析成功但无GPS信号
        ESP_LOGW(TAG, "当前基站无定位信息");
        esp_event_post_to(esp_gps->event_loop_hdl, ESP_GPS_EVENT, GPS_DATA_ERROR,NULL, sizeof(gps_info_t), 100 / portTICK_PERIOD_MS);
    } else {
        // 解析失败
        ESP_LOGW(TAG, "基站定位信息解析失败，错误码: %d", result);
        esp_event_post_to(esp_gps->event_loop_hdl, ESP_GPS_EVENT, GPS_DATA_ERROR,NULL, sizeof(gps_info_t), 100 / portTICK_PERIOD_MS);
    }
}

static void esp_handle_uart_pattern(void *arg)
{
    esp_gps_t *esp_gps = (esp_gps_t *)arg;
    int pos = uart_pattern_pop_pos(esp_gps->uart_port);
    if (pos != -1){
        size_t buffered_size;
        uart_get_buffered_data_len(esp_gps->uart_port, &buffered_size);
        int read_len = uart_read_bytes(esp_gps->uart_port, esp_gps->buffer, buffered_size, pdMS_TO_TICKS(100));
        if (read_len > 0){
            esp_gps->buffer[read_len] = '\0';
            if (strstr((char*)esp_gps->buffer, "+CGPSINFO:") != NULL) {
                process_cgps_info(esp_gps, read_len);
            }
            else if (strstr((char*)esp_gps->buffer, "+CLBS:") != NULL) {
                process_clbs_info(esp_gps, read_len);
            }
        }
    } else {
        //ESP_LOGE(TAG, "检测到模式但位置无效");
    }
}

static void ESP_GPS_TASK(void *arg) 
{   
    esp_gps_t *esp_gps = (esp_gps_t *)arg;
    GNSS_module_init(esp_gps);
    uart_event_t event;
    while (1) {
                // 等待UART事件，无限等待
        if (xQueueReceive(esp_gps->event_queue, (void *)&event, portMAX_DELAY)) {
            // 只处理接收到数据的事件
            switch (event.type){
            case UART_DATA:
                break;
            case UART_FIFO_OVF:
                ESP_LOGW(TAG, "HW FIFO Overflow");
                uart_flush(esp_gps->uart_port);
                xQueueReset(esp_gps->event_queue);
                break;
            case UART_BUFFER_FULL:
                ESP_LOGW(TAG, "Ring Buffer Full");
                uart_flush(esp_gps->uart_port);
                xQueueReset(esp_gps->event_queue);
                break;
            case UART_PATTERN_DET:
                esp_handle_uart_pattern(esp_gps);   
                break;
            default:
                break;
            }
        }
        esp_event_loop_run(esp_gps->event_loop_hdl, pdMS_TO_TICKS(50));
    }
    vTaskDelete(NULL);
}


esp_gps_handle_t esp_gps_init(const esp_gps_config_t *config){
    esp_gps_t *esp_gps = calloc(1, sizeof(esp_gps_t));
    if (!esp_gps) {
        ESP_LOGE(TAG, "calloc memory for esp_fps failed");
        goto err_gps;
    }
    esp_gps->buffer = calloc(1, ESP_GPS_BUF_SIZE);
    if (!esp_gps->buffer) {
        ESP_LOGE(TAG, "calloc memory for runtime buffer failed");
        goto err_buffer;
    }
    /* Set attributes */
    esp_gps->uart_port = config->uart.uart_port;
    /* Install UART friver */
    uart_config_t uart_config = {
        .baud_rate = config->uart.baud_rate,
        .data_bits = config->uart.data_bits,
        .parity = config->uart.parity,
        .stop_bits = config->uart.stop_bits,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
        if (uart_driver_install(esp_gps->uart_port, UART_RX_BUF_SIZE, 0,
                            config->uart.event_queue_size, &esp_gps->event_queue, 0) != ESP_OK) {
        ESP_LOGE(TAG, "install uart driver failed");
        goto err_uart_install;
    }
    if (uart_param_config(esp_gps->uart_port, &uart_config) != ESP_OK) {
        ESP_LOGE(TAG, "config uart parameter failed");
        goto err_uart_config;
    }
    if (uart_set_pin(esp_gps->uart_port, config->uart.tx_pin, config->uart.rx_pin,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(TAG, "config uart gpio failed");
        goto err_uart_config;
    }

    uart_enable_pattern_det_baud_intr(esp_gps->uart_port, '\n', 1, 9, 0, 0);

    uart_pattern_queue_reset(esp_gps->uart_port, config->uart.event_queue_size);
    uart_flush(esp_gps->uart_port);

    esp_event_loop_args_t loop_args = {
        .queue_size = GPS_EVENT_LOOP_QUEUE_SIZE,
        .task_name = NULL
    };
    if (esp_event_loop_create(&loop_args, &esp_gps->event_loop_hdl) != ESP_OK) {
        ESP_LOGE(TAG, "create event loop faild");
        goto err_eloop;
    }
    BaseType_t err = xTaskCreate(
                         ESP_GPS_TASK,
                         "ESP_GPS",
                         4096,
                         esp_gps,
                         10,
                         &esp_gps->tsk_hdl);
    if (err != pdTRUE) {
        ESP_LOGE(TAG, "create ESP GPS task failed");
        goto err_task_create;
    }
    ESP_LOGI(TAG, "ESP GPS init OK");
    return esp_gps;
err_task_create:
    esp_event_loop_delete(esp_gps->event_loop_hdl);
err_eloop:
err_uart_install:
    uart_driver_delete(esp_gps->uart_port);
err_uart_config:
err_buffer:
    free(esp_gps->buffer);
err_gps:
    free(esp_gps);
    return NULL;
}

esp_err_t esp_gps_deinit(esp_gps_handle_t nmea_hdl)
{
    esp_gps_t *esp_gps = (esp_gps_t *)nmea_hdl;
    vTaskDelete(esp_gps->tsk_hdl);
    esp_event_loop_delete(esp_gps->event_loop_hdl);
    uart_tx_chars(esp_gps->uart_port,GNSSPWROFF,strlen(GNSSPWROFF));
    esp_err_t err = uart_driver_delete(esp_gps->uart_port);
    free(esp_gps->buffer);
    free(esp_gps);
    return err;
}

esp_err_t esp_gps_add_handler(esp_gps_handle_t gps_hdl, esp_event_handler_t event_handler, void *handler_args)
{
    esp_gps_t *esp_gps = (esp_gps_t *)gps_hdl;
    return esp_event_handler_register_with(esp_gps->event_loop_hdl, ESP_GPS_EVENT, ESP_EVENT_ANY_ID,
                                           event_handler, handler_args);
}


esp_err_t esp_gps_remove_handler(esp_gps_handle_t gps_hdl, esp_event_handler_t event_handler)
{
    esp_gps_t *esp_gps = (esp_gps_t *)gps_hdl;
    return esp_event_handler_unregister_with(esp_gps->event_loop_hdl, ESP_GPS_EVENT, ESP_EVENT_ANY_ID, event_handler);
}


static void gps_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data){
    gps_info_t *gps_info = (gps_info_t *)event_data;
    data_model_t *data_model = data_model_get_latest();
    switch (event_id) {
        case GPS_DATA_UPDATE:
            if (gps_info->data_source == FROM_GNSS) {
                ESP_LOGI(TAG, "GPS位置: %.6f°%c, %.6f°%c, 时间: %s %s, 数据来源: GNSS",
                         convert_to_decimal_degrees(gps_info->latitude), gps_info->ns_indicator,
                         convert_to_decimal_degrees(gps_info->longitude), gps_info->ew_indicator,
                         gps_info->date, gps_info->utc_time);
            } else if (gps_info->data_source == FROM_LBS) {
                ESP_LOGI(TAG, "GPS位置: %.6f°%c, %.6f°%c ,数据来源: LBS",
                         convert_to_decimal_degrees(gps_info->latitude), gps_info->ns_indicator,
                         convert_to_decimal_degrees(gps_info->longitude), gps_info->ew_indicator);
            }
            if (data_model != NULL) {
                data_model_update_gps_data(data_model, (void *)gps_info);
            }
            break;
        case GPS_DATA_ERROR:
            ESP_LOGW(TAG, "GPS数据错误");
            break;
        default:
            ESP_LOGW(TAG, "未知事件");
            break;
    }
}

esp_err_t gps_start(){
    esp_err_t err;
    esp_gps_config_t config = ESP_GPS_CONFIG_DEFAULT();

    esp_gps_handle_t gps_hdl = esp_gps_init(&config);

    err = esp_gps_add_handler(gps_hdl, gps_event_handler, NULL);

    return err;
}

void gps_stop(esp_gps_handle_t gps_hdl){
    esp_gps_remove_handler(gps_hdl, gps_event_handler);
    esp_gps_deinit(gps_hdl);
}

esp_err_t gps_update_data_model(data_model_t *model, const gps_info_t *gps_info)
{
    if (model == NULL || gps_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 更新数据模型中的GPS数据
    return data_model_update_gps_data(model, (void *)gps_info);
}

esp_err_t gps_register_event_handler(esp_event_handler_t event_handler, void *handler_args)
{
    static esp_gps_handle_t gps_hdl = NULL;
    esp_err_t ret = ESP_OK;
    
    if (gps_hdl == NULL) {
        // 第一次调用时创建GPS句柄
        esp_gps_config_t config = ESP_GPS_CONFIG_DEFAULT();
        gps_hdl = esp_gps_init(&config);
        
        if (gps_hdl == NULL) {
            ESP_LOGE(TAG, "创建GPS句柄失败");
            return ESP_FAIL;
        }
    }
    
    // 注册事件处理程序
    ret = esp_gps_add_handler(gps_hdl, event_handler, handler_args);
    
    return ret;
}
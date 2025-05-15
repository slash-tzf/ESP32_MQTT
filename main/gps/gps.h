#ifndef GPS_H
#define GPS_H
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_types.h"
#include "esp_event.h"
#include "driver/uart.h"
#include "sdkconfig.h"
#include "data_model.h"

typedef enum {
    FROM_GNSS,  // GNSS模块
    FROM_LBS ,  // 基站定位
} gps_data_src_t;

// GPS事件类型
typedef enum {
    GPS_DATA_UPDATE,   // GPS数据已更新
    GPS_DATA_ERROR     // GPS数据解析错误
} gps_event_id_t;

typedef struct {
    double latitude;        // 纬度，格式为 ddmm.mmmmmm (度分)
    char ns_indicator;      // 北/南指示符: 'N'=北, 'S'=南
    double longitude;       // 经度，格式为 dddmm.mmmmmm (度分)
    char ew_indicator;      // 东/西指示符: 'E'=东, 'W'=西
    char date[15];           // 日期，格式为 2020/06/17 20yy/mm/dd
    char utc_time[10];      // UTC时间，格式为  09:34:16 hh:mm:ss
    float altitude;         // 高度，单位为米
    float speed;            // 地面速度，单位为节
    float course;           // 航向，单位为度
    int valid;              // 数据是否有效的标志
    gps_data_src_t data_source;        // 数据来源: FROM_GNSS 或 FROM_LBS
} gps_info_t;



typedef struct {
    struct {
        uart_port_t uart_port;        /*!< UART port number */
        uint32_t rx_pin;              /*!< UART Rx Pin number */
        uint32_t tx_pin;              /*!< UART Tx Pin number */
        uint32_t baud_rate;           /*!< UART baud rate */
        uart_word_length_t data_bits; /*!< UART data bits length */
        uart_parity_t parity;         /*!< UART parity */
        uart_stop_bits_t stop_bits;   /*!< UART stop bits length */
        uint32_t event_queue_size;    /*!< UART event queue size */
    } uart;                           /*!< UART specific configuration */
} esp_gps_config_t;


typedef void *esp_gps_handle_t;

#define ESP_GPS_CONFIG_DEFAULT()              \
    {                                               \
        .uart = {                                   \
            .uart_port = UART_NUM_1,                \
            .rx_pin = CONFIG_GNSS_UART_RX,          \
            .tx_pin = CONFIG_GNSS_UART_TX,          \
            .baud_rate = 115200,                    \
            .data_bits = UART_DATA_8_BITS,          \
            .parity = UART_PARITY_DISABLE,          \
            .stop_bits = UART_STOP_BITS_1,          \
            .event_queue_size = 16                  \
        }                                           \
    }

/**
 * @brief 启动GPS模块
 * 
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t gps_start();

/**
 * @brief 停止GPS模块
 * 
 * @param gps_hdl GPS句柄
 */
void gps_stop(esp_gps_handle_t gps_hdl);

/**
 * @brief 更新数据模型中的GPS数据
 * 
 * @param model 数据模型指针
 * @param gps_info GPS信息结构体指针
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t gps_update_data_model(data_model_t *model, const gps_info_t *gps_info);

/**
 * @brief 注册GPS事件处理函数
 * 
 * @param event_handler 事件处理函数
 * @param handler_args 事件处理函数参数
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t gps_register_event_handler(esp_event_handler_t event_handler, void *handler_args);

/**
 * @brief 将GPS坐标从度分格式(ddmm.mmmm)转换为十进制度格式
 * 
 * @param coord_ddmm 度分格式的坐标
 * @return double 十进制度格式的坐标
 */
double convert_to_decimal_degrees(double coord_ddmm);

#endif // GPS_H
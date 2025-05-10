#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "sdkconfig.h"
#include "string.h"
#include "nmea.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "gpgll.h"
#include "gpgga.h"
#include "gprmc.h"
#include "gpgsa.h"
#include "gpvtg.h"
#include "gptxt.h"
#include "gpgsv.h"
#include "gps.h"


static const char *TAG = "GPS";

#define UART_NUM                CONFIG_GNSS_UART_NUM
#define UART_RX_PIN             CONFIG_GNSS_UART_RX
#define UART_TX_PIN             CONFIG_GNSS_UART_TX
#define UART_RX_BUF_SIZE        (1024)

#define UART_QUEUE_SIZE 10
//AT commands For GNSS
//more details in SIM76XX AT command manual
#define GNSSPWRON       "AT+CGNSSPWR=1\r\n"
#define GNSSPWROFF      "AT+CGNSSPWR=0\r\n"
#define GNSSNMEA        "AT+CGNSSNMEA=1,1,1,1,1,0,0,0,0,0\r\n"
#define GNSSTST         "AT+CGNSSTST=1\r\n"
#define GNSSPORTSWITCH  "AT+CGNSSPORTSWITCH=0,1\r\n"


static QueueHandle_t uart_queue;
static char s_buf[UART_RX_BUF_SIZE + 1];
static size_t s_total_bytes;
static char *s_last_buf_end;

static nmea_gpgga_s *gpgga;
static nmea_gprmc_s *gprmc;


/**
 * 将GPS坐标格式化为单个字符串，格式为：纬度°N/S,经度°E/W
 * 例如：23.1234°N,45.5678°E
 * 
 * @param latitude GPS纬度数据结构
 * @param longitude GPS经度数据结构
 * @param buffer 输出字符串缓冲区
 * @param buffer_size 缓冲区大小
 * @return 格式化后的字符串长度
 */
int format_gps_position(const nmea_position *latitude, const nmea_position *longitude, char *buffer, size_t buffer_size)
{
    if (!latitude || !longitude || !buffer || buffer_size == 0) {
        return 0;
    }
     
     // 计算完整的度数（度+分/60）
    double lat_decimal = latitude->degrees + (latitude->minutes / 60.0);
    double lon_decimal = longitude->degrees + (longitude->minutes / 60.0);
     
     // 格式化为字符串
    return snprintf(buffer, buffer_size, "%.6f°%c,%.6f°%c", 
                    lat_decimal, (char)latitude->cardinal,
                    lon_decimal, (char)longitude->cardinal);
}


void nmea_read_line(char **out_line_buf, size_t *out_line_len, int timeout_ms)
{
    *out_line_buf = NULL;
    *out_line_len = 0;

    if (s_last_buf_end != NULL) {
        /* Data left at the end of the buffer after the last call;
         * copy it to the beginning.
         */
        size_t len_remaining = s_total_bytes - (s_last_buf_end - s_buf);
        memmove(s_buf, s_last_buf_end, len_remaining);
        s_last_buf_end = NULL;
        s_total_bytes = len_remaining;
    }

    /* Read data from the UART */
    int read_bytes = uart_read_bytes(UART_NUM,
                                     (uint8_t *) s_buf + s_total_bytes,
                                     UART_RX_BUF_SIZE - s_total_bytes, pdMS_TO_TICKS(timeout_ms));
    if (read_bytes <= 0) {
        return;
    }
    s_total_bytes += read_bytes;

    /* find start (a dollar sign) */
    char *start = memchr(s_buf, '$', s_total_bytes);
    if (start == NULL) {
        s_total_bytes = 0;
        return;
    }

    /* find end of line */
    char *end = memchr(start, '\r', s_total_bytes - (start - s_buf));
    if (end == NULL || *(++end) != '\n') {
        return;
    }
    end++;

    end[-2] = NMEA_END_CHAR_1;
    end[-1] = NMEA_END_CHAR_2;

    *out_line_buf = start;
    *out_line_len = end - start;
    if (end < s_buf + s_total_bytes) {
        /* some data left at the end of the buffer, record its position until the next call */
        s_last_buf_end = end;
    } else {
        s_total_bytes = 0;
    }
}

static void nmea_data_init(){

    uart_tx_chars(UART_NUM,"\r\n",strlen("\r\n"));
    vTaskDelay(1500);
    uart_tx_chars(UART_NUM,GNSSPWRON,strlen(GNSSPWRON));
    vTaskDelay(2000);
    uart_tx_chars(UART_NUM, GNSSTST, strlen(GNSSTST));
    vTaskDelay(1000);
    uart_tx_chars(UART_NUM, GNSSNMEA, strlen(GNSSNMEA));
    vTaskDelay(1000);
    uart_tx_chars(UART_NUM, GNSSPORTSWITCH, strlen(GNSSPORTSWITCH));
    vTaskDelay(1000);
}

static void read_and_parse_nmea()
{
    nmea_data_init();
    uart_event_t event;
    while (1) {
                // 等待UART事件，无限等待
        if (xQueueReceive(uart_queue, (void *)&event, portMAX_DELAY)) {
            // 只处理接收到数据的事件
            switch (event.type){
            case UART_DATA:
                break;
            case UART_FIFO_OVF:
                ESP_LOGW(TAG, "HW FIFO Overflow");
                uart_flush(UART_NUM);
                xQueueReset(uart_queue);
                break;
            case UART_BUFFER_FULL:
                ESP_LOGW(TAG, "Ring Buffer Full");
                uart_flush(UART_NUM);
                xQueueReset(uart_queue);
                break;
            case UART_PATTERN_DET:
            nmea_s *data;
                
            char *start;
            size_t length;
            nmea_read_line(&start, &length, 100); 
            if (length == 0) {
                continue;
            }
            /* handle data */
            data = nmea_parse(start, length, 0);
            if (data == NULL) {
                printf("Failed to parse the sentence!\n");
                printf("  Type: %.5s (%d)\n", start + 1, nmea_get_type(start));
            } else {
                if (data->errors != 0) {
                    printf("WARN: The sentence struct contains parse errors!\n");
                }
                
                if (NMEA_GPGGA == data->type) {
                    printf("GPGGA sentence\n");
                    gpgga = (nmea_gpgga_s *) data;
                    printf("Number of satellites: %d\n", gpgga->n_satellites);
                    printf("Altitude: %f %c\n", gpgga->altitude,
                           gpgga->altitude_unit);
                }
                
                if (NMEA_GPRMC == data->type) {
                    printf("GPRMC sentence\n");
                    gprmc = (nmea_gprmc_s *) data;
                    
                    
                    char position_str[64];
                    format_gps_position(&gprmc->latitude, &gprmc->longitude, position_str, sizeof(position_str));
                    printf("Position: %s\n", position_str);
                    
                    printf("Longitude:\n");
                    printf("  Degrees: %d\n", gprmc->longitude.degrees);
                    printf("  Minutes: %f\n", gprmc->longitude.minutes);
                    printf("  Cardinal: %c\n", (char) gprmc->longitude.cardinal);
                    printf("Latitude:\n");
                    printf("  Degrees: %d\n", gprmc->latitude.degrees);
                    printf("  Minutes: %f\n", gprmc->latitude.minutes);
                    printf("  Cardinal: %c\n", (char) gprmc->latitude.cardinal);
                }
                
                nmea_free(data);
            }
            default:
                break;
            }
        }
    }
}


void GPS_init_interface(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        .source_clk = UART_SCLK_DEFAULT,
#endif
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM,
                        CONFIG_GNSS_UART_TX, CONFIG_GNSS_UART_RX,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_RX_BUF_SIZE * 2, 0, UART_QUEUE_SIZE, &uart_queue, 0));
    uart_enable_pattern_det_baud_intr(UART_NUM, '\n', 1, 9, 0, 0);
    xTaskCreate(read_and_parse_nmea, "read_and_parse_nmea", 4096, NULL, 10, NULL);
    printf("NMEA interface initialized\n");
}
#ifndef OTA_H
#define OTA_H

#include "esp_http_server.h"
#include "esp_https_ota.h"

#define OTA_URL_SIZE 256

// OTA任务的参数结构体
typedef struct {
    char *url;
} ota_task_param_t;

esp_err_t mqtt_ota_handler(const char *mqtt_data, size_t data_len);

void run_diagnostic(void);

#endif
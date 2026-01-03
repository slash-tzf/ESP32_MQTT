#include "gy26.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "gy26";

// Based on the vendor I2C demo:
// - Write: [0x00, 0x31] to trigger heading measurement
// - Write: [0x00, 0x35] to trigger temperature measurement
// - Then read 8 bytes from address 0x00
#define GY26_MEM_ADDR                 0x00
#define GY26_CMD_MEASURE_HEADING      0x31
#define GY26_CMD_MEASURE_TEMPERATURE  0x35

#define GY26_REG_HEADING_H            0x01
#define GY26_REG_HEADING_L            0x02
#define GY26_REG_PITCH_H              0x03
#define GY26_REG_PITCH_L              0x04
#define GY26_REG_TEMP_H               0x05
#define GY26_REG_TEMP_L               0x06
#define GY26_REG_CAL_LEVEL            0x07

static esp_err_t gy26_write_cmd(const gy26_handle_t *handle, uint8_t cmd)
{
    uint8_t payload[2] = {GY26_MEM_ADDR, cmd};
    return i2c_master_write_to_device(handle->port,
                                      handle->i2c_addr_7bit,
                                      payload,
                                      sizeof(payload),
                                      pdMS_TO_TICKS(200));
}

static esp_err_t gy26_read_reg8(const gy26_handle_t *handle, uint8_t reg, uint8_t *out)
{
    if (!handle || !out) {
        return ESP_ERR_INVALID_ARG;
    }

    // The vendor demos treat the module like an EEPROM: set register address, then read 1 byte.
    return i2c_master_write_read_device(handle->port,
                                        handle->i2c_addr_7bit,
                                        &reg,
                                        sizeof(reg),
                                        out,
                                        1,
                                        pdMS_TO_TICKS(200));
}

static esp_err_t gy26_read_heading_only(const gy26_handle_t *handle, float *heading_deg)
{
    if (!handle || !heading_deg) {
        return ESP_ERR_INVALID_ARG;
    }

    // Trigger heading measurement only
    esp_err_t ret = gy26_write_cmd(handle, GY26_CMD_MEASURE_HEADING);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(handle->measure_delay_heading_ms));

    uint8_t h = 0, l = 0;
    ret = gy26_read_reg8(handle, GY26_REG_HEADING_H, &h);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gy26_read_reg8(handle, GY26_REG_HEADING_L, &l);
    if (ret != ESP_OK) {
        return ret;
    }

    uint16_t heading_tenths = ((uint16_t)h << 8) | l;
    *heading_deg = (float)heading_tenths / 10.0f;
    return ESP_OK;
}

esp_err_t gy26_init(gy26_handle_t *handle,
                   i2c_port_t port,
                   uint8_t i2c_addr_7bit,
                   uint32_t measure_delay_heading_ms,
                   uint32_t measure_delay_temp_ms)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(handle, 0, sizeof(*handle));
    handle->port = port;
    handle->i2c_addr_7bit = i2c_addr_7bit;
    handle->measure_delay_heading_ms = measure_delay_heading_ms;
    handle->measure_delay_temp_ms = measure_delay_temp_ms;

    // Lightweight probe: try reading a couple of registers; failure is not fatal.
    uint8_t tmp = 0;
    esp_err_t ret = gy26_read_reg8(handle, GY26_REG_CAL_LEVEL, &tmp);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "probe read failed: %s", esp_err_to_name(ret));
    }

    return ESP_OK;
}

esp_err_t gy26_read_heading_temperature(const gy26_handle_t *handle,
                                        float *heading_deg,
                                        float *temperature_c)
{
    if (!handle || (!heading_deg && !temperature_c)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;

    if (heading_deg) {
        ret = gy26_read_heading_only(handle, heading_deg);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    if (temperature_c) {
        // Trigger temperature measurement
        ret = gy26_write_cmd(handle, GY26_CMD_MEASURE_TEMPERATURE);
        if (ret != ESP_OK) {
            return ret;
        }
        vTaskDelay(pdMS_TO_TICKS(handle->measure_delay_temp_ms));

        uint8_t h = 0, l = 0;
        ret = gy26_read_reg8(handle, GY26_REG_TEMP_H, &h);
        if (ret != ESP_OK) {
            return ret;
        }
        ret = gy26_read_reg8(handle, GY26_REG_TEMP_L, &l);
        if (ret != ESP_OK) {
            return ret;
        }

        uint16_t temp_raw = ((uint16_t)h << 8) | l;
        bool negative = (temp_raw & 0x1000) != 0;
        temp_raw &= 0x0FFF;
        float t = (float)temp_raw / 10.0f;
        *temperature_c = negative ? -t : t;
    }

    return ESP_OK;
}

esp_err_t gy26_read_heading(const gy26_handle_t *handle, float *heading_deg)
{
    return gy26_read_heading_only(handle, heading_deg);
}

esp_err_t gy26_read_temperature(const gy26_handle_t *handle, float *temperature_c)
{
    return gy26_read_heading_temperature(handle, NULL, temperature_c);
}

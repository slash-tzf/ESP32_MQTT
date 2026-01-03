#pragma once

#include <stdint.h>
#include "driver/i2c.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_port_t port;
    uint8_t i2c_addr_7bit;
    uint32_t measure_delay_heading_ms;
    uint32_t measure_delay_temp_ms;
} gy26_handle_t;

/**
 * @brief Initialize a GY-26 compass handle (I2C mode).
 *
 * Note: The I2C driver for the port must already be installed.
 */
esp_err_t gy26_init(gy26_handle_t *handle,
                   i2c_port_t port,
                   uint8_t i2c_addr_7bit,
                   uint32_t measure_delay_heading_ms,
                   uint32_t measure_delay_temp_ms);

/**
 * @brief Read heading in degrees.
 *
 * @param heading_deg Output heading, unit: degrees.
 */
esp_err_t gy26_read_heading(const gy26_handle_t *handle, float *heading_deg);

/**
 * @brief Read temperature in Celsius.
 */
esp_err_t gy26_read_temperature(const gy26_handle_t *handle, float *temperature_c);

/**
 * @brief Read both heading and temperature.
 */
esp_err_t gy26_read_heading_temperature(const gy26_handle_t *handle,
                                        float *heading_deg,
                                        float *temperature_c);

#ifdef __cplusplus
}
#endif

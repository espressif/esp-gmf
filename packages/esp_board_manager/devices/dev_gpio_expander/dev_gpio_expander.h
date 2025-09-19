/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define DEV_IO_EXPANDER_MAX_ADDR_COUNT  2

/**
 * @brief  IO Expander device configuration structure
 */
typedef struct {
    const char     *name;
    const char     *type;
    const char     *chip;
    const char     *i2c_name;
    size_t          i2c_addr_count;
    const uint16_t  i2c_addr[DEV_IO_EXPANDER_MAX_ADDR_COUNT];
    const int8_t    max_pins;
    uint32_t        output_io_mask;
    uint32_t        output_io_level_mask;
    uint32_t        input_io_mask;
} dev_io_expander_config_t;

/**
 * @brief  Initialize the IO expander device with the given configuration
 *
 *         This function initializes an IO expander device using the provided configuration structure.
 *         It sets up the necessary hardware interfaces (I2C) and allocates resources
 *         for the IO expander. The resulting device handle can be used for further IO operations.
 *
 * @param[in]   cfg            Pointer to the IO expander configuration structure
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  device_handle  Pointer to a variable to receive the esp_io_expander_handle_t handle
 *
 * @return
 *       - 0                On success
 *       - Negative  value  On failure
 */
int dev_gpio_expander_init(void *cfg, int cfg_size, void **device_handle);

/**
 * @brief  Deinitialize the IO expander device
 *
 *         This function deinitializes the IO expander device and frees the allocated resources.
 *         It should be called when the device is no longer needed to prevent memory leaks.
 *
 * @param[in]  device_handle  Pointer to the device handle to be deinitialized
 *
 * @return
 *       - 0         On success
 *       - Negative  value  On failure
 */
int dev_gpio_expander_deinit(void *device_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

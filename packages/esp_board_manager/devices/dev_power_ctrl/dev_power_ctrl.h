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

/**
 * @brief  GPIO power control sub-configuration structure
 *
 *         This structure defines the configuration for GPIO-based power control,
 *         including the GPIO peripheral name and the active level for enabling power.
 */
typedef struct {
    const char  *gpio_name;     /*!< GPIO peripheral name */
    uint8_t      active_level;  /*!< Active level to enable power: 0 or 1 */
} dev_power_ctrl_gpio_sub_config_t;

/**
 * @brief  Power control device handle structure
 *
 *         This structure contains the handle for the power control device,
 *         including the peripheral handle and the power control function pointer.
 */
typedef struct {
    void  *periph_handle;  /*!< Peripheral handle */
} dev_power_ctrl_handle_t;

/**
 * @brief  Power control device configuration structure
 *
 *         This structure contains all the configuration parameters needed to initialize
 *         a power control device, including device name, type, sub-type, and sub-type
 *         specific configuration.
 */
typedef struct {
    const char  *name;      /*!< Power control device name */
    const char  *type;      /*!< Device type: "power_ctrl" */
    const char  *sub_type;  /*!< Power sub-type: "gpio", "power_ic(todo)", etc. */
    union {
        dev_power_ctrl_gpio_sub_config_t  gpio;  /*!< GPIO sub-type configuration */
    } sub_cfg;                                   /*!< Sub-type specific configuration */
} dev_power_ctrl_config_t;

/**
 * @brief  Initialize power control device
 *
 *         This function initializes a power control device using the provided configuration structure.
 *         It sets up the necessary hardware interface based on the sub-type configuration.
 *         The resulting device handle can be used for power control operations.
 *
 * @param[in]   cfg            Pointer to the power control configuration structure
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  device_handle  Pointer to a variable to receive the dev_power_ctrl_handle_t handle
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_power_ctrl_init(void *cfg, int cfg_size, void **device_handle);

/**
 * @brief  Deinitialize power control device
 *
 *         This function deinitializes the power control device and frees the allocated resources.
 *         It should be called when the device is no longer needed to prevent resource leaks.
 *
 * @param[in]  device_handle  Pointer to the device handle to be deinitialized
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_power_ctrl_deinit(void *device_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

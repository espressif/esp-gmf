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
 * @brief  M5Stack CoreS3 feature enumeration
 *
 *         This enumeration defines various hardware features supported by the M5Stack CoreS3 development board,
 *         used to enable or disable specific functional modules in the power management system.
 */
typedef enum {
    CORES3_POWER_MANAGER_FEATURE_LCD,      /*!< LCD display feature */
    CORES3_POWER_MANAGER_FEATURE_TOUCH,    /*!< Touch screen feature */
    CORES3_POWER_MANAGER_FEATURE_SD,       /*!< SD card feature */
    CORES3_POWER_MANAGER_FEATURE_SPEAKER,  /*!< Speaker feature */
    CORES3_POWER_MANAGER_FEATURE_CAMERA,   /*!< Camera feature */
} cores3_power_manager_feature_t;

/**
 * @brief  Power manager handle structure
 *
 *         This structure holds the handle to the power management device (AXP2101 PMU)
 *         used for controlling power features on the M5Stack CoreS3 board.
 */
typedef struct {
    i2c_master_dev_handle_t pm_handle;  /*!< I2C device handle for AXP2101 power management unit */
} cores3_power_manager_handle_t;

/**
 * @brief  Initialize the power manager
 *
 *         This function initializes the power manager, configures the AXP2101 and AW9523 chips,
 *         and returns a device handle that can be used for subsequent power management operations.
 *
 * @param[in]   config         Pointer to the configuration structure
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  device_handle  Pointer to a variable to receive the power manager handle
 *
 * @return
 *       - 0      On success
 *       - Other  error codes
 */
int cores3_power_manager_init(void *config, int cfg_size, void **device_handle);

/**
 * @brief  Deinitialize the power manager
 *
 *         This function deinitializes the power manager, frees allocated resources,
 *         and closes communication with the AXP2101 and AW9523 chips.
 *
 * @param[in]  device_handle  Pointer to the device handle to be deinitialized
 *
 * @return
 *       - 0      On success
 *       - Other  error codes
 */
int cores3_power_manager_deinit(void *device_handle);

/**
 * @brief  Enable specific feature on M5Stack CoreS3
 *
 *         This function enables the specified hardware feature by controlling the AXP2101 power management chip,
 *         providing the required power supply for the corresponding functional module.
 * @note   In cores3, the AW9523B IO expander needs to be initialized before the AXP2101 PMU, because AW9523B is needed when enabling the power
 *
 * @param[in]  pm_handle  Pointer to the power manager handle structure
 * @param[in]  feature    Feature to enable (cores3_power_manager_feature_t enumeration value)
 *
 * @return
 *       - ESP_OK  On success
 *       - Other   error codes (see esp_err_t for details)
 */
esp_err_t cores3_power_manager_enable(void *device_handle, cores3_power_manager_feature_t feature);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

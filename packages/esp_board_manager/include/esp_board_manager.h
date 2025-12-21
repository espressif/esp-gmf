/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include "esp_board_manager_err.h"
#include "esp_board_device.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Board information structure
 */
typedef struct esp_board_info {
    const char  *name;          /*!< Board name */
    const char  *chip;          /*!< Chip type */
    const char  *version;       /*!< Board version */
    const char  *description;   /*!< Board description */
    const char  *manufacturer;  /*!< Board manufacturer */
} esp_board_info_t;

/* Global board information */
extern const esp_board_info_t g_esp_board_info;

/**
 * @brief  Initialize the board manager
 *
 *         Initializes all peripherals first, then all devices. If device initialization
 *         fails, peripherals are deinitialized to maintain consistency. The manager
 *         can only be initialized once
 *
 * @note  Device initialization strictly follows the order defined in board_devices.yaml
 *         Peripheral initialization strictly follows the order defined in board_peripherals.yaml
 *         If a device depends on a peripheral for power-on, it must be initialized after that peripheral
 *         For example, the LCD power control device should be listed before the Display LCD device
 *         in board_devices.yaml
 *
 * @return
 *       - ESP_OK                              On success
 *       - ESP_BOARD_ERR_MANAGER_ALREADY_INIT  If manager already initialized
 *       - Others                              Error codes from peripheral or device initialization
 */
esp_err_t esp_board_manager_init(void);

/**
 * @brief  Get peripheral handle by name
 *
 *         Retrieves a peripheral handle that has been initialized by the board manager
 *         The board manager must be initialized before calling this function
 *
 * @param[in]   periph_name    Peripheral name
 * @param[out]  periph_handle  Pointer to store the peripheral handle
 *
 * @return
 *       - ESP_OK                                  On success
 *       - ESP_BOARD_ERR_MANAGER_INVALID_ARG       If periph_name or periph_handle is NULL
 *       - ESP_BOARD_ERR_MANAGER_PERIPH_NOT_FOUND  If peripheral not found
 */
esp_err_t esp_board_manager_get_periph_handle(const char *periph_name, void **periph_handle);

/**
 * @brief  Get device handle by name
 *
 *         Retrieves a device handle that has been initialized by the board manager
 *         The board manager must be initialized before calling this function
 *
 * @param[in]   dev_name       Device name
 * @param[out]  device_handle  Pointer to store the device handle
 *
 * @return
 *       - ESP_OK                                  On success
 *       - ESP_BOARD_ERR_MANAGER_INVALID_ARG       If dev_name or device_handle is NULL
 *       - ESP_BOARD_ERR_MANAGER_DEVICE_NOT_FOUND  If device not found
 */
esp_err_t esp_board_manager_get_device_handle(const char *dev_name, void **device_handle);

/**
 * @brief  Query device configuration
 *
 *         Retrieves the configuration data for a specific device. This function
 *         returns the raw configuration data that was used to initialize the device
 *
 * @param[in]   dev_name  Device name
 * @param[out]  config    Pointer to store the device configuration
 *
 * @return
 *       - ESP_OK                                  On success
 *       - ESP_BOARD_ERR_MANAGER_INVALID_ARG       If dev_name or config is NULL
 *       - ESP_BOARD_ERR_MANAGER_DEVICE_NOT_FOUND  If device not found
 *       - ESP_BOARD_ERR_DEVICE_NOT_SUPPORTED      If device has no configuration
 */
esp_err_t esp_board_manager_get_device_config(const char *dev_name, void **config);

/**
 * @brief  Get peripheral configuration by name
 *
 * @param[in]   periph_name  Peripheral name
 * @param[out]  config       Pointer to store the peripheral configuration
 *
 * @return
 *       - ESP_OK                              On success
 *       - ESP_BOARD_ERR_MANAGER_INVALID_ARG   If periph_name or config is NULL
 *       - ESP_BOARD_ERR_PERIPH_NOT_FOUND      If peripheral configuration not found
 *       - ESP_BOARD_ERR_PERIPH_NOT_SUPPORTED  If peripheral has no configuration
 */
esp_err_t esp_board_manager_get_periph_config(const char *periph_name, void **config);

/**
 * @brief  Retrieves the board information
 *
 *         This function can be called without initializing the board manager
 *
 * @param[out]  board_info  Pointer to store the board information
 *
 * @return
 *       - ESP_OK                             On success
 *       - ESP_BOARD_ERR_MANAGER_INVALID_ARG  If board_info is NULL
 */
esp_err_t esp_board_manager_get_board_info(esp_board_info_t *board_info);

/**
 * @brief  Register a user defined device handle
 *         The user can initialize the device handle manually and register it with the board manager
 *         After that, the application can access it through the board manager APIs
 *
 * @param[in]  reg_handle  Pointer to the device handle to register
 *
 * @return
 *       - ESP_OK                             On success
 *       - ESP_BOARD_ERR_MANAGER_INVALID_ARG  If reg_handle is NULL
 */
esp_err_t esp_board_manager_register_device_handle(esp_board_device_handle_t *reg_handle);

/**
 * @brief  Initialize a specific device by name
 *
 *         Initializes a single device by name. The device must exist in the board
 *         configuration. This function is useful for lazy initialization of devices
 *
 * @param[in]  dev_name  Device name to initialize
 *
 * @return
 *       - ESP_OK                                  On success
 *       - ESP_BOARD_ERR_MANAGER_INVALID_ARG       If dev_name is NULL
 *       - ESP_BOARD_ERR_MANAGER_DEVICE_NOT_FOUND  If device not found
 *       - Others                                  Error codes from device initialization
 */
esp_err_t esp_board_manager_init_device_by_name(const char *dev_name);

/**
 * @brief  Deinitialize a specific device by name
 *
 *         Deinitializes a single device by name. The device must exist and be
 *         initialized. This function is useful for selective cleanup
 *
 * @param[in]  dev_name  Device name to deinitialize
 *
 * @return
 *       - ESP_OK                             On success
 *       - ESP_BOARD_ERR_MANAGER_INVALID_ARG  If dev_name is NULL
 *       - Others                             Error codes from device deinitialization
 */
esp_err_t esp_board_manager_deinit_device_by_name(const char *dev_name);

/**
 * @brief  Print board manager status information
 *
 *         Displays comprehensive status information including all peripherals,
 *         devices, and their associations. The board manager must be initialized
 *
 * @return
 *       - ESP_OK  On success
 */
esp_err_t esp_board_manager_print(void);

/**
 * @brief  Print board information
 *
 *         Displays board metadata including name, chip type, version, description,
 *         and manufacturer. This function can be called without initializing the
 *         board manager
 *
 * @return
 *       - ESP_OK  On success
 */
esp_err_t esp_board_manager_print_board_info(void);

/**
 * @brief  Deinitialize the board manager
 *
 *         Deinitializes all devices first, then all peripherals. This ensures
 *         proper cleanup order and prevents resource leaks. The manager state
 *         is reset to allow re-initialization
 *
 * @return
 *       - ESP_OK                          On success
 *       - ESP_BOARD_ERR_MANAGER_NOT_INIT  If board manager not initialized
 *       - Others                          Error codes from device or peripheral deinitialization
 */
esp_err_t esp_board_manager_deinit(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

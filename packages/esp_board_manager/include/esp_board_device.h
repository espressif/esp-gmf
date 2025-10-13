/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_board_manager_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Function pointer type for device initialization
 */
typedef int (*esp_board_device_init_func)(void *cfg, int cfg_size, void **device_handle);

/**
 * @brief  Function pointer type for device deinitialization
 */
typedef int (*esp_board_device_deinit_func)(void *device_handle);

/**
 * @brief  Structure representing a device descriptor
 */
typedef struct esp_board_device_desc {
    const struct esp_board_device_desc *next;          /*!< Pointer to next device descriptor */
    const char                         *name;          /*!< Device name */
    const char                         *chip;          /*!< Device chip type */
    const char                         *type;          /*!< Device type */
    const void                         *cfg;           /*!< Device configuration data */
    int                                cfg_size;       /*!< Size of configuration data */
    uint8_t                            init_skip : 1;  /*!< Skip initialization when manager initializes all devices */
} esp_board_device_desc_t;

/**
 * @brief  Structure representing a device handle
 */
typedef struct esp_board_device_handle {
    struct esp_board_device_handle *next;           /*!< Pointer to next device handle */
    const char                     *name;           /*!< Device name */
    const char                     *chip;           /*!< Device chip type */
    const char                     *type;           /*!< Device type */
    void                           *device_handle;  /*!< Device-specific handle */
    uint32_t                        ref_count;      /*!< Reference count */
    esp_board_device_init_func      init;           /*!< Device initialization function */
    esp_board_device_deinit_func    deinit;         /*!< Device deinitialization function */
} esp_board_device_handle_t;

/**
 * @brief  Initialize a device by name
 *
 *         This function initializes a device with reference counting support. If the device
 *         is already initialized, it increments the reference count instead of reinitializing.
 *         The device is only actually initialized when the reference count reaches 1.
 *
 * @param[in]  name  Device name to initialize
 *
 * @return
 *       - ESP_OK                            On success
 *       - ESP_BOARD_ERR_DEVICE_INVALID_ARG  If name is NULL
 *       - ESP_BOARD_ERR_DEVICE_NOT_FOUND    If device descriptor or handle not found
 *       - ESP_BOARD_ERR_DEVICE_NO_INIT      If no init function is available
 *       - Others                            Error codes from device-specific initialization
 */
esp_err_t esp_board_device_init(const char *name);

/**
 * @brief  Get device handle by name
 *
 *         Retrieves the device handle for a given device name. For GPIO devices,
 *         the handle is dereferenced to get the actual pin number.
 *
 * @param[in]   name           Device name
 * @param[out]  device_handle  Pointer to store the device handle
 *
 * @return
 *       - ESP_OK                            On success
 *       - ESP_BOARD_ERR_DEVICE_INVALID_ARG  If name or device_handle is NULL
 *       - ESP_BOARD_ERR_DEVICE_NOT_FOUND    If device handle not found
 */
esp_err_t esp_board_device_get_handle(const char *name, void **device_handle);

/**
 * @brief  Get device configuration by name
 *
 *         Retrieves the configuration for a given device name.
 *
 * @param[in]   name    Device name
 * @param[out]  config  Pointer to store the configuration
 *
 * @return
 *       - ESP_OK                            On success
 *       - ESP_BOARD_ERR_DEVICE_INVALID_ARG  If name or config is NULL
 *       - ESP_BOARD_ERR_DEVICE_NOT_FOUND    If device configuration not found
 *       - ESP_ERR_NOT_FOUND                 If device has no configuration
 */
esp_err_t esp_board_device_get_config(const char *name, void **config);

/**
 * @brief  Set device initialization and deinitialization functions
 *
 *         Associates custom init and deinit functions with a device. This allows
 *         runtime configuration of device behavior.
 *
 * @param[in]  name    Device name
 * @param[in]  init    Initialization function pointer
 * @param[in]  deinit  Deinitialization function pointer
 *
 * @return
 *       - ESP_OK                            On success
 *       - ESP_BOARD_ERR_DEVICE_INVALID_ARG  If any parameter is NULL
 *       - ESP_BOARD_ERR_DEVICE_NOT_FOUND    If device handle not found
 */
esp_err_t esp_board_device_set_ops(const char *name, esp_board_device_init_func init, esp_board_device_deinit_func deinit);

/**
 * @brief  Deinitialize a device by name
 *
 *         Decrements the reference count for a device. The device is only actually
 *         deinitialized when the reference count reaches 0. If the device is not
 *         initialized, this function returns success.
 *
 * @param[in]  name  Device name to deinitialize
 *
 * @return
 *       - ESP_OK                            On success
 *       - ESP_BOARD_ERR_DEVICE_INVALID_ARG  If name is NULL
 *       - ESP_BOARD_ERR_DEVICE_NOT_FOUND    If device handle not found
 *       - ESP_BOARD_ERR_DEVICE_NO_INIT      If no deinit function is available
 *       - Others                            Error codes from device-specific deinitialization
 */
esp_err_t esp_board_device_deinit(const char *name);

/**
 * @brief  Show device information
 *
 *         Displays detailed information about devices. If name is NULL, shows
 *         information for all devices. Otherwise, shows information for the
 *         specified device including type, configuration size, handle status,
 *         and reference count.
 *
 * @param[in]  name  Device name (NULL for all devices)
 *
 * @return
 *       - ESP_OK                          On success
 *       - ESP_BOARD_ERR_DEVICE_NOT_FOUND  If specific device not found (when name is not NULL)
 */
esp_err_t esp_board_device_show(const char *name);

/**
 * @brief  Initialize all devices
 *
 *         Iterates through all device descriptors and initializes each device.
 *         Continues initializing even if some devices fail, but logs errors.
 *
 *   NOTE: Device initialization strictly follows the order defined in board_devices.yaml.
 *         If a device depends on a peripheral for power-on, it must be initialized
 *         after that peripheral. For example, the LCD power control device should be
 *         listed before the Display LCD device in board_devices.yaml.
 *         If initialization fails due to unresolved dependencies, either reorder the
 *         YAML entries or manually call the device initialization function.
 *
 * @return
 *       - ESP_OK  On success (always returns ESP_OK, errors are logged)
 */
esp_err_t esp_board_device_init_all(void);

/**
 * @brief  Deinitialize all devices
 *
 *         Iterates through all device handles and deinitializes each device.
 *         Continues deinitializing even if some devices fail, but logs errors.
 *
 * @return
 *       - ESP_OK  On success (always returns ESP_OK, errors are logged)
 */
esp_err_t esp_board_device_deinit_all(void);

/**
 * @brief  Find device handle structure by device-specific handle
 *
 *         Searches through the device handle linked list to find the device
 *         handle structure that contains the specified device-specific handle.
 *
 * @param[in]  device_handle  Device-specific handle to search for
 *
 * @return
 *       - NULL    If not found or if device_handle is NULL
 *       - Others  Pointer to device handle structure if found
 */
const esp_board_device_handle_t *esp_board_device_find_by_handle(void *device_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

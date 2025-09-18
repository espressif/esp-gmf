/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_board_manager_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief Maximum number of peripherals supported by custom device
 */
#define MAX_PERIPHERALS 4

/**
 * @brief  Function pointer type for custom device initialization
 */
typedef int (*custom_device_init_func_t)(void *config, int cfg_size, void **device_handle);

/**
 * @brief  Function pointer type for custom device deinitialization
 */
typedef int (*custom_device_deinit_func_t)(void *device_handle);

/**
 * @brief  Custom device handle structure
 *
 *         This structure should be used as the device handle to store device information
 *         for proper deinitialization.
 */
typedef struct {
    void       *user_handle;  /*!< User-defined device handle */
    const char *device_name;  /*!< Device name for lookup */
} custom_device_handle_t;

/**
 * @brief  Structure describing a custom device implementation
 *
 *         This structure is placed in a special linker section (.custom_devices_desc)
 *         and automatically discovered by the custom device system.
 */
typedef struct {
    const char                   *device_name;  /*!< Custom device name (must match YAML config) */
    custom_device_init_func_t    init_func;     /*!< Device initialization function */
    custom_device_deinit_func_t  deinit_func;   /*!< Device deinitialization function */
} custom_device_desc_t;

/**
 * @brief  Macro to define a custom device implementation
 *
 *         This macro places the device descriptor in the special linker section
 *         for automatic discovery by the custom device system
 *
 * @param  name         Device name (must match YAML config)
 * @param  init_func    Initialization function pointer
 * @param  deinit_func  Deinitialization function pointer
 */
#define CUSTOM_DEVICE_IMPLEMENT(name, init_func_entry, deinit_func_entry) \
    static const custom_device_desc_t __attribute__((section(".custom_devices_desc"), used)) \
    custom_device_##name = { \
        .device_name = #name, \
        .init_func = init_func_entry, \
        .deinit_func = deinit_func_entry \
    }

/**
 * @brief  Generic custom device configuration structure
 *
 *         This is a base structure that will be extended by dynamically generated
 *         structures based on the YAML configuration.
 */
typedef struct {
    const char *name;                               /*!< Custom device name */
    const char *type;                               /*!< Device type: "custom" */
    const char *chip;                               /*!< Chip name */
    uint8_t     peripheral_count;                   /*!< Number of peripherals */
    const char *peripheral_names[MAX_PERIPHERALS];  /*!< Peripheral names array */
} dev_custom_base_config_t;

/**
 * @brief  Initialize custom device
 *
 * @param[in]   cfg            Pointer to device configuration
 * @param[in]   cfg_size       Size of configuration structure
 * @param[out]  device_handle  Pointer to store device handle
 *
 * @return
 *       - 0               On success
 *       - Negative value  On failure
 */
int dev_custom_init(void *cfg, int cfg_size, void **device_handle);

/**
 * @brief  Deinitialize custom device
 *
 * @param[in]  device_handle  Device handle
 *
 * @return
 *       - 0               On success
 *       - Negative value  On failure
 */
int dev_custom_deinit(void *device_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

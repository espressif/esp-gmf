/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <string.h>
#include "esp_log.h"
#include "esp_board_device.h"
#include "esp_board_periph.h"
#include "esp_board_manager_defs.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/* External references to global arrays */
extern const esp_board_device_desc_t g_esp_board_devices[];
extern esp_board_device_handle_t g_esp_board_device_handles[];
extern const esp_board_periph_desc_t g_esp_board_peripherals[];
extern esp_board_periph_entry_t g_esp_board_periph_handles[];

/**
 * @brief  Find device handle by name
 *
 *         Searches through the global device handles array to find a device
 *         with the specified name. This is an internal utility function used
 *         by the board manager for device management
 *
 * @param[in]  name  Device name to search for
 *
 * @return
 *       - esp_board_device_handle_t*  Device handle if found
 *       - NULL                        If device not found or name is NULL
 */
static inline esp_board_device_handle_t *esp_board_find_device_handle(const char *name)
{
    if (!name) {
        return NULL;
    }

    esp_board_device_handle_t *handle = g_esp_board_device_handles;
    while (handle && handle->name) {
        if (strcmp(handle->name, name) == 0) {
            return handle;
        }
        handle = handle->next;
    }
    return NULL;
}

/**
 * @brief  Find device descriptor by name
 *
 *         Searches through the global device descriptors array to find a device
 *         descriptor with the specified name. This is an internal utility function
 *         used by the board manager for device configuration management
 *
 * @param[in]  name  Device name to search for
 *
 * @return
 *       - const esp_board_device_desc_t*  Device descriptor if found
 *       - NULL                            If device not found or name is NULL
 */
static inline const esp_board_device_desc_t *esp_board_find_device_desc(const char *name)
{
    if (!name) {
        return NULL;
    }

    const esp_board_device_desc_t *desc = g_esp_board_devices;
    while (desc) {
        if (strcmp(desc->name, name) == 0) {
            return desc;
        }
        desc = desc->next;
    }
    return NULL;
}

/**
 * @brief  Find peripheral descriptor by name
 *
 *         Searches through the global peripheral descriptors array to find a
 *         peripheral descriptor with the specified name. This is an internal
 *         utility function used by the board manager for peripheral configuration management
 *
 * @param[in]  name  Peripheral name to search for
 *
 * @return
 *       - const esp_board_periph_desc_t*  Peripheral descriptor if found
 *       - NULL                            If peripheral not found or name is NULL
 */
static inline const esp_board_periph_desc_t *esp_board_find_periph_desc(const char *name)
{
    if (!name) {
        return NULL;
    }

    const esp_board_periph_desc_t *desc = g_esp_board_peripherals;
    while (desc) {
        if (strcmp(desc->name, name) == 0) {
            return desc;
        }
        desc = desc->next;
    }
    return NULL;
}

/**
 * @brief  Find peripheral handle by type and role
 *
 *         Searches through the global peripheral handles array to find a
 *         peripheral handle with the specified type and role combination.
 *         This is an internal utility function used by the board manager
 *         for peripheral handle management
 *
 * @param[in]  type  Peripheral type to search for
 * @param[in]  role  Peripheral role to search for
 *
 * @return
 *       - esp_board_periph_entry_t*  Peripheral handle if found
 *       - NULL                       If peripheral not found or type/role is NULL
 */
static inline esp_board_periph_entry_t *esp_board_find_periph_handle(const char *type, esp_board_periph_role_t role)
{
    if (!type) {
        return NULL;
    }

    esp_board_periph_entry_t *handle = g_esp_board_periph_handles;
    while (handle) {
        if (strcmp(handle->type, type) == 0 && handle->role == role) {
            return handle;
        }
        handle = handle->next;
    }

    /* No logging needed for internal utility function */
    return NULL;
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */

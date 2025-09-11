/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_board_device.h"
#include "esp_board_manager_err.h"

static const char *TAG = "BOARD_DEVICE";

extern const esp_board_device_desc_t g_esp_board_devices[];
extern esp_board_device_handle_t g_esp_board_device_handles[];

/* Find device descriptor by name */
static const esp_board_device_desc_t *find_device_desc(const char *name)
{
    const esp_board_device_desc_t *desc = g_esp_board_devices;
    while (desc) {
        if (strcmp(desc->name, name) == 0) {
            return desc;
        }
        desc = desc->next;
    }
    return NULL;
}

/* Find device handle by name */
static esp_board_device_handle_t *find_device_handle(const char *name)
{
    esp_board_device_handle_t *handle = g_esp_board_device_handles;
    while (handle && handle->name) {
        if (strcmp(handle->name, name) == 0) {
            return handle;
        }
        handle = handle->next;
    }
    return NULL;
}

esp_err_t esp_board_device_init(const char *name)
{
    ESP_BOARD_RETURN_ON_FALSE(name, ESP_BOARD_ERR_DEVICE_INVALID_ARG, TAG, "name is null");

    /* Find device descriptor */
    const esp_board_device_desc_t *desc = find_device_desc(name);
    ESP_BOARD_RETURN_ON_DEVICE_NOT_FOUND(desc, name, TAG, "Device %s not found", name);

    /* Find device handle */
    esp_board_device_handle_t *handle = find_device_handle(name);
    ESP_BOARD_RETURN_ON_DEVICE_NOT_FOUND(handle, name, TAG, "Device handle %s not found", name);

    /* Check if init function exists */
    ESP_BOARD_RETURN_ON_FALSE(handle->init, ESP_BOARD_ERR_DEVICE_NO_INIT, TAG, "No init function for device: %s", name);

    /* Increment reference count */
    handle->ref_count++;

    /* Initialize device if not already initialized */
    if (handle->device_handle) {
        ESP_LOGW(TAG, "Device %s already initialized, ref_count: %" PRIu32, name, handle->ref_count);
        return ESP_OK;
    } else {
        esp_err_t ret = handle->init((void *)desc->cfg, desc->cfg_size, &handle->device_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init device: %s", name);
            handle->ref_count--;  /* Decrement ref_count on failure */
            return ret;
        }
    }
    ESP_LOGI(TAG, "initialized device %s ref_count: %" PRIu32 " device_handle:%p", name, handle->ref_count, handle->device_handle);
    return ESP_OK;
}

esp_err_t esp_board_device_get_handle(const char *name, void **device_handle)
{
    ESP_BOARD_RETURN_ON_FALSE(name && device_handle, ESP_BOARD_ERR_DEVICE_INVALID_ARG, TAG, "Invalid args");

    /* Find device handle */
    esp_board_device_handle_t *handle = find_device_handle(name);
    ESP_BOARD_RETURN_ON_DEVICE_NOT_FOUND(handle, name, TAG, "Device handle %s not found", name);
    if (handle->device_handle == NULL) {
        ESP_LOGE(TAG, "Device %s handle is NULL", name);
        return ESP_BOARD_ERR_DEVICE_NO_HANDLE;
    }
    *device_handle = handle->device_handle;
    ESP_LOGI(TAG, "Device handle %s found, Handle: %p TO: %p", name, handle->device_handle, *device_handle);
    return ESP_OK;
}

esp_err_t esp_board_device_get_config(const char *name, void **config)
{
    ESP_BOARD_RETURN_ON_FALSE(name && config, ESP_BOARD_ERR_DEVICE_INVALID_ARG, TAG, "Invalid args");

    /* Find device descriptor */
    const esp_board_device_desc_t *desc = find_device_desc(name);
    ESP_BOARD_RETURN_ON_DEVICE_NOT_FOUND(desc, name, TAG, "Device descriptor %s not found", name);

    if (!desc->cfg) {
        ESP_LOGW(TAG, "Device %s has no config", name);
        *config = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    *config = (void *)desc->cfg;
    ESP_LOGI(TAG, "Device %s config found: %p (size: %d)", name, desc->cfg, desc->cfg_size);
    return ESP_OK;
}

esp_err_t esp_board_device_set_ops(const char *name, esp_board_device_init_func init, esp_board_device_deinit_func deinit)
{
    ESP_BOARD_RETURN_ON_FALSE(name && init && deinit, ESP_BOARD_ERR_DEVICE_INVALID_ARG, TAG, "Invalid args");

    /* Find device handle */
    esp_board_device_handle_t *handle = find_device_handle(name);
    ESP_BOARD_RETURN_ON_DEVICE_NOT_FOUND(handle, name, TAG, "Device handle %s not found", name);

    /* Set functions */
    handle->init = init;
    handle->deinit = deinit;
    ESP_LOGI(TAG, "Set functions for device: %s", name);

    return ESP_OK;
}

esp_err_t esp_board_device_deinit(const char *name)
{
    ESP_BOARD_RETURN_ON_FALSE(name, ESP_BOARD_ERR_DEVICE_INVALID_ARG, TAG, "name is null");

    /* Find device handle */
    esp_board_device_handle_t *handle = find_device_handle(name);
    ESP_BOARD_RETURN_ON_DEVICE_NOT_FOUND(handle, name, TAG, "Device handle %s not found", name);

    /* Check if device is initialized */
    if (!handle->device_handle) {
        ESP_LOGW(TAG, "Device %s not initialized", name);
        return ESP_OK;
    }

    /* Decrement reference count */
    handle->ref_count--;
    ESP_LOGI(TAG, "Deinit device %s ref_count: %" PRIu32 " device_handle:%p", name, handle->ref_count, handle->device_handle);

    /* Only deinitialize if ref_count reaches 0 */
    if (handle->ref_count == 0) {
        /* Check if deinit function exists */
        ESP_BOARD_RETURN_ON_FALSE(handle->deinit, ESP_BOARD_ERR_DEVICE_NO_INIT, TAG, "No deinit function for device: %s", name);

        /* Deinitialize device */
        esp_err_t ret = handle->deinit(handle->device_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to deinit device: %s", name);
            return ret;
        }
        handle->device_handle = NULL;
    } else {
        ESP_LOGW(TAG, "Device %s still has %" PRIu32 " references, not deinitializing", name, handle->ref_count);
    }

    return ESP_OK;
}

esp_err_t esp_board_device_show(const char *name)
{
    if (name) {
        /* Show information for specific device */
        const esp_board_device_desc_t *desc = find_device_desc(name);
        ESP_BOARD_RETURN_ON_DEVICE_NOT_FOUND(desc, name, TAG, "Device %s not found", name);

        esp_board_device_handle_t *handle = find_device_handle(name);
        ESP_LOGI(TAG, "Device %s:", name);
        ESP_LOGI(TAG, "  Type: %s", desc->type);
        ESP_LOGI(TAG, "  Config size: %d", desc->cfg_size);
        if (handle) {
            ESP_LOGI(TAG, "  Handle: %p", handle->device_handle);
            ESP_LOGI(TAG, "  Status: %s", handle->device_handle ? "Initialized" : "Not initialized");
            ESP_LOGI(TAG, "  Ref count: %" PRIu32, handle->ref_count);
        } else {
            ESP_LOGI(TAG, "  No handle found");
        }
    } else {
        /* Show information for all devices */
        const esp_board_device_desc_t *desc = g_esp_board_devices;
        while (desc) {
            esp_board_device_handle_t *handle = find_device_handle(desc->name);
            ESP_LOGI(TAG, "Device %s:", desc->name);
            ESP_LOGI(TAG, "  Type: %s", desc->type);
            ESP_LOGI(TAG, "  Config size: %d", desc->cfg_size);
            if (handle) {
                ESP_LOGI(TAG, "  Handle: %p", handle->device_handle);
                ESP_LOGI(TAG, "  Status: %s", handle->device_handle ? "Initialized" : "Not initialized");
                ESP_LOGI(TAG, "  Ref count: %" PRIu32, handle->ref_count);
            } else {
                ESP_LOGI(TAG, "  No handle found");
            }
            desc = desc->next;
        }
    }
    return ESP_OK;
}

esp_err_t esp_board_device_init_all(void)
{
    const esp_board_device_desc_t *desc = g_esp_board_devices;
    esp_err_t ret;

    /* Initialize all devices in the list */
    while (desc && desc->name) {
        /* Check if device should be skipped during initialization */
        if (desc->init_skip) {
            ESP_LOGD(TAG, "Skipping initialization of device: %s (init_skip=true)", desc->name);
            desc = desc->next;
            continue;
        }

        ret = esp_board_device_init(desc->name);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize device: %s", desc->name);
        }
        desc = desc->next;
    }
    return ESP_OK;
}

esp_err_t esp_board_device_deinit_all(void)
{
    const esp_board_device_desc_t *desc = g_esp_board_devices;
    esp_err_t ret;

    /* Deinitialize all devices in the list */
    while (desc && desc->name) {
        ret = esp_board_device_deinit(desc->name);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to deinitialize device: %s", desc->name);
        }
        desc = desc->next;
    }
    return ESP_OK;
}

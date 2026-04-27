/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_board_device.h"
#include "esp_board_manager_err.h"
#include "esp_board_find_utils.h"
#if CONFIG_ESP_BOARD_DEV_POWER_CTRL_SUPPORT
#include "dev_power_ctrl.h"
#endif  /* CONFIG_ESP_BOARD_DEV_POWER_CTRL_SUPPORT */

static const char *TAG = "BOARD_DEVICE";

extern const esp_board_device_desc_t g_esp_board_devices[];
extern esp_board_device_handle_t     g_esp_board_device_handles[];

typedef struct cfg_override_node {
    struct cfg_override_node *next;
    const char               *name;
    void                     *cfg;
    uint16_t                  cfg_size;
} cfg_override_node_t;

static cfg_override_node_t *s_cfg_override_list = NULL;

void esp_board_device_restore_all_configs(void)
{
    cfg_override_node_t *node = s_cfg_override_list;

    while (node) {
        cfg_override_node_t *next = node->next;
        free(node);
        node = next;
    }

    s_cfg_override_list = NULL;
}

static cfg_override_node_t **find_cfg_override_link(const char *name)
{
    cfg_override_node_t **link = &s_cfg_override_list;

    while (*link) {
        if (strcmp((*link)->name, name) == 0) {
            return link;
        }
        link = &(*link)->next;
    }
    return NULL;
}

static const void *find_effective_cfg(const char *name, const esp_board_device_desc_t *desc, uint16_t *out_size)
{
    for (cfg_override_node_t *node = s_cfg_override_list; node; node = node->next) {
        if (strcmp(node->name, name) == 0) {
            if (out_size) {
                *out_size = node->cfg_size;
            }
            return node->cfg;
        }
    }
    if (out_size) {
        *out_size = desc->cfg_size;
    }
    return desc->cfg;
}

esp_err_t esp_board_device_init(const char *name)
{
    ESP_BOARD_RETURN_ON_FALSE(name, ESP_BOARD_ERR_DEVICE_INVALID_ARG, TAG, "name is null");
    esp_err_t ret = ESP_OK;
    /* Find device descriptor */
    const esp_board_device_desc_t *desc = esp_board_find_device_desc(name);
    ESP_BOARD_RETURN_ON_DEVICE_NOT_FOUND(desc, name, TAG, "Device %s not found", name);

    /* Power on device if power control device is specified */
    if (desc->power_ctrl_device) {
        ret = esp_board_device_power_ctrl(name, true);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to power on device: %s via power control device: %s", name, desc->power_ctrl_device);
        }
    }

    /* Find device handle */
    esp_board_device_handle_t *handle = esp_board_find_device_handle(name);
    ESP_BOARD_RETURN_ON_DEVICE_NOT_FOUND(handle, name, TAG, "Device handle %s not found", name);

    /* Check if init function exists */
    ESP_BOARD_RETURN_ON_FALSE(handle->init, ESP_BOARD_ERR_DEVICE_NO_INIT, TAG, "No init function for device: %s", name);

    /* Increment reference count */
    handle->ref_count++;

    /* Initialize device if not already initialized */
    if (handle->device_handle) {
        ESP_LOGI(TAG, "Device %s already initialized, ref_count: %d", name, handle->ref_count);
        return ESP_OK;
    } else {
        uint16_t cfg_size = 0;
        const void *cfg = find_effective_cfg(name, desc, &cfg_size);

        ret = handle->init((void *)cfg, cfg_size, &handle->device_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init device: %s", name);
            handle->ref_count--;  /* Decrement ref_count on failure */
            return ret;
        }
    }
    ESP_LOGD(TAG, "initialized device %s ref_count: %d device_handle:%p", name, handle->ref_count, handle->device_handle);
    return ret;
}

esp_err_t esp_board_device_get_handle(const char *name, void **device_handle)
{
    ESP_BOARD_RETURN_ON_FALSE(name && device_handle, ESP_BOARD_ERR_DEVICE_INVALID_ARG, TAG, "Invalid args");

    /* Find device handle */
    esp_board_device_handle_t *handle = esp_board_find_device_handle(name);
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
    const esp_board_device_desc_t *desc = esp_board_find_device_desc(name);
    ESP_BOARD_RETURN_ON_DEVICE_NOT_FOUND(desc, name, TAG, "Device descriptor %s not found", name);

    if (!desc->cfg) {
        ESP_LOGW(TAG, "Device %s has no config", name);
        *config = NULL;
        return ESP_BOARD_ERR_DEVICE_NOT_SUPPORTED;
    }

    uint16_t cfg_size = 0;
    const void *effective_cfg = find_effective_cfg(name, desc, &cfg_size);

    *config = (void *)effective_cfg;
    ESP_LOGI(TAG, "Device %s config found: %p (size: %d)", name, effective_cfg, cfg_size);
    return ESP_OK;
}

esp_err_t esp_board_device_get_config_by_handle(void *device_handle, void **config)
{
    if (device_handle == NULL || config == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_BOARD_ERR_DEVICE_INVALID_ARG;
    }
    *config = NULL;
    const esp_board_device_handle_t *board_device = esp_board_device_find_by_handle(device_handle);
    if (board_device == NULL) {
        ESP_LOGE(TAG, "Device handle[%p] not found", device_handle);
        return ESP_BOARD_ERR_DEVICE_NOT_FOUND;
    }
    esp_err_t ret = esp_board_device_get_config(board_device->name, config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Get device %s config failed", board_device->name);
        return ret;
    }
    return ESP_OK;
}

esp_err_t esp_board_device_set_ops(const char *name, esp_board_device_init_func init, esp_board_device_deinit_func deinit)
{
    ESP_BOARD_RETURN_ON_FALSE(name && init && deinit, ESP_BOARD_ERR_DEVICE_INVALID_ARG, TAG, "Invalid args");

    /* Find device handle */
    esp_board_device_handle_t *handle = esp_board_find_device_handle(name);
    ESP_BOARD_RETURN_ON_DEVICE_NOT_FOUND(handle, name, TAG, "Device handle %s not found", name);

    /* Set functions */
    handle->init = init;
    handle->deinit = deinit;
    ESP_LOGI(TAG, "Set functions for device: %s", name);

    return ESP_OK;
}

esp_err_t esp_board_device_override_config(const char *name, const void *config, uint16_t config_size)
{
    ESP_BOARD_RETURN_ON_FALSE(name && config && config_size, ESP_BOARD_ERR_DEVICE_INVALID_ARG, TAG, "Invalid args");

    const esp_board_device_desc_t *desc = esp_board_find_device_desc(name);
    ESP_BOARD_RETURN_ON_DEVICE_NOT_FOUND(desc, name, TAG, "Device %s not found", name);
    ESP_BOARD_RETURN_ON_FALSE(desc->cfg && desc->cfg_size, ESP_BOARD_ERR_DEVICE_NOT_SUPPORTED, TAG, "Device %s has no config", name);

    if (config_size != desc->cfg_size) {
        ESP_LOGW(TAG, "Device %s override size differs from generated config size, default: %u, override: %u",
                 name, desc->cfg_size, config_size);
    }

    cfg_override_node_t **old_link = find_cfg_override_link(name);
    cfg_override_node_t *old_node = old_link ? *old_link : NULL;
    cfg_override_node_t *node = malloc(sizeof(cfg_override_node_t) + config_size);
    ESP_BOARD_RETURN_ON_FALSE(node, ESP_ERR_NO_MEM, TAG, "Failed to allocate config override for %s", name);

    node->name = desc->name;
    node->cfg_size = config_size;
    node->cfg = (void *)(node + 1);
    memcpy(node->cfg, config, config_size);

    if (old_node) {
        node->next = old_node->next;
        *old_link = node;
        free(old_node);
    } else {
        node->next = s_cfg_override_list;
        s_cfg_override_list = node;
    }

    ESP_LOGI(TAG, "Config override set for device %s", name);
    return ESP_OK;
}

esp_err_t esp_board_device_restore_config(const char *name)
{
    ESP_BOARD_RETURN_ON_FALSE(name, ESP_BOARD_ERR_DEVICE_INVALID_ARG, TAG, "name is null");

    cfg_override_node_t **link = &s_cfg_override_list;

    while (*link) {
        cfg_override_node_t *node = *link;
        if (strcmp(node->name, name) == 0) {
            *link = node->next;
            free(node);
            ESP_LOGI(TAG, "Config override reset for device %s", name);
            return ESP_OK;
        }
        link = &node->next;
    }

    ESP_LOGW(TAG, "Config override for device %s not found", name);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t esp_board_device_deinit(const char *name)
{
    ESP_BOARD_RETURN_ON_FALSE(name, ESP_BOARD_ERR_DEVICE_INVALID_ARG, TAG, "name is null");

    /* Find device handle */
    esp_board_device_handle_t *handle = esp_board_find_device_handle(name);
    ESP_BOARD_RETURN_ON_DEVICE_NOT_FOUND(handle, name, TAG, "Device handle %s not found", name);
    if (!handle->device_handle) {
        ESP_LOGW(TAG, "Device %s not initialized", name);
        return ESP_OK;
    }

    /* Decrement reference count */
    handle->ref_count--;
    ESP_LOGI(TAG, "Deinit device %s ref_count: %d device_handle:%p", name, handle->ref_count, handle->device_handle);

    /* Only deinitialize if ref_count reaches 0 */
    if (handle->ref_count == 0) {
        ESP_BOARD_RETURN_ON_FALSE(handle->deinit, ESP_BOARD_ERR_DEVICE_NO_INIT, TAG, "No deinit function for device: %s", name);
        /* Deinitialize device */
        const esp_board_device_desc_t *desc = esp_board_find_device_desc(name);
        ESP_BOARD_RETURN_ON_DEVICE_NOT_FOUND(desc, name, TAG, "Device %s not found", name);
        if (desc->power_ctrl_device != NULL) {
            /* Deinitialize power ctrl device if needed */
            esp_board_device_deinit(desc->power_ctrl_device);
        }
        esp_err_t ret = handle->deinit(handle->device_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to deinit device: %s", name);
            return ret;
        }
        handle->device_handle = NULL;
    } else {
        ESP_LOGW(TAG, "Device %s still has %d references, not deinitializing", name, handle->ref_count);
    }
    return ESP_OK;
}

esp_err_t esp_board_device_show(const char *name)
{
    if (name) {
        /* Show information for specific device */
        const esp_board_device_desc_t *desc = esp_board_find_device_desc(name);
        ESP_BOARD_RETURN_ON_DEVICE_NOT_FOUND(desc, name, TAG, "Device %s not found", name);

        esp_board_device_handle_t *handle = esp_board_find_device_handle(name);
        ESP_LOGI(TAG, "Device %s:", name);
        ESP_LOGI(TAG, "  Type: %s", desc->type);
        ESP_LOGI(TAG, "  Config size: %d", desc->cfg_size);
        if (handle) {
            ESP_LOGI(TAG, "  Handle: %p", handle->device_handle);
            ESP_LOGI(TAG, "  Status: %s", handle->device_handle ? "Initialized" : "Not initialized");
            ESP_LOGI(TAG, "  Ref count: %d", handle->ref_count);
        } else {
            ESP_LOGI(TAG, "  No handle found");
        }
    } else {
        /* Show information for all devices */
        const esp_board_device_desc_t *desc = g_esp_board_devices;
        while (desc && desc->name) {
            esp_board_device_handle_t *handle = esp_board_find_device_handle(desc->name);
            ESP_LOGI(TAG, "Device %s:", desc->name);
            ESP_LOGI(TAG, "  Type: %s", desc->type);
            ESP_LOGI(TAG, "  Config size: %d", desc->cfg_size);
            if (handle) {
                ESP_LOGI(TAG, "  Handle: %p", handle->device_handle);
                ESP_LOGI(TAG, "  Status: %s", handle->device_handle ? "Initialized" : "Not initialized");
                ESP_LOGI(TAG, "  Ref count: %d", handle->ref_count);
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
        esp_board_device_handle_t *handle = esp_board_find_device_handle(desc->name);
        do {
            ret = esp_board_device_deinit(desc->name);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to deinitialize device: %s", desc->name);
            }
        } while (handle->ref_count > 0);
        desc = desc->next;
    }
    return ESP_OK;
}

const esp_board_device_handle_t *esp_board_device_find_by_handle(void *device_handle)
{
    if (device_handle == NULL) {
        ESP_LOGE(TAG, "Device handle is NULL in find_device_by_handle");
        return NULL;
    }
    esp_board_device_handle_t *handle = g_esp_board_device_handles;
    while (handle) {
        if (handle->device_handle == device_handle) {
            return handle;
        }
        handle = handle->next;
    }
    return NULL;
}

esp_err_t esp_board_device_power_ctrl(const char *name, bool power_on)
{
#ifdef CONFIG_ESP_BOARD_DEV_POWER_CTRL_SUPPORT
    if (name == NULL) {
        ESP_LOGE(TAG, "Device name is NULL");
        return ESP_BOARD_ERR_MANAGER_INVALID_ARG;
    }
    /* Find the device description by name */
    const esp_board_device_desc_t *desc = esp_board_find_device_desc(name);
    ESP_BOARD_RETURN_ON_DEVICE_NOT_FOUND(desc, name, TAG, "Device %s not found", name);
    if (desc->power_ctrl_device == NULL) {
        ESP_LOGW(TAG, "Device %s has no power control device configured", name);
        return ESP_BOARD_ERR_DEVICE_NOT_SUPPORTED;
    }

    /* Find the power control device handle by name, init if not already done */
    esp_board_device_handle_t *handle = esp_board_find_device_handle(desc->power_ctrl_device);
    ESP_BOARD_RETURN_ON_DEVICE_NOT_FOUND(handle, desc->power_ctrl_device, TAG, "Power control device %s not found", desc->power_ctrl_device);
    if (handle->ref_count < 1) {
        ESP_LOGI(TAG, "Power control device %s is not initialized, try to init it first", desc->power_ctrl_device);
        esp_board_device_init(desc->power_ctrl_device);
    }
    const esp_board_device_desc_t *power_ctrl_desc = esp_board_find_device_desc(desc->power_ctrl_device);
    ESP_BOARD_RETURN_ON_DEVICE_NOT_FOUND(power_ctrl_desc, desc->power_ctrl_device, TAG, "Device %s not found", name);
    const char *sub_type = power_ctrl_desc->sub_type;
    ESP_BOARD_RETURN_ON_FALSE(sub_type, ESP_BOARD_ERR_MANAGER_INVALID_ARG, TAG,
                              "Power control device %s has no sub_type", desc->power_ctrl_device);

    /* Get the power control function by name */
    char power_ctrl_func_name[40];
    snprintf(power_ctrl_func_name, sizeof(power_ctrl_func_name), "%s_power_ctrl", sub_type);
    void *extra_func = NULL;
    if (esp_board_extra_func_get(power_ctrl_func_name, &extra_func) != 0) {
        ESP_LOGE(TAG, "Power control function %s not found for device %s", power_ctrl_func_name, desc->power_ctrl_device);
        return ESP_BOARD_ERR_MANAGER_INVALID_ARG;
    }
    esp_board_device_power_ctrl_func power_ctrl_func = (esp_board_device_power_ctrl_func)extra_func;

    /* Call the power control function */
    if (power_ctrl_func(handle->device_handle, name, power_on) != 0) {
        ESP_LOGE(TAG, "Failed to control power for device %s", name);
        return ESP_BOARD_ERR_MANAGER_INVALID_ARG;
    }

    return ESP_OK;
#else
    ESP_LOGW(TAG, "Power control support not enabled in configuration");
    return ESP_BOARD_ERR_DEVICE_NOT_SUPPORTED;
#endif  /* CONFIG_ESP_BOARD_DEV_POWER_CTRL_SUPPORT */
}

esp_err_t esp_board_device_callback_register(const char *name, void *call_back_func, void *user_data)
{
    ESP_BOARD_RETURN_ON_FALSE(name && call_back_func, ESP_BOARD_ERR_DEVICE_INVALID_ARG, TAG, "Invalid arguments");

    /* Find device descriptor */
    const esp_board_device_desc_t *dev_desc = esp_board_find_device_desc(name);
    ESP_BOARD_RETURN_ON_FALSE(dev_desc, ESP_BOARD_ERR_DEVICE_NOT_FOUND, TAG, "Device %s not found", name);

    /* Get device-specific callback registration function */
    void *extra_func = NULL;
    esp_err_t err = esp_board_extra_func_get(dev_desc->type, &extra_func);
    if (err != 0) {
        ESP_LOGE(TAG, "Callback register func for device type %s not found", dev_desc->type);
        return ESP_BOARD_ERR_DEVICE_NOT_SUPPORTED;
    }

    /* Get device handle */
    void *dev_handle = NULL;
    err = esp_board_device_get_handle(name, &dev_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get device handle for %s", name);
        return ESP_BOARD_ERR_DEVICE_NOT_FOUND;
    }

    /* Cast to appropriate function pointer type and call */
    esp_board_device_callback_register_func register_func = (esp_board_device_callback_register_func)extra_func;
    uint16_t cfg_size = 0;
    const void *cfg = find_effective_cfg(name, dev_desc, &cfg_size);

    err = register_func(dev_handle, cfg, cfg_size, call_back_func, user_data);
    if (err != 0) {
        ESP_LOGE(TAG, "Device callback registration failed for %s, ", name);
        return ESP_BOARD_ERR_DEVICE_INIT_FAILED;
    }

    ESP_LOGI(TAG, "Callback registered for device %s", name);
    return ESP_OK;
}

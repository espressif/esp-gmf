/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_board_manager.h"
#include "esp_board_periph.h"
#include "esp_board_manager_err.h"
#include "esp_board_find_utils.h"

static const char *TAG = "BOARD_MANAGER";

extern const esp_board_info_t g_esp_board_info;
extern const esp_board_device_desc_t g_esp_board_devices[];
extern esp_board_device_handle_t g_esp_board_device_handles[];
extern const esp_board_periph_desc_t g_esp_board_peripherals[];
extern esp_board_periph_entry_t g_esp_board_periph_handles[];

/* Manager state */
static bool s_manager_initialized = false;

esp_err_t esp_board_manager_init(void)
{
    if (s_manager_initialized) {
        ESP_LOGW(TAG, "Board manager already initialized");
        return ESP_BOARD_ERR_MANAGER_ALREADY_INIT;
    }

    /* Initialize all peripherals first */
    esp_err_t ret = esp_board_periph_init_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize all peripherals");
        return ret;
    }

    /* Initialize all devices */
    ret = esp_board_device_init_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize all devices");
        /* Try to deinitialize peripherals on failure */
        esp_board_periph_deinit_all();
        return ret;
    }

    s_manager_initialized = true;
    ESP_LOGI(TAG, "Board manager initialized");
    return ESP_OK;
}

esp_err_t esp_board_manager_get_periph_handle(const char *periph_name, void **periph_handle)
{
    ESP_BOARD_RETURN_ON_FALSE(periph_name && periph_handle, ESP_BOARD_ERR_MANAGER_INVALID_ARG, TAG, "Invalid arguments");

    esp_err_t ret = esp_board_periph_get_handle(periph_name, periph_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get peripheral handle for %s", periph_name);
        return ESP_BOARD_ERR_MANAGER_PERIPH_NOT_FOUND;
    }

    return ESP_OK;
}

esp_err_t esp_board_manager_get_device_handle(const char *dev_name, void **device_handle)
{
    ESP_BOARD_RETURN_ON_FALSE(dev_name && device_handle, ESP_BOARD_ERR_MANAGER_INVALID_ARG, TAG, "Invalid arguments");

    /* Find device handle */
    esp_err_t ret = esp_board_device_get_handle(dev_name, device_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Device %s not found, error: %d", dev_name, ret);
        return ESP_BOARD_ERR_MANAGER_DEVICE_NOT_FOUND;
    }

    return ESP_OK;
}

esp_err_t esp_board_manager_get_device_config(const char *dev_name, void **config)
{
    ESP_BOARD_RETURN_ON_FALSE(dev_name && config, ESP_BOARD_ERR_MANAGER_INVALID_ARG, TAG, "Invalid arguments");

    esp_err_t ret = esp_board_device_get_config(dev_name, config);
    return ret;
}

esp_err_t esp_board_manager_get_periph_config(const char *periph_name, void **config)
{
    ESP_BOARD_RETURN_ON_FALSE(periph_name && config, ESP_BOARD_ERR_MANAGER_INVALID_ARG, TAG, "Invalid arguments");

    esp_err_t ret = esp_board_periph_get_config(periph_name, config);
    return ret;
}

esp_err_t esp_board_manager_get_board_info(esp_board_info_t *board_info)
{
    ESP_BOARD_RETURN_ON_FALSE(board_info, ESP_BOARD_ERR_MANAGER_INVALID_ARG, TAG, "Invalid arguments");
    *board_info = g_esp_board_info;
    return ESP_OK;
}

esp_err_t esp_board_manager_register_device_handle(esp_board_device_handle_t *reg_handle)
{
    ESP_BOARD_RETURN_ON_FALSE(reg_handle, ESP_BOARD_ERR_DEVICE_INVALID_ARG, TAG, "Invalid args");

    /* Check reg_handle validity */
    ESP_BOARD_RETURN_ON_FALSE(reg_handle->name && reg_handle->type, ESP_BOARD_ERR_DEVICE_INVALID_ARG, TAG, "Invalid name or type");
    ESP_BOARD_RETURN_ON_FALSE(reg_handle->init && reg_handle->deinit, ESP_BOARD_ERR_DEVICE_INVALID_ARG, TAG, "Init/deinit function missing");
    ESP_BOARD_RETURN_ON_FALSE(reg_handle->device_handle == NULL, ESP_BOARD_ERR_DEVICE_INVALID_ARG, TAG, "Device handle is NULL");

    /* Insert reg_handle into g_esp_board_device_handles list */
    /* Since g_esp_board_device_handles is an array, we need to find the last element */
    esp_board_device_handle_t *current = &g_esp_board_device_handles[0];
    while (current->next && current->next->name) {
        current = current->next;
    }
    current->next = reg_handle;
    reg_handle->next = NULL;
    ESP_LOGI(TAG, "Registered device handle: %s", reg_handle->name);

    return ESP_OK;
}

esp_err_t esp_board_manager_init_device_by_name(const char *dev_name)
{
    ESP_BOARD_RETURN_ON_FALSE(dev_name, ESP_BOARD_ERR_MANAGER_INVALID_ARG, TAG, "Invalid arguments");

    /* First check if device exists */
    const esp_board_device_desc_t *dev_desc = esp_board_find_device_desc(dev_name);
    if (dev_desc == NULL) {
        ESP_LOGE(TAG, "Device %s not found", dev_name);
        return ESP_BOARD_ERR_MANAGER_DEVICE_NOT_FOUND;
    }

    /* Initialize the device */
    esp_err_t ret = esp_board_device_init(dev_name);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize device %s", dev_name);
        return ret;
    }

    ESP_LOGI(TAG, "Device %s initialized", dev_name);
    return ESP_OK;
}

esp_err_t esp_board_manager_deinit_device_by_name(const char *dev_name)
{
    ESP_BOARD_RETURN_ON_FALSE(dev_name, ESP_BOARD_ERR_MANAGER_INVALID_ARG, TAG, "Invalid arguments");

    /* Deinitialize the device */
    esp_err_t ret = esp_board_device_deinit(dev_name);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize device %s", dev_name);
        return ret;
    }

    ESP_LOGI(TAG, "Device %s deinitialized", dev_name);
    return ESP_OK;
}

esp_err_t esp_board_manager_print_board_info(void)
{
    ESP_LOGI(TAG, "Board Information:");
    ESP_LOGI(TAG, "=================");
    ESP_LOGI(TAG, "Name: %s", g_esp_board_info.name);
    ESP_LOGI(TAG, "Chip: %s", g_esp_board_info.chip);
    ESP_LOGI(TAG, "Version: %s", g_esp_board_info.version);
    ESP_LOGI(TAG, "Description: %s", g_esp_board_info.description);
    ESP_LOGI(TAG, "Manufacturer: %s", g_esp_board_info.manufacturer);
    ESP_LOGI(TAG, "=================\r\n");
    return ESP_OK;
}

esp_err_t esp_board_manager_print(void)
{
    printf("\r\n");
    ESP_LOGI(TAG, "Board Manager Status:");
    ESP_LOGI(TAG, "-------------------");

    ESP_LOGI(TAG, "Peripherals:");
    esp_board_periph_show(NULL);
    ESP_LOGI(TAG, "Devices:");
    esp_board_device_show(NULL);

    /* Show device-peripheral associations */
    ESP_LOGI(TAG, "Device-Peripheral Associations:");
    const esp_board_device_desc_t *dev_desc = g_esp_board_devices;
    while (dev_desc) {
        esp_board_device_handle_t *dev_handle = esp_board_find_device_handle(dev_desc->name);
        if (dev_handle && dev_handle->device_handle) {
            ESP_LOGI(TAG, "  Device: %s (%s)", dev_desc->name, dev_desc->type);
        }
        dev_desc = dev_desc->next;
    }

    return ESP_OK;
}

esp_err_t esp_board_manager_deinit(void)
{
    if (!s_manager_initialized) {
        ESP_LOGW(TAG, "Board manager not initialized");
        return ESP_BOARD_ERR_MANAGER_NOT_INIT;
    }

    /* First deinitialize all devices */
    esp_err_t ret = esp_board_device_deinit_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize all devices");
        return ret;
    }
    ESP_LOGI(TAG, "All devices deinitialized");

    /* Then deinitialize all peripherals */
    ret = esp_board_periph_deinit_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize all peripherals");
        return ret;
    }

    s_manager_initialized = false;
    ESP_LOGI(TAG, "Board manager deinitialized");
    return ESP_OK;
}

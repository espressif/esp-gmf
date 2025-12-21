/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"
#include "esp_err.h"
#include "dev_gpio_ctrl.h"
#include "driver/gpio.h"
#include "esp_board_periph.h"
#include "esp_board_device.h"
#include <string.h>

static const char *TAG = "DEV_GPIO_CTRL";

int dev_gpio_ctrl_init(void *cfg, int cfg_size, void **device_handle)
{
    if (!cfg || !device_handle) {
        ESP_LOGE(TAG, "Invalid parameters, cfg: %p, device_handle: %p", cfg, device_handle);
        return -1;
    }

    const dev_gpio_ctrl_config_t *config = (const dev_gpio_ctrl_config_t *)cfg;

    // Get GPIO peripheral handle
    periph_gpio_handle_t *gpio_handle = NULL;
    if (!config->gpio_name || strlen(config->gpio_name) == 0) {
        ESP_LOGE(TAG, "No GPIO peripheral name configured for device: %s", config->name);
        return -1;
    }

    int ret = esp_board_periph_ref_handle(config->gpio_name, (void*)&gpio_handle);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to get GPIO peripheral handle '%s': %d", config->gpio_name, ret);
        return -1;
    }

    // Get GPIO peripheral config
    periph_gpio_config_t gpio_config = {0};
    esp_err_t config_ret = esp_board_periph_get_config(config->gpio_name, (void*)&gpio_config);
    if (config_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get GPIO peripheral config '%s': %s", config->gpio_name, esp_err_to_name(config_ret));
        return -1;
    }

    // Set the active level using the GPIO handle
    esp_err_t err = gpio_set_level(gpio_handle->gpio_num, config->active_level);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set active level for GPIO %d: %s", gpio_handle->gpio_num, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Initialized GPIO control device '%s': gpio=%d, set initial level:%d (active_level=%d, default_level=%d)",
             config->name, gpio_handle->gpio_num, config->active_level, config->active_level, config->default_level);

    // Store the GPIO handle for later use
    *device_handle = gpio_handle;
    return 0;
}

int dev_gpio_ctrl_deinit(void *device_handle)
{
    if (!device_handle) {
        ESP_LOGE(TAG, "Invalid handle");
        return -1;
    }

    dev_gpio_ctrl_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    if (cfg) {
        esp_board_periph_unref_handle(cfg->gpio_name);
    }

    // Cast back to GPIO handle type
    periph_gpio_handle_t *gpio_handle = (periph_gpio_handle_t *)device_handle;
    if (!gpio_handle) {
        ESP_LOGE(TAG, "Invalid GPIO handle");
        return -1;
    }
    ESP_LOGI(TAG, "GPIO control device gpio %d deinitialized", gpio_handle->gpio_num);
    return 0;
}

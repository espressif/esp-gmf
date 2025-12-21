/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_board_periph.h"
#include "esp_board_entry.h"
#include "esp_board_device.h"
#include "dev_power_ctrl.h"
#include "periph_gpio.h"

static const char *TAG = "DEV_POWER_CTRL_SUB_GPIO";

static int dev_power_ctrl_sub_gpio_power_control(void *dev_handle, const char *device_name, bool power_on)
{
    // Get configuration to determine the active level
    dev_power_ctrl_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(dev_handle, (void **)&cfg);
    if (cfg == NULL) {
        ESP_LOGE(TAG, "Failed to get configuration");
        return -1;
    }

    dev_power_ctrl_handle_t *power_ctrl_handle = (dev_power_ctrl_handle_t *)dev_handle;
    periph_gpio_handle_t *gpio_handle = (periph_gpio_handle_t *)power_ctrl_handle->periph_handle;
    uint8_t level;
    if (power_on) {
        level = cfg->sub_cfg.gpio.active_level;
    } else {
        // When turning off, set to the opposite of active level
        level = !cfg->sub_cfg.gpio.active_level;
    }

    esp_err_t err = gpio_set_level(gpio_handle->gpio_num, level);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO level to %d: %s", level, esp_err_to_name(err));
        return -1;
    }

    ESP_LOGI(TAG, "GPIO power control: %s, level: %d for device: %s", power_on ? "ON" : "OFF", level, device_name);
    return 0;
}

int dev_power_ctrl_sub_gpio_init(void *cfg, int cfg_size, void **device_handle)
{
    // No need to check parameters here, it will be checked in dev_power_ctrl_init
    const dev_power_ctrl_config_t *config = (const dev_power_ctrl_config_t *)cfg;
    dev_power_ctrl_handle_t *handle = calloc(1, sizeof(dev_power_ctrl_handle_t));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for dev_power_ctrl_sub_gpio");
        return -1;
    }
    ESP_LOGI(TAG, "Initializing GPIO power control: %s", config->sub_cfg.gpio.gpio_name);

    // Initialize GPIO peripheral using esp_board_periph_ref_handle
    void *periph_handle = NULL;
    int ret = esp_board_periph_ref_handle(config->sub_cfg.gpio.gpio_name, &periph_handle);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to initialize GPIO peripheral: %s", config->sub_cfg.gpio.gpio_name);
        free(handle);
        return -1;
    }

    // Store the peripheral handle in device handle
    handle->periph_handle = periph_handle;
    *device_handle = handle;
    ESP_LOGI(TAG, "GPIO power control initialized successfully");
    return 0;
}

int dev_power_ctrl_sub_gpio_deinit(void *device_handle)
{
    dev_power_ctrl_handle_t *handle = (dev_power_ctrl_handle_t *)device_handle;

    // Get configuration to find the peripheral name
    dev_power_ctrl_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    if (cfg && handle->periph_handle) {
        // Deinitialize GPIO peripheral using esp_board_periph_unref_handle
        int ret = esp_board_periph_unref_handle(cfg->sub_cfg.gpio.gpio_name);
        if (ret != 0) {
            ESP_LOGE(TAG, "GPIO peripheral deinit failed with error: %d", ret);
            // Continue with cleanup even if deinit failed
        } else {
            ESP_LOGI(TAG, "GPIO peripheral deinitialized successfully");
        }
    }
    free(handle);
    return 0;
}

ESP_BOARD_ENTRY_IMPLEMENT(gpio, dev_power_ctrl_sub_gpio_init, dev_power_ctrl_sub_gpio_deinit);
DEVICE_EXTRA_FUNC_REGISTER(gpio_power_ctrl, dev_power_ctrl_sub_gpio_power_control);

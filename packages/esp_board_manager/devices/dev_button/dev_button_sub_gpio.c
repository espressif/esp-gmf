/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"
#include "esp_board_periph.h"
#include "esp_board_device.h"
#include "esp_board_entry.h"
#include "dev_button.h"
#include "periph_gpio.h"
#include "button_gpio.h"

static const char *TAG = "DEV_BUTTON_SUB_GPIO";

int dev_button_sub_gpio_init(void *cfg, int cfg_size, void **device_handle)
{
    // Get GPIO handle and config from board manager
    const dev_button_config_t *config = (const dev_button_config_t *)cfg;
    button_handle_t btn_handle = NULL;
    periph_gpio_handle_t *gpio_handle = NULL;
    esp_err_t ret = esp_board_periph_ref_handle(config->sub_cfg.gpio.gpio_name, (void **)&gpio_handle);
    if (ret != ESP_OK || gpio_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get GPIO handle for %s: %s", config->sub_cfg.gpio.gpio_name, esp_err_to_name(ret));
        return -1;
    }

    // Get GPIO configuration to get the GPIO number
    periph_gpio_config_t *gpio_cfg = NULL;
    ret = esp_board_periph_get_config(config->sub_cfg.gpio.gpio_name, (void **)&gpio_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get GPIO config for %s: %s", config->sub_cfg.gpio.gpio_name, esp_err_to_name(ret));
        goto cleanup;
    }

    ESP_LOGI(TAG, "Initializing  GPIO button on port %d, active level: %d, power_save: %d, disable_pull: %d",
             gpio_handle->gpio_num, config->sub_cfg.gpio.active_level,
             config->sub_cfg.gpio.enable_power_save, config->sub_cfg.gpio.disable_pull);

    // Prepare GPIO button configuration
    button_gpio_config_t gpio_button_cfg = {
        .gpio_num = gpio_handle->gpio_num,
        .active_level = config->sub_cfg.gpio.active_level,
        .enable_power_save = config->sub_cfg.gpio.enable_power_save,
        .disable_pull = config->sub_cfg.gpio.disable_pull,
    };

    // Create GPIO button
    ret = iot_button_new_gpio_device(&config->button_timing_cfg, &gpio_button_cfg, &btn_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create  GPIO button: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    dev_button_handles_t *button_handles = calloc(1, sizeof(dev_button_handles_t));
    if (button_handles == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for dev_button_sub_gpio");
        goto cleanup;
    }
    button_handles->num_buttons = 1;
    button_handles->button_handles[0] = btn_handle;
    *device_handle = button_handles;
    return 0;
cleanup:
    if (btn_handle) {
        iot_button_delete(btn_handle);
    }
    esp_board_periph_unref_handle(config->sub_cfg.gpio.gpio_name);
    return -1;
}

int dev_button_sub_gpio_deinit(void *device_handle)
{
    if (device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    dev_button_handles_t *button_handles = (dev_button_handles_t *)device_handle;
    if (button_handles->button_handles[0]) {
        iot_button_delete(button_handles->button_handles[0]);
        button_handles->button_handles[0] = NULL;
    }

    // Unreference GPIO peripheral handle
    dev_button_config_t *cfg = NULL;
    esp_err_t ret = esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get button config: %s", esp_err_to_name(ret));
    }
    if (cfg && cfg->sub_cfg.gpio.gpio_name) {
        esp_board_periph_unref_handle(cfg->sub_cfg.gpio.gpio_name);
    }
    free(device_handle);
    ESP_LOGI(TAG, "Successfully deinitialized  GPIO button");
    return ret == ESP_OK ? 0 : -1;
}

ESP_BOARD_ENTRY_IMPLEMENT(gpio, dev_button_sub_gpio_init, dev_button_sub_gpio_deinit);

/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"
#include "esp_board_entry.h"
#include "esp_board_device.h"
#include "dev_display_lcd.h"
#include "esp_lcd_panel_ops.h"

static const char *TAG = "DEV_DISPLAY_LCD";

int dev_display_lcd_init(void *cfg, int cfg_size, void **device_handle)
{
    if (cfg == NULL || device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    if (cfg_size != sizeof(dev_display_lcd_config_t)) {
        ESP_LOGE(TAG, "Invalid config size");
        return -1;
    }
    esp_err_t ret = ESP_FAIL;
    dev_display_lcd_handles_t *handle = NULL;

    const dev_display_lcd_config_t *config = (const dev_display_lcd_config_t *)cfg;
    ESP_LOGI(TAG, "Initializing LCD display: %s, chip: %s, sub_type: %s",
             config->name, config->chip, config->sub_type);
    const esp_board_entry_desc_t *entry_desc = esp_board_entry_find_desc(config->sub_type);
    if (entry_desc == NULL) {
        ESP_LOGE(TAG, "Failed to find sub device: %s", config->sub_type);
        return -1;
    }
    if (entry_desc->init_func == NULL) {
        ESP_LOGE(TAG, "LCD sub_type '%s' has no init function", config->sub_type);
        return -1;
    }
    ret = entry_desc->init_func((void *)config, cfg_size, (void **)&handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize sub device: %s", config->sub_type);
        return -1;
    }

    // Reset LCD panel if needed
    if (config->need_reset) {
        ret = esp_lcd_panel_reset(handle->panel_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reset LCD panel: %s", esp_err_to_name(ret));
            entry_desc->deinit_func(handle);
            return -1;
        }
    }

    // Initialize LCD panel
    ret = esp_lcd_panel_init(handle->panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LCD panel: %s", esp_err_to_name(ret));
        entry_desc->deinit_func(handle);
        return -1;
    }

    // Invert color if needed
    if (config->invert_color) {
        ret = esp_lcd_panel_invert_color(handle->panel_handle, true);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to invert color on LCD panel: %s", esp_err_to_name(ret));
        }
    }

    // Turn on display
    esp_lcd_panel_disp_on_off(handle->panel_handle, true);
    ESP_LOGI(TAG, "Successfully initialized LCD display: %s (sub_type: %s), panel: %p, io: %p",
             config->name, config->sub_type, handle->panel_handle, handle->io_handle);
    *device_handle = handle;
    return 0;
}

int dev_display_lcd_deinit(void *device_handle)
{
    if (device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    dev_display_lcd_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    if (cfg) {
        const esp_board_entry_desc_t *desc = esp_board_entry_find_desc(cfg->sub_type);
        if (desc && desc->deinit_func) {
            int ret = desc->deinit_func(device_handle);
            if (ret != 0) {
                ESP_LOGE(TAG, "LCD(sub_type: %s) deinit failed with error: %d", cfg->sub_type, ret);
            } else {
                ESP_LOGI(TAG, "LCD(sub_type: %s) deinitialized successfully", cfg->sub_type);
            }
        } else {
            ESP_LOGW(TAG, "No custom deinit function found for sub type '%s'", cfg->sub_type);
        }
    } else {
        ESP_LOGE(TAG, "Failed to get config from device handle");
    }
    return 0;
}

/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "dev_lcd_touch.h"
#include "esp_board_device.h"
#include "esp_board_entry.h"

static const char *TAG = "DEV_LCD_TOUCH";

static const char *dev_lcd_touch_resolve_entry_name(const char *sub_type)
{
    if (sub_type && strcmp(sub_type, ESP_BOARD_DEVICE_LCD_TOUCH_SUB_TYPE_I2C) == 0) {
        return "lcd_touch_sub_i2c";
    }
    if (sub_type && strcmp(sub_type, ESP_BOARD_DEVICE_LCD_TOUCH_SUB_TYPE_SPI) == 0) {
        return "lcd_touch_sub_spi";
    }
    return sub_type;
}

int dev_lcd_touch_init(void *cfg, int cfg_size, void **device_handle)
{
    if (!cfg || cfg_size != sizeof(dev_lcd_touch_config_t) || !device_handle) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    const dev_lcd_touch_config_t *config = (const dev_lcd_touch_config_t *)cfg;
    if (config->sub_type == NULL) {
        ESP_LOGE(TAG, "Invalid LCD touch sub_type");
        return -1;
    }

    const esp_board_entry_desc_t *entry_desc = esp_board_entry_find_desc(dev_lcd_touch_resolve_entry_name(config->sub_type));
    if (!entry_desc || !entry_desc->init_func) {
        ESP_LOGE(TAG, "Failed to find LCD touch sub device: %s", config->sub_type);
        return -1;
    }

    int ret = entry_desc->init_func(cfg, cfg_size, device_handle);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to initialize LCD touch sub device: %s", config->sub_type);
        return ret;
    }

    ESP_LOGI(TAG, "Successfully initialized LCD touch: %s, chip: %s, sub_type: %s",
             config->name,
             config->chip ? config->chip : "(unknown)",
             config->sub_type);
    return 0;
}

int dev_lcd_touch_deinit(void *device_handle)
{
    if (!device_handle) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    dev_lcd_touch_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    if (!cfg) {
        ESP_LOGE(TAG, "Failed to get LCD touch config by handle");
        return -1;
    }
    if (cfg->sub_type == NULL) {
        ESP_LOGE(TAG, "Invalid LCD touch sub_type");
        return -1;
    }

    const esp_board_entry_desc_t *desc = esp_board_entry_find_desc(dev_lcd_touch_resolve_entry_name(cfg->sub_type));
    if (!desc || !desc->deinit_func) {
        ESP_LOGE(TAG, "No deinit function found for LCD touch sub type: %s", cfg->sub_type);
        return -1;
    }

    int ret = desc->deinit_func(device_handle);
    if (ret != 0) {
        ESP_LOGE(TAG, "LCD touch sub type %s deinit failed: %d", cfg->sub_type, ret);
        return ret;
    }

    ESP_LOGI(TAG, "Successfully deinitialized LCD touch: %s, sub_type: %s",
             cfg->name, cfg->sub_type);
    return 0;
}

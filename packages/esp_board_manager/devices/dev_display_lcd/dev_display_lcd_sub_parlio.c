/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_board_device.h"
#include "esp_board_entry.h"
#include "dev_display_lcd.h"

static const char *TAG = "DEV_DISPLAY_LCD_SUB_PARLIO";

extern esp_err_t lcd_panel_factory_entry_t(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel);

int dev_display_lcd_sub_parlio_init(void *cfg, int cfg_size, void **device_handle)
{
    dev_display_lcd_config_t *lcd_cfg = (dev_display_lcd_config_t *)cfg;
    dev_display_lcd_handles_t *lcd_handles = calloc(1, sizeof(dev_display_lcd_handles_t));
    if (lcd_handles == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for dev_display_lcd_sub_parlio");
        return -1;
    }

    ESP_LOGI(TAG, "Initializing PARLIO LCD display: %s, chip: %s", lcd_cfg->name, lcd_cfg->chip);

    esp_err_t ret = esp_lcd_new_panel_io_parl(&lcd_cfg->sub_cfg.parlio.io_parl, &lcd_handles->io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create PARLIO panel IO: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    ret = lcd_panel_factory_entry_t(lcd_handles->io_handle, &lcd_cfg->sub_cfg.parlio.panel_config, &lcd_handles->panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LCD panel: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(lcd_handles->io_handle);
        goto cleanup;
    }
    *device_handle = lcd_handles;
    return 0;
cleanup:
    free(lcd_handles);
    return -1;
}

int dev_display_lcd_sub_parlio_deinit(void *device_handle)
{
    if (device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    dev_display_lcd_handles_t *lcd_handles = (dev_display_lcd_handles_t *)device_handle;
    dev_display_lcd_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    if (cfg == NULL) {
        ESP_LOGE(TAG, "Failed to get device config");
        free(device_handle);
        return -1;
    }

    if (lcd_handles->panel_handle) {
        esp_lcd_panel_del(lcd_handles->panel_handle);
        lcd_handles->panel_handle = NULL;
    }

    if (lcd_handles->io_handle) {
        esp_lcd_panel_io_del(lcd_handles->io_handle);
        lcd_handles->io_handle = NULL;
    }

    free(device_handle);
    return 0;
}

ESP_BOARD_ENTRY_IMPLEMENT(parlio, dev_display_lcd_sub_parlio_init, dev_display_lcd_sub_parlio_deinit);

/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_err.h"
#include "esp_log.h"
#include "esp_board_periph.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "dev_display_lcd.h"
#include "esp_board_device.h"
#include "esp_board_entry.h"

static const char *TAG = "DEV_DISPLAY_LCD_SUB_DSI";

extern esp_err_t lcd_dsi_panel_factory_entry_t(esp_lcd_dsi_bus_handle_t dsi_handle, dev_display_lcd_config_t *lcd_cfg, dev_display_lcd_handles_t *lcd_handles);

int dev_display_lcd_sub_dsi_init(void *cfg, int cfg_size, void **device_handle)
{
    esp_err_t ret;
    dev_display_lcd_config_t *lcd_cfg = (dev_display_lcd_config_t *)cfg;
    dev_display_lcd_handles_t *lcd_handles = (dev_display_lcd_handles_t *)*device_handle;

    ESP_LOGI(TAG, "Initializing DSI LCD display: %s, chip: %s", lcd_cfg->name, lcd_cfg->chip);
    esp_lcd_dsi_bus_handle_t dsi_handle = NULL;
    if (lcd_cfg->sub_cfg.dsi.dsi_name && strlen(lcd_cfg->sub_cfg.dsi.dsi_name) > 0) {
        ret = esp_board_periph_ref_handle(lcd_cfg->sub_cfg.dsi.dsi_name, (void **)&dsi_handle);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to get DSI peripheral handle: %d", ret);
            return -1;
        }
    } else {
        ESP_LOGE(TAG, "No DSI name configured for LCD display: %s", lcd_cfg->name);
        return -1;
    }

    ret = esp_lcd_new_panel_io_dbi(dsi_handle, &lcd_cfg->sub_cfg.dsi.dbi_config, &lcd_handles->io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LCD panel IO: %s", esp_err_to_name(ret));
        esp_board_periph_unref_handle(lcd_cfg->sub_cfg.dsi.dsi_name);
        return -1;
    }

    ret = lcd_dsi_panel_factory_entry_t(dsi_handle, lcd_cfg, lcd_handles);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LCD panel: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(lcd_handles->io_handle);
        esp_board_periph_unref_handle(lcd_cfg->sub_cfg.dsi.dsi_name);
        return -1;
    }
    return 0;
}

int dev_display_lcd_sub_dsi_deinit(void *device_handle)
{
    if (device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    dev_display_lcd_handles_t *lcd_handles = (dev_display_lcd_handles_t *)device_handle;
    dev_display_lcd_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    if (cfg == NULL) {
        ESP_LOGE(TAG, "Failed to get LCD config");
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

    esp_board_periph_unref_handle(cfg->sub_cfg.dsi.dsi_name);
    return 0;
}

ESP_BOARD_ENTRY_IMPLEMENT(dsi, dev_display_lcd_sub_dsi_init, dev_display_lcd_sub_dsi_deinit);

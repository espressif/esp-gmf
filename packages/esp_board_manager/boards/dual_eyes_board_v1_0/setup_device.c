/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "dev_display_lcd_spi.h"
#include "esp_board_device.h"
#include "esp_lcd_gc9a01.h"
#include "esp_log.h"

esp_err_t lcd_panel_factory_entry_t(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)
{
    int ret = esp_lcd_new_panel_gc9a01(io, panel_dev_config, ret_panel);
    if (ret != ESP_OK) {
        ESP_LOGE("lcd_panel_factory_entry_t", "New GC9A01 panel failed");
        return ret;
    }
    ESP_LOGI("lcd_panel_factory_entry_t", "New GC9A01 panel success");
    return ESP_OK;
}

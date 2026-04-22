/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Board-specific LCD panel factory for esp_hi_skill.
 */

#include <string.h>
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

static const char *TAG = "ESP_HI_SKILL_SETUP";

static const ili9341_lcd_init_cmd_t s_vendor_init_cmds[] = {
    {0x11, NULL, 0, 120},
    {0xB1, (uint8_t[]) {0x05, 0x3A, 0x3A}, 3, 0},
    {0xB2, (uint8_t[]) {0x05, 0x3A, 0x3A}, 3, 0},
    {0xB3, (uint8_t[]) {0x05, 0x3A, 0x3A, 0x05, 0x3A, 0x3A}, 6, 0},
    {0xB4, (uint8_t[]) {0x03}, 1, 0},
    {0xC0, (uint8_t[]) {0x44, 0x04, 0x04}, 3, 0},
    {0xC1, (uint8_t[]) {0xC0}, 1, 0},
    {0xC2, (uint8_t[]) {0x0D, 0x00}, 2, 0},
    {0xC3, (uint8_t[]) {0x8D, 0x6A}, 2, 0},
    {0xC4, (uint8_t[]) {0x8D, 0xEE}, 2, 0},
    {0xC5, (uint8_t[]) {0x08}, 1, 0},
    {0xE0, (uint8_t[]) {0x0F, 0x10, 0x03, 0x03, 0x07, 0x02, 0x00, 0x02, 0x07, 0x0C, 0x13, 0x38, 0x0A, 0x0E, 0x03, 0x10}, 16, 0},
    {0xE1, (uint8_t[]) {0x10, 0x0B, 0x04, 0x04, 0x10, 0x03, 0x00, 0x03, 0x03, 0x09, 0x17, 0x33, 0x0B, 0x0C, 0x06, 0x10}, 16, 0},
    {0x35, (uint8_t[]) {0x00}, 1, 0},
    {0x3A, (uint8_t[]) {0x05}, 1, 0},
    {0x36, (uint8_t[]) {0xC8}, 1, 0},
    {0x29, NULL, 0, 0},
    {0x2C, NULL, 0, 0},
};

static const ili9341_vendor_config_t s_vendor_config = {
    .init_cmds      = s_vendor_init_cmds,
    .init_cmds_size = sizeof(s_vendor_init_cmds) / sizeof(s_vendor_init_cmds[0]),
};

esp_err_t lcd_panel_factory_entry_t(esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_panel_dev_config_t *panel_dev_config,
                                    esp_lcd_panel_handle_t *ret_panel)
{
    esp_lcd_panel_dev_config_t panel_dev_cfg = {0};
    memcpy(&panel_dev_cfg, panel_dev_config, sizeof(esp_lcd_panel_dev_config_t));
    panel_dev_cfg.vendor_config = (void *)&s_vendor_config;

    esp_err_t ret = esp_lcd_new_panel_ili9341(io, &panel_dev_cfg, ret_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_ili9341 failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_lcd_panel_set_gap(*ret_panel, 1, 26);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_set_gap failed: %s", esp_err_to_name(ret));
        esp_err_t cleanup_ret = esp_lcd_panel_del(*ret_panel);
        if (cleanup_ret != ESP_OK) {
            ESP_LOGW(TAG, "esp_lcd_panel_del failed during cleanup: %s", esp_err_to_name(cleanup_ret));
        }
        *ret_panel = NULL;
        return ret;
    }

    return ESP_OK;
}

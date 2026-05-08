/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "esp_idf_version.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "dev_lcd_touch.h"
#include "esp_board_device.h"
#include "esp_board_entry.h"
#include "esp_board_periph.h"
#include "periph_i2c_internal.h"

static const char *TAG = "DEV_LCD_TOUCH_SUB_I2C";

extern esp_err_t lcd_touch_factory_entry_t(const esp_lcd_panel_io_handle_t io,
                                           const esp_lcd_touch_config_t *config,
                                           esp_lcd_touch_handle_t *tp);

int dev_lcd_touch_sub_i2c_init(void *cfg, int cfg_size, void **device_handle)
{
    if (!cfg || cfg_size != sizeof(dev_lcd_touch_config_t) || !device_handle) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    dev_lcd_touch_config_t *touch_cfg = (dev_lcd_touch_config_t *)cfg;
    dev_lcd_touch_handles_t *touch_handles = (dev_lcd_touch_handles_t *)calloc(1, sizeof(dev_lcd_touch_handles_t));
    if (!touch_handles) {
        ESP_LOGE(TAG, "Failed to allocate touch handles");
        return -1;
    }

    void *i2c_bus_handle = NULL;
    bool i2c_refd = false;
    esp_err_t ret = ESP_OK;
    uint16_t effective_addr = 0;
    uint16_t runtime_addr = 0;

    if (!touch_cfg->sub_cfg.i2c.i2c_name || touch_cfg->sub_cfg.i2c.i2c_name[0] == '\0') {
        ESP_LOGE(TAG, "No I2C name configured for LCD touch: %s", touch_cfg->name);
        goto init_err;
    }

    ret = esp_board_periph_ref_handle(touch_cfg->sub_cfg.i2c.i2c_name, &i2c_bus_handle);
    if (ret != ESP_OK || !i2c_bus_handle) {
        ESP_LOGE(TAG, "Failed to get I2C peripheral handle: %s", esp_err_to_name(ret));
        goto init_err;
    }
    i2c_refd = true;

    for (size_t i = 0; i < touch_cfg->sub_cfg.i2c.i2c_addr_count; i++) {
        uint16_t candidate = touch_cfg->sub_cfg.i2c.i2c_addr[i];
        if (candidate == 0) {
            continue;
        }
        if ((candidate & 0x1) != 0 || candidate > 0xfe) {
            ESP_LOGW(TAG, "Skip invalid 8-bit I2C address: 0x%02x", candidate);
            continue;
        }
        uint16_t candidate_runtime_addr = candidate >> 1;
        ret = i2c_master_probe((i2c_master_bus_handle_t)i2c_bus_handle, candidate_runtime_addr, 200);
        if (ret == ESP_OK) {
            effective_addr = candidate;
            runtime_addr = candidate_runtime_addr;
            break;
        }
    }

    if (effective_addr == 0) {
        ESP_LOGE(TAG, "No LCD touch found on I2C bus: %s", touch_cfg->sub_cfg.i2c.i2c_name);
        goto init_err;
    }

    ret = periph_i2c_set_effective_addr_internal(touch_cfg->sub_cfg.i2c.i2c_name, touch_cfg->name, effective_addr);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to publish effective I2C addr for %s: %s", touch_cfg->name, esp_err_to_name(ret));
    }

    esp_lcd_panel_io_i2c_config_t io_i2c_config = touch_cfg->sub_cfg.i2c.io_i2c_config;
    io_i2c_config.dev_addr = runtime_addr;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    ret = esp_lcd_new_panel_io_i2c((i2c_master_bus_handle_t)i2c_bus_handle, &io_i2c_config, &touch_handles->io_handle);
#else
    ret = esp_lcd_new_panel_io_i2c_v2((i2c_master_bus_handle_t)i2c_bus_handle, &io_i2c_config, &touch_handles->io_handle);
#endif  /* ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0) */
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LCD touch panel IO, addr: 0x%02x, err:%s", runtime_addr, esp_err_to_name(ret));
        goto init_err;
    }

    esp_lcd_touch_config_t touch_config = touch_cfg->touch_config;
    ret = lcd_touch_factory_entry_t(touch_handles->io_handle, &touch_config, &touch_handles->touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LCD touch driver: %s", esp_err_to_name(ret));
        goto init_err;
    }

    ESP_LOGI(TAG, "Successfully initialized LCD touch: %s, addr: 0x%02x, touch:%p, io:%p",
             touch_cfg->name, effective_addr, touch_handles->touch_handle, touch_handles->io_handle);
    *device_handle = touch_handles;
    return 0;

init_err:
    if (effective_addr != 0) {
        periph_i2c_clear_effective_addr_internal(touch_cfg->name);
    }
    if (touch_handles->touch_handle) {
        esp_lcd_touch_del(touch_handles->touch_handle);
    }
    if (touch_handles->io_handle) {
        esp_lcd_panel_io_del(touch_handles->io_handle);
    }
    if (i2c_refd) {
        esp_board_periph_unref_handle(touch_cfg->sub_cfg.i2c.i2c_name);
    }
    free(touch_handles);
    return -1;
}

int dev_lcd_touch_sub_i2c_deinit(void *device_handle)
{
    if (!device_handle) {
        return -1;
    }

    dev_lcd_touch_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    if (cfg) {
        esp_err_t ret = periph_i2c_clear_effective_addr_internal(cfg->name);
        if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Failed to clear effective I2C addr for %s: %s", cfg->name, esp_err_to_name(ret));
        }
    }

    dev_lcd_touch_handles_t *touch_handles = (dev_lcd_touch_handles_t *)device_handle;
    if (touch_handles->touch_handle) {
        esp_lcd_touch_del(touch_handles->touch_handle);
        touch_handles->touch_handle = NULL;
    }
    if (touch_handles->io_handle) {
        esp_lcd_panel_io_del(touch_handles->io_handle);
        touch_handles->io_handle = NULL;
    }
    if (cfg && cfg->sub_cfg.i2c.i2c_name) {
        esp_board_periph_unref_handle(cfg->sub_cfg.i2c.i2c_name);
    }

    free(touch_handles);
    return 0;
}

ESP_BOARD_ENTRY_IMPLEMENT(lcd_touch_sub_i2c, dev_lcd_touch_sub_i2c_init, dev_lcd_touch_sub_i2c_deinit);

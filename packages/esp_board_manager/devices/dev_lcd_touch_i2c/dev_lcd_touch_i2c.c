/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "esp_log.h"

#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"

#include "dev_lcd_touch_i2c.h"
#include "esp_board_periph.h"
#include "esp_board_device.h"

static const char *TAG = "DEV_LCD_TOUCH_I2C";

// Touch driver factory function declarations
extern esp_err_t lcd_touch_factory_entry_t(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *tp);

int dev_lcd_touch_i2c_init(void *cfg, int cfg_size, void **device_handle)
{
    dev_lcd_touch_i2c_config_t *touch_cfg = (dev_lcd_touch_i2c_config_t *)cfg;

    dev_lcd_touch_i2c_handles_t *touch_handles = (dev_lcd_touch_i2c_handles_t *)calloc(1, sizeof(dev_lcd_touch_i2c_handles_t));
    if (touch_handles == NULL) {
        ESP_LOGE(TAG, "Failed to allocate touch handles");
        return -1;
    }
    ESP_LOGI(TAG, "Initializing LCD touch: %s, chip: %s, addr: 0x%02" PRIx32, touch_cfg->name, touch_cfg->chip, touch_cfg->io_i2c_config.dev_addr);

    // Get I2C peripheral handle
    void *i2c_bus_handle = NULL;
    int ret = 0;
    if (touch_cfg->i2c_name && strlen(touch_cfg->i2c_name) > 0) {
        ret = esp_board_periph_ref_handle(touch_cfg->i2c_name, &i2c_bus_handle);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to get I2C peripheral handle: %d", ret);
            free(touch_handles);
            return -1;
        }
    } else {
        ESP_LOGE(TAG, "No I2C name configured for LCD touch: %s", touch_cfg->name);
        free(touch_handles);
        return -1;
    }

    esp_lcd_panel_io_i2c_config_t io_i2c_config = {};
    memcpy(&io_i2c_config, &touch_cfg->io_i2c_config, sizeof(io_i2c_config));
    io_i2c_config.dev_addr = 0x00;
    for (size_t i = 0; i < sizeof(touch_cfg->i2c_addr) / sizeof(touch_cfg->i2c_addr[0]); i++) {
        if (touch_cfg->i2c_addr[i] > 0) {
            ESP_LOGD(TAG, "I2C handle: %p, dev_addr[%zu]: 0x%02x, scl_speed_hz: %" PRIu32,
                i2c_bus_handle, i, touch_cfg->i2c_addr[i], touch_cfg->io_i2c_config.scl_speed_hz);
            ret = i2c_master_probe(i2c_bus_handle, touch_cfg->i2c_addr[i], 200/portTICK_PERIOD_MS);
            if (ret == ESP_OK) {
                io_i2c_config.dev_addr = touch_cfg->i2c_addr[i];
                break;
            }
        }
    }
    ret = esp_lcd_new_panel_io_i2c_v2((i2c_master_bus_handle_t)i2c_bus_handle, &io_i2c_config, &touch_handles->io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LCD panel IO for touch, i2c addr: %" PRIx32, io_i2c_config.dev_addr);
        free(touch_handles);
        return -1;
    }

    ESP_LOGD(TAG, "touch_config: x_max=%d, y_max=%d, rst_gpio_num=%d, int_gpio_num=%d, levels.reset=%d, levels.interrupt=%d, flags.swap_xy=%d, flags.mirror_x=%d, flags.mirror_y=%d",
             touch_cfg->touch_config.x_max,
             touch_cfg->touch_config.y_max,
             touch_cfg->touch_config.rst_gpio_num,
             touch_cfg->touch_config.int_gpio_num,
             touch_cfg->touch_config.levels.reset,
             touch_cfg->touch_config.levels.interrupt,
             touch_cfg->touch_config.flags.swap_xy,
             touch_cfg->touch_config.flags.mirror_x,
             touch_cfg->touch_config.flags.mirror_y);

    ret = lcd_touch_factory_entry_t(touch_handles->io_handle, &touch_cfg->touch_config, &touch_handles->touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create CST816S touch driver: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(touch_handles->io_handle);
        free(touch_handles);
        return -1;
    }

    ESP_LOGI(TAG, "Successfully initialized: %s, touch: %p, io: %p", touch_cfg->name, touch_handles->touch_handle, touch_handles->io_handle);
    *device_handle = touch_handles;
    return 0;
}

int dev_lcd_touch_i2c_deinit(void *device_handle)
{
    dev_lcd_touch_i2c_handles_t *touch_handles = (dev_lcd_touch_i2c_handles_t *)device_handle;

    if (touch_handles->touch_handle) {
        esp_lcd_touch_del(touch_handles->touch_handle);
        touch_handles->touch_handle = NULL;
    }

    if (touch_handles->io_handle) {
        esp_lcd_panel_io_del(touch_handles->io_handle);
        touch_handles->io_handle = NULL;
    }

    dev_lcd_touch_i2c_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    if (cfg) {
        esp_board_periph_unref_handle(cfg->i2c_name);
    }

    free(touch_handles);
    return 0;
}

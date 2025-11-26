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
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_dev.h"

#include "dev_display_lcd_spi.h"
#include "esp_board_periph.h"
#include "periph_spi.h"
#include "esp_board_device.h"

static const char *TAG = "DEV_DISPLAY_LCD_SPI";

extern esp_err_t lcd_panel_factory_entry_t(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel);

int dev_display_lcd_spi_init(void *cfg, int cfg_size, void **device_handle)
{
    dev_display_lcd_spi_config_t *lcd_cfg = (dev_display_lcd_spi_config_t *)cfg;

    dev_display_lcd_spi_handles_t *lcd_handles = (dev_display_lcd_spi_handles_t *)calloc(1, sizeof(dev_display_lcd_spi_handles_t));
    if (lcd_handles == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LCD handles");
        return -1;
    }

    ESP_LOGI(TAG, "Initializing LCD display: %s, chip: %s", lcd_cfg->name, lcd_cfg->chip);
    periph_spi_handle_t *spi_handle = NULL;
    if (lcd_cfg->spi_name && strlen(lcd_cfg->spi_name) > 0) {
        int ret = esp_board_periph_ref_handle(lcd_cfg->spi_name, (void **)&spi_handle);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to get SPI peripheral handle: %d", ret);
            free(lcd_handles);
            return -1;
        }
    } else {
        ESP_LOGE(TAG, "No SPI name configured for LCD display: %s", lcd_cfg->name);
        free(lcd_handles);
        return -1;
    }
    ESP_LOGD(TAG, "SPI PORT:%d, cs_gpio=%d, dc_gpio=%d, spi_mode=%d, pclk_hz=%d, trans_queue_depth=%d, lcd_cmd_bits=%d, lcd_param_bits=%d, cs_ena_pretrans=%d, cs_ena_posttrans=%d, flags: dc_high_on_cmd=%d, dc_low_on_data=%d, dc_low_on_param=%d, octal_mode=%d, quad_mode=%d, sio_mode=%d, lsb_first=%d, cs_high_active=%d",
             spi_handle->spi_port,
             lcd_cfg->io_spi_config.cs_gpio_num,
             lcd_cfg->io_spi_config.dc_gpio_num,
             lcd_cfg->io_spi_config.spi_mode,
             lcd_cfg->io_spi_config.pclk_hz,
             lcd_cfg->io_spi_config.trans_queue_depth,
             lcd_cfg->io_spi_config.lcd_cmd_bits,
             lcd_cfg->io_spi_config.lcd_param_bits,
             lcd_cfg->io_spi_config.cs_ena_pretrans,
             lcd_cfg->io_spi_config.cs_ena_posttrans,
             lcd_cfg->io_spi_config.flags.dc_high_on_cmd,
             lcd_cfg->io_spi_config.flags.dc_low_on_data,
             lcd_cfg->io_spi_config.flags.dc_low_on_param,
             lcd_cfg->io_spi_config.flags.octal_mode,
             lcd_cfg->io_spi_config.flags.quad_mode,
             lcd_cfg->io_spi_config.flags.sio_mode,
             lcd_cfg->io_spi_config.flags.lsb_first,
             lcd_cfg->io_spi_config.flags.cs_high_active);
    // Create LCD panel IO using the configured IO SPI config
    esp_err_t ret = esp_lcd_new_panel_io_spi(spi_handle->spi_port, &lcd_cfg->io_spi_config, &lcd_handles->io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LCD panel IO: %s", esp_err_to_name(ret));
        free(lcd_handles);
        return -1;
    }

    ret = lcd_panel_factory_entry_t(lcd_handles->io_handle, &lcd_cfg->panel_config, &lcd_handles->panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LCD panel: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(lcd_handles->io_handle);
        free(lcd_handles);
        return -1;
    }
    // Initialize LCD panel
    ret = esp_lcd_panel_reset(lcd_handles->panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset LCD panel: %s", esp_err_to_name(ret));
        esp_lcd_panel_del(lcd_handles->panel_handle);
        esp_lcd_panel_io_del(lcd_handles->io_handle);
        free(lcd_handles);
        return -1;
    }
    ret = esp_lcd_panel_init(lcd_handles->panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LCD panel: %s", esp_err_to_name(ret));
        esp_lcd_panel_del(lcd_handles->panel_handle);
        esp_lcd_panel_io_del(lcd_handles->io_handle);
        free(lcd_handles);
        return -1;
    }

    // Invert color if needed
    if (lcd_cfg->invert_color) {
        ret = esp_lcd_panel_invert_color(lcd_handles->panel_handle, true);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to invert color on LCD panel: %s", esp_err_to_name(ret));
        }
    }

    // Turn on display
    esp_lcd_panel_disp_on_off(lcd_handles->panel_handle, true);

    ESP_LOGI(TAG, "Successfully initialized: %s, p:%p, panel: %p, io: %p", lcd_cfg->name, lcd_handles, lcd_handles->panel_handle, lcd_handles->io_handle);
    *device_handle = lcd_handles;
    return 0;
}

int dev_display_lcd_spi_deinit(void *device_handle)
{
    dev_display_lcd_spi_handles_t *lcd_handles = (dev_display_lcd_spi_handles_t *)device_handle;

    if (lcd_handles->panel_handle) {
        esp_lcd_panel_del(lcd_handles->panel_handle);
        lcd_handles->panel_handle = NULL;
    }

    if (lcd_handles->io_handle) {
        esp_lcd_panel_io_del(lcd_handles->io_handle);
        lcd_handles->io_handle = NULL;
    }

    const char *name = NULL;
    const esp_board_device_handle_t *device_handle_struct = esp_board_device_find_by_handle(device_handle);
    if (device_handle_struct) {
        name = device_handle_struct->name;
    }
    dev_display_lcd_spi_config_t *cfg = NULL;
    esp_board_device_get_config(name, (void **)&cfg);
    if (cfg) {
        esp_board_periph_unref_handle(cfg->spi_name);
    }
    free(lcd_handles);
    return 0;
}

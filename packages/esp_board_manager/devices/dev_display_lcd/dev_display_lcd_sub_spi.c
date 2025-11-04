/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_board_periph.h"
#include "esp_board_device.h"
#include "esp_board_entry.h"
#include "periph_spi.h"
#include "dev_display_lcd.h"

static const char *TAG = "DEV_DISPLAY_LCD_SUB_SPI";

extern esp_err_t lcd_panel_factory_entry_t(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel);

int dev_display_lcd_sub_spi_init(void *cfg, int cfg_size, void **device_handle)
{
    dev_display_lcd_config_t *lcd_cfg = (dev_display_lcd_config_t *)cfg;
    dev_display_lcd_handles_t *lcd_handles = (dev_display_lcd_handles_t *)*device_handle;

    ESP_LOGI(TAG, "Initializing SPI LCD display: %s, chip: %s", lcd_cfg->name, lcd_cfg->chip);
    periph_spi_handle_t *spi_handle = NULL;
    if (lcd_cfg->sub_cfg.spi.spi_name && strlen(lcd_cfg->sub_cfg.spi.spi_name) > 0) {
        int ret = esp_board_periph_ref_handle(lcd_cfg->sub_cfg.spi.spi_name, (void **)&spi_handle);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to get SPI peripheral handle: %d", ret);
            return -1;
        }
    } else {
        ESP_LOGE(TAG, "No SPI name configured for LCD display: %s", lcd_cfg->name);
        return -1;
    }

    ESP_LOGD(TAG, "SPI PORT:%d, cs_gpio=%d, dc_gpio=%d, spi_mode=%d, pclk_hz=%d, trans_queue_depth=%d, lcd_cmd_bits=%d, lcd_param_bits=%d, cs_ena_pretrans=%d, cs_ena_posttrans=%d, flags: dc_high_on_cmd=%d, dc_low_on_data=%d, dc_low_on_param=%d, octal_mode=%d, quad_mode=%d, sio_mode=%d, lsb_first=%d, cs_high_active=%d",
             spi_handle->spi_port,
             lcd_cfg->sub_cfg.spi.io_spi_config.cs_gpio_num,
             lcd_cfg->sub_cfg.spi.io_spi_config.dc_gpio_num,
             lcd_cfg->sub_cfg.spi.io_spi_config.spi_mode,
             lcd_cfg->sub_cfg.spi.io_spi_config.pclk_hz,
             lcd_cfg->sub_cfg.spi.io_spi_config.trans_queue_depth,
             lcd_cfg->sub_cfg.spi.io_spi_config.lcd_cmd_bits,
             lcd_cfg->sub_cfg.spi.io_spi_config.lcd_param_bits,
             lcd_cfg->sub_cfg.spi.io_spi_config.cs_ena_pretrans,
             lcd_cfg->sub_cfg.spi.io_spi_config.cs_ena_posttrans,
             lcd_cfg->sub_cfg.spi.io_spi_config.flags.dc_high_on_cmd,
             lcd_cfg->sub_cfg.spi.io_spi_config.flags.dc_low_on_data,
             lcd_cfg->sub_cfg.spi.io_spi_config.flags.dc_low_on_param,
             lcd_cfg->sub_cfg.spi.io_spi_config.flags.octal_mode,
             lcd_cfg->sub_cfg.spi.io_spi_config.flags.quad_mode,
             lcd_cfg->sub_cfg.spi.io_spi_config.flags.sio_mode,
             lcd_cfg->sub_cfg.spi.io_spi_config.flags.lsb_first,
             lcd_cfg->sub_cfg.spi.io_spi_config.flags.cs_high_active);

    // Create LCD panel IO using the configured IO SPI config
    esp_err_t ret = esp_lcd_new_panel_io_spi(spi_handle->spi_port, &lcd_cfg->sub_cfg.spi.io_spi_config, &lcd_handles->io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LCD panel IO: %s", esp_err_to_name(ret));
        esp_board_periph_unref_handle(lcd_cfg->sub_cfg.spi.spi_name);
        return -1;
    }

    ret = lcd_panel_factory_entry_t(lcd_handles->io_handle, &lcd_cfg->sub_cfg.spi.panel_config, &lcd_handles->panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LCD panel: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(lcd_handles->io_handle);
        esp_board_periph_unref_handle(lcd_cfg->sub_cfg.spi.spi_name);
        return -1;
    }

    return 0;
}

int dev_display_lcd_sub_spi_deinit(void *device_handle)
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

    esp_board_periph_unref_handle(cfg->sub_cfg.spi.spi_name);
    return 0;
}

ESP_BOARD_ENTRY_IMPLEMENT(spi, dev_display_lcd_sub_spi_init, dev_display_lcd_sub_spi_deinit);

/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"
#include "esp_check.h"
#include "esp_board_periph.h"
#include "driver/i2c_master.h"
#include "dev_gpio_expander.h"
#include "esp_io_expander.h"

static const char *TAG = "DEV_IO_EXPANDER";

extern esp_err_t io_expander_factory_entry_t(i2c_master_bus_handle_t i2c_bus, const uint16_t dev_addr, esp_io_expander_handle_t *handle_ret);

int dev_gpio_expander_init(void *cfg, int cfg_size, void **device_handle)
{
    if (!cfg || !device_handle) {
        ESP_LOGE(TAG, "Invalid parameters, cfg: %p, device_handle: %p", cfg, device_handle);
        return -1;
    }

    const dev_io_expander_config_t *config = (const dev_io_expander_config_t *)cfg;
    void *i2c_handle = NULL;
    esp_err_t ret = esp_board_periph_get_handle(config->i2c_name, &i2c_handle);
    if (ret != ESP_OK || !i2c_handle) {
        ESP_LOGE(TAG, "Failed to get I2C (%s) handle, ret:%d, i2c_handle:%p\n", config->i2c_name, ret, i2c_handle);
        return -1;
    }

    uint16_t dev_addr = 0xFF;
    for (size_t i = 0; i < config->i2c_addr_count; i++) {
        if (i2c_master_probe((i2c_master_bus_handle_t)i2c_handle, config->i2c_addr[i] >> 1, 100) == ESP_OK) {
            ESP_LOGI(TAG, "IO Expander found at address 0x%02X", config->i2c_addr[i]);
            dev_addr = config->i2c_addr[i] >> 1;
            break;
        }
    }
    if (dev_addr == 0xFF) {
        ESP_LOGE(TAG, "No IO Expander found on the I2C bus");
        return -1;
    }

    esp_io_expander_handle_t *dev = (esp_io_expander_handle_t *)calloc(1, sizeof(esp_io_expander_handle_t));
    if (!dev) {
        ESP_LOGE(TAG, "Failed to allocate memory for io_expander device");
        return -1;
    }

    ret = io_expander_factory_entry_t(i2c_handle, dev_addr, dev);
    if (ret != ESP_OK || !dev) {
        ESP_LOGE(TAG, "Failed to create IO expander handle\n");
        free(dev);
        return -1;
    }

    for (uint32_t i = 0; i < config->max_pins; i++) {
        uint32_t pin_mask = (1 << i);
        if (config->output_io_mask & pin_mask) {
            ESP_GOTO_ON_ERROR(esp_io_expander_set_dir(*dev, pin_mask, IO_EXPANDER_OUTPUT),
                              io_expander_del, TAG, "Set IO expander pin %d as output failed", i);
            uint8_t level = (config->output_io_level_mask >> i) & 1;
            ESP_GOTO_ON_ERROR(esp_io_expander_set_level(*dev, pin_mask, level),
                              io_expander_del, TAG, "Set IO expander pin %d default level failed", i);
            ESP_LOGI(TAG, "Set IO expander pin %d as output, level: %d", i, level);
        } else if (config->input_io_mask & pin_mask) {
            ESP_GOTO_ON_ERROR(esp_io_expander_set_dir(*dev, pin_mask, IO_EXPANDER_INPUT),
                              io_expander_del, TAG, "Set IO expander pin %d as input failed", i);
            ESP_LOGI(TAG, "Set IO expander pin %d as input", i);
        }
    }

    *device_handle = dev;
    return 0;

io_expander_del:
    free(dev);
    return -1;
}

int dev_gpio_expander_deinit(void *device_handle)
{
    esp_io_expander_handle_t *io_expander_dev = (esp_io_expander_handle_t *)device_handle;
    esp_io_expander_del(*io_expander_dev);
    free(io_expander_dev);
    return 0;
}

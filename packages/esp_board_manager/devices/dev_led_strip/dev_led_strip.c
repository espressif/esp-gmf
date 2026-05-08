/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <inttypes.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_board_entry.h"
#include "esp_board_device.h"
#include "dev_led_strip.h"

static const char *TAG = "DEV_LED_STRIP";

static const char *dev_led_strip_resolve_entry_name(const char *sub_type)
{
    if (sub_type && strcmp(sub_type, ESP_BOARD_DEVICE_LED_STRIP_SUB_TYPE_RMT) == 0) {
        return "led_strip_sub_rmt";
    }
    if (sub_type && strcmp(sub_type, ESP_BOARD_DEVICE_LED_STRIP_SUB_TYPE_SPI) == 0) {
        return "led_strip_sub_spi";
    }
    return sub_type;
}

int dev_led_strip_init(void *cfg, int cfg_size, void **device_handle)
{
    if (cfg == NULL || device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    if (cfg_size != sizeof(dev_led_strip_config_t)) {
        ESP_LOGE(TAG, "Invalid config size");
        return -1;
    }

    const dev_led_strip_config_t *config = (const dev_led_strip_config_t *)cfg;
    if (config->sub_type == NULL) {
        ESP_LOGE(TAG, "Invalid LED strip sub_type");
        return -1;
    }

    ESP_LOGI(TAG, "Initializing LED strip: %s, chip: %s, sub_type: %s, gpio: %d, max_leds: %" PRIu32,
             config->name,
             config->chip ? config->chip : "(unknown)",
             config->sub_type, config->strip_config.strip_gpio_num,
             config->strip_config.max_leds);

    const esp_board_entry_desc_t *entry_desc = esp_board_entry_find_desc(dev_led_strip_resolve_entry_name(config->sub_type));
    if (entry_desc == NULL || entry_desc->init_func == NULL) {
        ESP_LOGE(TAG, "Failed to find LED strip sub device: %s", config->sub_type);
        return -1;
    }

    int ret = entry_desc->init_func(cfg, cfg_size, device_handle);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to initialize LED strip sub device: %s", config->sub_type);
        return ret;
    }

    ESP_LOGI(TAG, "Successfully initialized LED strip: %s, sub_type: %s",
             config->name, config->sub_type);
    return 0;
}

int dev_led_strip_deinit(void *device_handle)
{
    if (device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    dev_led_strip_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    if (cfg == NULL) {
        ESP_LOGE(TAG, "Failed to get LED strip config by handle");
        return -1;
    }
    if (cfg->sub_type == NULL) {
        ESP_LOGE(TAG, "Invalid LED strip sub_type");
        return -1;
    }

    const esp_board_entry_desc_t *desc = esp_board_entry_find_desc(dev_led_strip_resolve_entry_name(cfg->sub_type));
    if (desc == NULL || desc->deinit_func == NULL) {
        ESP_LOGE(TAG, "No deinit function found for LED strip sub type: %s", cfg->sub_type);
        return -1;
    }

    int ret = desc->deinit_func(device_handle);
    if (ret != 0) {
        ESP_LOGE(TAG, "LED strip sub type %s deinit failed: %d", cfg->sub_type, ret);
        return ret;
    }

    ESP_LOGI(TAG, "Successfully deinitialized LED strip: %s, sub_type: %s",
             cfg->name, cfg->sub_type);
    return 0;
}

/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_board_entry.h"
#include "dev_led_strip.h"

static const char *TAG = "DEV_LED_STRIP_RMT";

int dev_led_strip_sub_rmt_init(void *cfg, int cfg_size, void **device_handle)
{
    if (cfg == NULL || device_handle == NULL || cfg_size != sizeof(dev_led_strip_config_t)) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    const dev_led_strip_config_t *config = (const dev_led_strip_config_t *)cfg;
    dev_led_strip_handles_t *handles = calloc(1, sizeof(dev_led_strip_handles_t));
    if (handles == NULL) {
        ESP_LOGE(TAG, "Failed to allocate RMT LED strip handle");
        return -1;
    }

    esp_err_t ret = led_strip_new_rmt_device(&config->strip_config,
                                             &config->sub_cfg.rmt.rmt_config,
                                             &handles->strip_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT LED strip: %s", esp_err_to_name(ret));
        free(handles);
        return -1;
    }

    ret = led_strip_clear(handles->strip_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear RMT LED strip: %s", esp_err_to_name(ret));
        led_strip_del(handles->strip_handle);
        handles->strip_handle = NULL;
        free(handles);
        return -1;
    }

    *device_handle = handles;
    return 0;
}

int dev_led_strip_sub_rmt_deinit(void *device_handle)
{
    if (device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    dev_led_strip_handles_t *handles = (dev_led_strip_handles_t *)device_handle;
    if (handles->strip_handle) {
        esp_err_t ret = led_strip_del(handles->strip_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to delete RMT LED strip: %s", esp_err_to_name(ret));
            return -1;
        }
        handles->strip_handle = NULL;
    }

    free(handles);
    return 0;
}

ESP_BOARD_ENTRY_IMPLEMENT(led_strip_sub_rmt, dev_led_strip_sub_rmt_init, dev_led_strip_sub_rmt_deinit);

/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

 #include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "driver/ledc.h"
#include "dev_ledc_ctrl.h"
#include "esp_board_periph.h"
#include "esp_board_device.h"

static const char *TAG = "DEV_LEDC_CTRL";

int dev_ledc_ctrl_init(void *cfg, int cfg_size, void **device_handle)
{
    if (!cfg || !device_handle) {
        ESP_LOGE(TAG, "Invalid parameters, cfg: %p, device_handle: %p", cfg, device_handle);
        return -1;
    }
    const dev_ledc_ctrl_config_t *config = (const dev_ledc_ctrl_config_t *)cfg;
    periph_ledc_handle_t *ledc_handle = NULL;
    if (!config->ledc_name || strlen(config->ledc_name) == 0) {
        ESP_LOGE(TAG, "No LEDC peripheral name configured for device: %s", config->name);
        return -1;
    }
    int ret = esp_board_periph_ref_handle(config->ledc_name, (void*)&ledc_handle);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to get LEDC peripheral handle '%s': %d", config->ledc_name, ret);
        return -1;
    }
    periph_ledc_config_t *ledc_config = NULL;
    esp_err_t config_ret = esp_board_periph_get_config(config->ledc_name, (void**)&ledc_config);
    if (config_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LEDC peripheral config '%s': %s", config->ledc_name, esp_err_to_name(config_ret));
        return -1;
    }
    uint32_t duty = (config->default_percent * ((1 << (uint32_t)ledc_config->duty_resolution) - 1)) / 100;

    // Set initial duty cycle using the peripheral handle
    esp_err_t err = ledc_set_duty(ledc_handle->speed_mode, ledc_handle->channel, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_set_duty failed: %s", esp_err_to_name(err));
        return err;
    }
    // Update the duty cycle
    err = ledc_update_duty(ledc_handle->speed_mode, ledc_handle->channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_update_duty failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "LEDC control device %s initialized: channel=%d, default_percent=%" PRIu32 "%%, duty=%" PRIu32,
             config->name, ledc_handle->channel, config->default_percent, duty);

    // Store the LEDC handle for later use
    *device_handle = ledc_handle;
    return 0;
}

int dev_ledc_ctrl_deinit(void *device_handle)
{
    if (!device_handle) {
        ESP_LOGE(TAG, "Invalid handle");
        return -1;
    }
    // Cast back to LEDC handle type
    periph_ledc_handle_t *ledc_handle = (periph_ledc_handle_t *)device_handle;
    if (!ledc_handle) {
        ESP_LOGE(TAG, "Invalid LEDC handle");
        return -1;
    }
    // Stop the LEDC channel
    esp_err_t err = ledc_stop(ledc_handle->speed_mode, ledc_handle->channel, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_stop failed: %s", esp_err_to_name(err));
        return err;
    }
    const char *name = NULL;
    const esp_board_device_handle_t *device_handle_struct = esp_board_device_find_by_handle(device_handle);
    if (device_handle_struct) {
        name = device_handle_struct->name;
    }
    dev_ledc_ctrl_config_t *cfg = NULL;
    esp_board_device_get_config(name, (void **)&cfg);
    if (cfg) {
        esp_board_periph_unref_handle(cfg->ledc_name);
    }

    ESP_LOGI(TAG, "LEDC control device channel %d deinitialized", ledc_handle->channel);
    return 0;
}

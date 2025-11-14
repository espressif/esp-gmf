/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_board_periph.h"
#include "esp_board_device.h"
#include "esp_board_manager_defs.h"
#include "esp_board_entry.h"
#include "dev_button.h"
#include "periph_adc.h"
#include "button_adc.h"

static const char *TAG = "DEV_BUTTON_SUB_ADC";

static int adc_button_single_init(const dev_button_config_t *config,
                                  periph_adc_handle_t *adc_periph_handle,
                                  periph_adc_config_t *adc_cfg,
                                  dev_button_handles_t *button_handle)
{
    ESP_LOGI(TAG, "Initializing single ADC button on unit %d, channel %d, button index: %d",
             adc_cfg->cfg.oneshot.unit_cfg.unit_id,
             adc_cfg->cfg.oneshot.channel_id,
             config->sub_cfg.adc.single.button_index);

    // Prepare ADC button configuration
    button_adc_config_t adc_button_cfg = {
        .adc_handle = &adc_periph_handle->oneshot,
        .unit_id = adc_cfg->cfg.oneshot.unit_cfg.unit_id,
        .adc_channel = adc_cfg->cfg.oneshot.channel_id,
        .button_index = config->sub_cfg.adc.single.button_index,
        .min = config->sub_cfg.adc.single.min_voltage,
        .max = config->sub_cfg.adc.single.max_voltage,
    };

    esp_err_t ret = iot_button_new_adc_device(&config->button_timing_cfg, &adc_button_cfg, &button_handle->button_handles[0]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC button: %s", esp_err_to_name(ret));
        return -1;
    }

    // Store handle into pre-allocated structure
    button_handle->num_buttons = 1;
    return 0;
}

static int adc_button_multiple_init(const dev_button_config_t *config,
                                    periph_adc_handle_t *adc_periph_handle,
                                    periph_adc_config_t *adc_cfg,
                                    dev_button_handles_t *button_handle)
{
    uint8_t button_num = config->sub_cfg.adc.button_num;
    const uint16_t *voltage_range = config->sub_cfg.adc.multi.voltage_range;
    if (voltage_range == NULL) {
        ESP_LOGE(TAG, "Invalid multi-button voltage_range configuration");
        return -1;
    }
    if (button_num > CONFIG_ADC_BUTTON_MAX_BUTTON_PER_CHANNEL) {
        ESP_LOGE(TAG, "Too many buttons configured for one ADC channel (max %d)", CONFIG_ADC_BUTTON_MAX_BUTTON_PER_CHANNEL);
        return -1;
    }
    ESP_LOGI(TAG, "Initializing %d ADC buttons on unit %d, channel %d",
             button_num, adc_cfg->cfg.oneshot.unit_cfg.unit_id,
             adc_cfg->cfg.oneshot.channel_id);

    button_handle->num_buttons = button_num;
    uint16_t max_voltage = config->sub_cfg.adc.multi.max_voltage;
    for (size_t i = 0; i < button_num; i++) {
        button_adc_config_t adc_button_cfg = {
            .adc_handle = &adc_periph_handle->oneshot,
            .unit_id = adc_cfg->cfg.oneshot.unit_cfg.unit_id,
            .adc_channel = adc_cfg->cfg.oneshot.channel_id,
            .button_index = i,
        };

        // Calculate min/max based on voltage_range
        if (i == 0) {
            adc_button_cfg.min = (0 + voltage_range[i]) / 2;
        } else {
            adc_button_cfg.min = (voltage_range[i - 1] + voltage_range[i]) / 2;
        }
        if (i == button_num - 1) {
            adc_button_cfg.max = (voltage_range[i] + max_voltage) / 2;
        } else {
            adc_button_cfg.max = (voltage_range[i] + voltage_range[i + 1]) / 2;
        }

        esp_err_t ret = iot_button_new_adc_device(&config->button_timing_cfg, &adc_button_cfg, &button_handle->button_handles[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create ADC button %d: %s", i, esp_err_to_name(ret));
            // Clean up previously created buttons
            for (size_t j = 0; j < i; j++) {
                iot_button_delete(button_handle->button_handles[j]);
                button_handle->button_handles[j] = NULL;
            }
            return -1;
        }
    }
    return 0;
}

int dev_button_sub_adc_init(void *cfg, int cfg_size, void **device_handle)
{
    // Get ADC handle from board manager periph
    const dev_button_config_t *config = (const dev_button_config_t *)cfg;
    periph_adc_handle_t *adc_periph_handle = NULL;
    esp_err_t ret = esp_board_periph_ref_handle(config->sub_cfg.adc.adc_name, (void **)&adc_periph_handle);
    if (ret != ESP_OK || adc_periph_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get ADC handle for %s: %s",
                 config->sub_cfg.adc.adc_name, esp_err_to_name(ret));
        return -1;
    }

    // Get ADC configuration to get unit_id and channel_id
    periph_adc_config_t *adc_cfg = NULL;
    ret = esp_board_periph_get_config(config->sub_cfg.adc.adc_name, (void **)&adc_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get ADC config for %s: %s", config->sub_cfg.adc.adc_name, esp_err_to_name(ret));
        goto cleanup;
    }

    // Check if ADC is in oneshot mode
    if (adc_cfg->role != ESP_BOARD_PERIPH_ROLE_ONESHOT) {
        ESP_LOGE(TAG, "Unsupported ADC role for ADC button: %d", adc_cfg->role);
        goto cleanup;
    }

    // Create adc button(s)
    dev_button_handles_t *button_handles = calloc(1, sizeof(dev_button_handles_t));
    if (button_handles == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for dev_button_sub_gpio");
        goto cleanup;
    }
    if (config->sub_cfg.adc.button_num > 1) {
        ret = adc_button_multiple_init(config, adc_periph_handle, adc_cfg, button_handles);
    } else {
        ret = adc_button_single_init(config, adc_periph_handle, adc_cfg, button_handles);
    }
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to initialize ADC button(s) : %s", esp_err_to_name(ret));
        free(button_handles);
        goto cleanup;
    }

    *device_handle = button_handles;
    return 0;

cleanup:
    esp_board_periph_unref_handle(config->sub_cfg.adc.adc_name);
    return -1;
}

int dev_button_sub_adc_deinit(void *device_handle)
{
    if (device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    // Deinitialize all buttons
    dev_button_handles_t *button_handles = (dev_button_handles_t *)device_handle;
    for (size_t i = 0; i < button_handles->num_buttons; i++) {
        if (button_handles->button_handles[i]) {
            iot_button_delete(button_handles->button_handles[i]);
            button_handles->button_handles[i] = NULL;
        }
    }

    // Unreference ADC peripheral handle
    dev_button_config_t *cfg = NULL;
    esp_err_t ret = esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get button config: %s", esp_err_to_name(ret));
    }
    if (cfg && cfg->sub_cfg.adc.adc_name) {
        esp_board_periph_unref_handle(cfg->sub_cfg.adc.adc_name);
    }
    free(device_handle);
    ESP_LOGI(TAG, "Successfully deinitialized ADC button(s)");
    return ret == ESP_OK ? 0 : -1;
}

ESP_BOARD_ENTRY_IMPLEMENT(adc, dev_button_sub_adc_init, dev_button_sub_adc_deinit);

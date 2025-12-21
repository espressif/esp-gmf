/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_log.h"
#include "esp_board_device.h"
#include "esp_board_entry.h"
#include "dev_button.h"

static const char *TAG = "DEV_BUTTON";

int dev_button_init(void *cfg, int cfg_size, void **device_handle)
{
    if (cfg == NULL || device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    if (cfg_size != sizeof(dev_button_config_t)) {
        ESP_LOGE(TAG, "Invalid config size");
        return -1;
    }
    esp_err_t ret = ESP_FAIL;
    dev_button_handles_t *handle = NULL;
    const dev_button_config_t *config = (const dev_button_config_t *)cfg;
    const esp_board_entry_desc_t *entry_desc = esp_board_entry_find_desc(config->sub_type);
    if (entry_desc == NULL) {
        ESP_LOGE(TAG, "Failed to find sub device: %s", config->sub_type);
        return -1;
    }
    ret = entry_desc->init_func((void *)config, cfg_size, (void **)&handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize sub device: %s", config->sub_type);
        return -1;
    }

    ESP_LOGI(TAG, "Successfully initialized button: %s, sub_type: %s",
             config->name, config->sub_type);

    *device_handle = handle;
    return 0;
}

int dev_button_deinit(void *device_handle)
{
    if (device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    dev_button_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    if (cfg) {
        const esp_board_entry_desc_t *desc = esp_board_entry_find_desc(cfg->sub_type);
        if (desc && desc->deinit_func) {
            int ret = desc->deinit_func(device_handle);
            if (ret != 0) {
                ESP_LOGE(TAG, "Sub device '%s' deinit failed with error: %d", cfg->sub_type, ret);
            } else {
                ESP_LOGI(TAG, "Sub device '%s' deinitialized successfully", cfg->sub_type);
            }
        } else {
            ESP_LOGW(TAG, "No custom deinit function found for sub type '%s'", cfg->sub_type);
        }
    }
    return 0;
}

static int dev_button_register_callback(void *handle, const void *cfg, int cfg_size, void *event_cb, void *user_data)
{
    if (handle == NULL || cfg == NULL || event_cb == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    if (cfg_size != sizeof(dev_button_config_t)) {
        ESP_LOGE(TAG, "Invalid config size");
        return -1;
    }

    dev_button_handles_t *button_handle = (dev_button_handles_t *)handle;
    const dev_button_config_t *config = (const dev_button_config_t *)cfg;
    button_cb_t button_event_cb = (button_cb_t)event_cb;
    esp_err_t ret = ESP_FAIL;
    // Check if user_data is NULL and use button name as default value.
    bool use_default_user_data = false;
    if (user_data == NULL) {
        use_default_user_data = true;
        ESP_LOGI(TAG, "User data is NULL, use button name as default value");
    }
    // Register enabled events based on bitfield configuration for each button.
    for (int i = 0; i < button_handle->num_buttons; i++) {
        if (use_default_user_data) {
            if (button_handle->num_buttons > 1) {
                user_data = (void *)config->sub_cfg.adc.multi.button_labels[i];
            } else {
                user_data = (void *)config->name;
            }
        }
        if (config->events_cfg.enabled_events.press_down) {
            ret = iot_button_register_cb(button_handle->button_handles[i], BUTTON_PRESS_DOWN, NULL, button_event_cb, user_data);
            ESP_BOARD_RETURN_ON_FALSE(ret == ESP_OK, -1, TAG, "Failed to register press_down callback: %s", esp_err_to_name(ret));
        }

        if (config->events_cfg.enabled_events.press_up) {
            ret = iot_button_register_cb(button_handle->button_handles[i], BUTTON_PRESS_UP, NULL, button_event_cb, user_data);
            ESP_BOARD_RETURN_ON_FALSE(ret == ESP_OK, -1, TAG, "Failed to register press_up callback: %s", esp_err_to_name(ret));
        }

        if (config->events_cfg.enabled_events.single_click) {
            ret = iot_button_register_cb(button_handle->button_handles[i], BUTTON_SINGLE_CLICK, NULL, button_event_cb, user_data);
            ESP_BOARD_RETURN_ON_FALSE(ret == ESP_OK, -1, TAG, "Failed to register single_click callback: %s", esp_err_to_name(ret));
        }

        if (config->events_cfg.enabled_events.double_click) {
            ret = iot_button_register_cb(button_handle->button_handles[i], BUTTON_DOUBLE_CLICK, NULL, button_event_cb, user_data);
            ESP_BOARD_RETURN_ON_FALSE(ret == ESP_OK, -1, TAG, "Failed to register double_click callback: %s", esp_err_to_name(ret));
        }

        if (config->events_cfg.enabled_events.long_press_hold) {
            ret = iot_button_register_cb(button_handle->button_handles[i], BUTTON_LONG_PRESS_HOLD, NULL, button_event_cb, user_data);
            ESP_BOARD_RETURN_ON_FALSE(ret == ESP_OK, -1, TAG, "Failed to register long_press_hold callback: %s", esp_err_to_name(ret));
        }

        if (config->events_cfg.enabled_events.press_repeat) {
            ret = iot_button_register_cb(button_handle->button_handles[i], BUTTON_PRESS_REPEAT, NULL, button_event_cb, user_data);
            ESP_BOARD_RETURN_ON_FALSE(ret == ESP_OK, -1, TAG, "Failed to register press_repeat callback: %s", esp_err_to_name(ret));
        }

        if (config->events_cfg.enabled_events.press_repeat_done) {
            ret = iot_button_register_cb(button_handle->button_handles[i], BUTTON_PRESS_REPEAT_DONE, NULL, button_event_cb, user_data);
            ESP_BOARD_RETURN_ON_FALSE(ret == ESP_OK, -1, TAG, "Failed to register press_repeat_done callback: %s", esp_err_to_name(ret));
        }

        if (config->events_cfg.enabled_events.press_end) {
            ret = iot_button_register_cb(button_handle->button_handles[i], BUTTON_PRESS_END, NULL, button_event_cb, user_data);
            ESP_BOARD_RETURN_ON_FALSE(ret == ESP_OK, -1, TAG, "Failed to register press_end callback: %s", esp_err_to_name(ret));
        }

        if (config->events_cfg.enabled_events.long_press_start) {
            ret = iot_button_register_cb(button_handle->button_handles[i], BUTTON_LONG_PRESS_START, NULL, button_event_cb, user_data);
            ESP_BOARD_RETURN_ON_FALSE(ret == ESP_OK, -1, TAG, "Failed to register long_press_start callback: %s", esp_err_to_name(ret));
        }

        if (config->events_cfg.enabled_events.long_press_up) {
            ret = iot_button_register_cb(button_handle->button_handles[i], BUTTON_LONG_PRESS_UP, NULL, button_event_cb, user_data);
            ESP_BOARD_RETURN_ON_FALSE(ret == ESP_OK, -1, TAG, "Failed to register long_press_up callback: %s", esp_err_to_name(ret));
        }

        if (config->events_cfg.enabled_events.long_press_start && config->events_cfg.long_press_start.count > 0 && config->events_cfg.long_press_start.durations_ms != NULL) {
            for (int j = 0; j < config->events_cfg.long_press_start.count; ++j) {
                button_event_args_t long_press_temp_args = {
                    .long_press.press_time = config->events_cfg.long_press_start.durations_ms[j],
                };
                ret = iot_button_register_cb(button_handle->button_handles[i], BUTTON_LONG_PRESS_START, &long_press_temp_args, button_event_cb, user_data);
                ESP_BOARD_RETURN_ON_FALSE(ret == ESP_OK, -1, TAG, "Failed to register long_press_start (durations_ms: %d) callback: %s", config->events_cfg.long_press_start.durations_ms[j], esp_err_to_name(ret));
            }
        }

        if (config->events_cfg.enabled_events.long_press_up && config->events_cfg.long_press_up.count > 0 && config->events_cfg.long_press_up.durations_ms != NULL) {
            for (int j = 0; j < config->events_cfg.long_press_up.count; ++j) {
                button_event_args_t long_press_temp_args = {
                    .long_press.press_time = config->events_cfg.long_press_up.durations_ms[j],
                };
                ret = iot_button_register_cb(button_handle->button_handles[i], BUTTON_LONG_PRESS_UP, &long_press_temp_args, button_event_cb, user_data);
                ESP_BOARD_RETURN_ON_FALSE(ret == ESP_OK, -1, TAG, "Failed to register long_press_up (durations_ms: %d) callback: %s", config->events_cfg.long_press_up.durations_ms[j], esp_err_to_name(ret));
            }
        }

        if (config->events_cfg.enabled_events.multi_click && config->events_cfg.multi_click.count > 0 && config->events_cfg.multi_click.click_counts != NULL) {
            for (int j = 0; j < config->events_cfg.multi_click.count; ++j) {
                button_event_args_t multiple_click_temp_args = {
                    .multiple_clicks.clicks = config->events_cfg.multi_click.click_counts[j],
                };
                ret = iot_button_register_cb(button_handle->button_handles[i], BUTTON_MULTIPLE_CLICK, &multiple_click_temp_args, button_event_cb, user_data);
                ESP_BOARD_RETURN_ON_FALSE(ret == ESP_OK, -1, TAG, "Failed to register multi_click (click_counts: %d) callback: %s", config->events_cfg.multi_click.click_counts[j], esp_err_to_name(ret));
            }
        }
    }

    ESP_LOGI(TAG, "Successfully registered callback for %s", config->name);
    return 0;
}

DEVICE_EXTRA_FUNC_REGISTER(button, dev_button_register_callback);

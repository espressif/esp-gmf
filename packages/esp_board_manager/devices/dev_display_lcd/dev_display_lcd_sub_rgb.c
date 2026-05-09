/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_board_device.h"
#include "esp_board_entry.h"
#include "dev_display_lcd.h"

static const char *TAG = "DEV_DISPLAY_LCD_SUB_RGB";

static bool dev_display_lcd_rgb_has_user_fbs_func(const dev_display_lcd_config_t *lcd_cfg)
{
    return lcd_cfg->sub_cfg.rgb.user_fbs_func && strlen(lcd_cfg->sub_cfg.rgb.user_fbs_func) > 0;
}

static int dev_display_lcd_rgb_call_user_fbs_func(const dev_display_lcd_config_t *lcd_cfg,
                                                  dev_display_lcd_rgb_user_fbs_action_t action,
                                                  void *user_fbs[ESP_RGB_LCD_PANEL_MAX_FB_NUM])
{
    if (!dev_display_lcd_rgb_has_user_fbs_func(lcd_cfg)) {
        return 0;
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    void *extra_func = NULL;
    if (esp_board_extra_func_get(lcd_cfg->sub_cfg.rgb.user_fbs_func, &extra_func) != 0) {
        ESP_LOGE(TAG, "RGB user frame buffer function '%s' not found", lcd_cfg->sub_cfg.rgb.user_fbs_func);
        return -1;
    }

    dev_display_lcd_rgb_user_fbs_func_t user_fbs_func = (dev_display_lcd_rgb_user_fbs_func_t)extra_func;
    return user_fbs_func(lcd_cfg, action, user_fbs);
#else
    ESP_LOGE(TAG, "RGB user frame buffers require ESP-IDF v6.0 or later");
    return -1;
#endif  /* ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0) */
}

int dev_display_lcd_sub_rgb_init(void *cfg, int cfg_size, void **device_handle)
{
    dev_display_lcd_config_t *lcd_cfg = (dev_display_lcd_config_t *)cfg;
    dev_display_lcd_handles_t *lcd_handles = calloc(1, sizeof(dev_display_lcd_handles_t));
    if (lcd_handles == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for dev_display_lcd_sub_rgb");
        return -1;
    }

    ESP_LOGI(TAG, "Initializing RGB LCD display: %s, chip: %s", lcd_cfg->name, lcd_cfg->chip);

    esp_lcd_rgb_panel_config_t panel_config = lcd_cfg->sub_cfg.rgb.panel_config;
    bool user_fbs_acquired = false;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    if (dev_display_lcd_rgb_has_user_fbs_func(lcd_cfg)) {
        int ret = dev_display_lcd_rgb_call_user_fbs_func(lcd_cfg, DEV_DISPLAY_LCD_RGB_USER_FBS_GET, panel_config.user_fbs);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to get RGB user frame buffers: %d", ret);
            goto cleanup;
        }
        memcpy(lcd_handles->rgb_user_fbs, panel_config.user_fbs, sizeof(lcd_handles->rgb_user_fbs));
        user_fbs_acquired = true;
    }
#else
    if (dev_display_lcd_rgb_has_user_fbs_func(lcd_cfg)) {
        dev_display_lcd_rgb_call_user_fbs_func(lcd_cfg, DEV_DISPLAY_LCD_RGB_USER_FBS_GET, NULL);
        goto cleanup;
    }
#endif  /* ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0) */

    esp_err_t ret = esp_lcd_new_rgb_panel(&panel_config, &lcd_handles->panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RGB LCD panel: %s", esp_err_to_name(ret));
        goto cleanup_user_fbs;
    }

    lcd_handles->io_handle = NULL;
    *device_handle = lcd_handles;
    return 0;

cleanup_user_fbs:
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    if (user_fbs_acquired) {
        int release_ret = dev_display_lcd_rgb_call_user_fbs_func(lcd_cfg, DEV_DISPLAY_LCD_RGB_USER_FBS_RELEASE,
                                                                 lcd_handles->rgb_user_fbs);
        if (release_ret != 0) {
            ESP_LOGW(TAG, "Failed to release RGB user frame buffers after init failure: %d", release_ret);
        }
    }
#endif  /* ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0) */
cleanup:
    free(lcd_handles);
    return -1;
}

int dev_display_lcd_sub_rgb_deinit_with_config(void *device_handle, const dev_display_lcd_config_t *cfg)
{
    if (device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    dev_display_lcd_handles_t *lcd_handles = (dev_display_lcd_handles_t *)device_handle;

    if (lcd_handles->panel_handle) {
        esp_lcd_panel_del(lcd_handles->panel_handle);
        lcd_handles->panel_handle = NULL;
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    if (cfg && dev_display_lcd_rgb_has_user_fbs_func(cfg)) {
        int release_ret = dev_display_lcd_rgb_call_user_fbs_func(cfg, DEV_DISPLAY_LCD_RGB_USER_FBS_RELEASE,
                                                                 lcd_handles->rgb_user_fbs);
        if (release_ret != 0) {
            ESP_LOGW(TAG, "Failed to release RGB user frame buffers: %d", release_ret);
        }
    }
#endif  /* ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0) */

    free(device_handle);
    return 0;
}

int dev_display_lcd_sub_rgb_deinit(void *device_handle)
{
    dev_display_lcd_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    return dev_display_lcd_sub_rgb_deinit_with_config(device_handle, cfg);
}

ESP_BOARD_ENTRY_IMPLEMENT(rgb, dev_display_lcd_sub_rgb_init, dev_display_lcd_sub_rgb_deinit);

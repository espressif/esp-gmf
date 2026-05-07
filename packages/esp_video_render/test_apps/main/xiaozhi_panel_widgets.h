/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_video_render_types.h"
#include "esp_vui_widget.h"
#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef enum {
    WIFI_STRENGTH_NONE   = 0,
    WIFI_STRENGTH_WEAK   = 1,
    WIFI_STRENGTH_MEDIUM = 2,
    WIFI_STRENGTH_STRONG = 3,
} wifi_strength_t;

typedef enum {
    BATTERY_LEVEL_0   = 0,
    BATTERY_LEVEL_25  = 1,
    BATTERY_LEVEL_50  = 2,
    BATTERY_LEVEL_75  = 3,
    BATTERY_LEVEL_100 = 4,
} battery_level_t;

int set_widget_font(esp_vui_widget_t *widget, const char *font_name, int font_size);

esp_video_render_err_t xiaozhi_panel_create(esp_vui_overlay_handle_t overlay,
                                            esp_video_render_frame_info_t *frame_info,
                                            esp_video_render_pos_t *pos,
                                            bool with_cache,
                                            void **panel_handle);

esp_video_render_err_t xiaozhi_panel_set_status(void *panel_handle,
                                                const char *status_text,
                                                wifi_strength_t wifi_strength,
                                                battery_level_t battery_level);

esp_video_render_err_t xiaozhi_panel_set_speaking(void *panel_handle, bool is_speaking);

esp_video_render_err_t xiaozhi_panel_set_emoji(void *panel_handle, const char *emoji);

esp_video_render_err_t xiaozhi_panel_set_text_lines(void *panel_handle, char *lines);

esp_video_render_err_t xiaozhi_panel_destroy(void *panel_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

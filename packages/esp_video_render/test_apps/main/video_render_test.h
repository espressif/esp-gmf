/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_video_render.h"
#include "esp_video_render_backend.h"
#include "esp_vui_widget.h"
#include "esp_vui_container.h"
#include "esp_video_render_types.h"
#include "video_pattern.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define BREAK_ON_FAIL(sta)  {                                             \
    int _ret = sta;                                                       \
    if (_ret) {                                                           \
        ESP_LOGE(TAG, "Fail at %s:%d ret %d", __func__, __LINE__, _ret);  \
        break;                                                            \
    }                                                                     \
}

#define BREAK_ON_NULL(ptr)  {                                        \
    if ((ptr) == NULL) {                                             \
        ESP_LOGE(TAG, "Null pointer at %s:%d", __func__, __LINE__);  \
        break;                                                       \
    }                                                                \
}

const esp_video_render_backend_ops_t *video_render_get_fake_backend(void);

typedef struct {
    const esp_video_render_backend_ops_t *ops;
    bool                                  is_lvgl;
    union {
        esp_video_render_lcd_cfg_t   lcd_cfg;
        esp_video_render_lvgl_cfg_t  lvgl_cfg;
    };
} backend_cfg_t;

esp_vui_widget_t *esp_vui_widget_clock_init(esp_vui_container_handle_t container,
                                            esp_video_render_frame_info_t *frame_info,
                                            esp_video_render_pos_t *pos,
                                            int size,
                                            esp_video_render_clr_t *bg_color,
                                            esp_video_render_clr_t *hour_color,
                                            esp_video_render_clr_t *minute_color,
                                            esp_video_render_clr_t *second_color);

esp_video_render_err_t esp_vui_widget_clock_update(esp_vui_widget_t *widget);

int create_video_render(uint8_t fps);

void video_render_use_lvgl(bool use_lvgl);

esp_video_render_handle_t create_lvgl_render(uint16_t width, uint16_t height, uint8_t fps);

void delete_lvgl_render(esp_video_render_handle_t handle);

esp_video_render_handle_t get_video_render(void);

void *get_render_pool(void);

backend_cfg_t *get_lcd_backend_cfg(void);

void destroy_video_render(void);

int video_render_lcd_backend_fb_test(int count);

int video_render_lcd_backend_none_fb_test(int count);

int video_render_proc_decode_test(int write_count);

int video_render_proc_color_convert_test(int write_count);

int video_render_proc_scale_test(int write_count);

int video_render_stream_rotate_test(int frame_count);

int video_render_proc_chain_test(int write_count);

int video_render_image_decode_test(int write_count);

int video_render_proc_wrapper_test(int count);

int video_render_blend_test(int write_count);

int video_render_blend_bitblt_test(int write_count);

int video_render_blend_transparent_color_test(int write_count);

int video_render_one_stream_with_fb(int write_count);

int video_render_compose_monitor_test(void);

// New demos
int demo_one_stream_video_only(int frame_count);
int demo_one_stream_video_with_overlay(int frame_count);
int demo_dual_stream_overlay_only(int frame_count);
int demo_dual_streams_video(int frame_count);
int demo_stream_visible(int frame_count);
int simple_widget_test(bool with_cache, int frame_count);
int dual_container_no_overlap(bool with_cache, int frame_count);
int dual_container_overlap(bool with_cache, int frame_count);
int demo_bouncing_balls_game(int frame_count);
int demo_clock_widget(bool with_cache, int frame_count);
int demo_text_widget(bool with_cache, int frame_count, bool test_scroll, bool test_emoji);
int demo_text_widget_alignment(bool with_cache, int frame_count);
int demo_text_widget_scroll(bool with_cache, int frame_count);
int demo_stream_src_rect_change(int frame_count);
int demo_stream_disp_rect_change(int frame_count);
int demo_stream_src_disp_rect_change(int frame_count);
int demo_dual_stream_rect_change(int frame_count);
int demo_stream_zorder_test(int frame_count);
int demo_fullscreen_stream_with_overlay_widget(int frame_count);
int demo_dual_stream_with_overlay(int frame_count);
int demo_xiaozhi_panel(bool with_cache, int frame_count);
int demo_dual_eyes_on_single_display(int repeat_count);
int demo_dual_eyes_single_display_on_lvgl(int repeat_count);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

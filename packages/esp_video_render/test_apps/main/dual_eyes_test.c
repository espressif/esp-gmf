/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "video_render_test.h"
#include "esp_video_render_dual_stream.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG  "DUAL_EYES_TEST"

#define LEFT_EYES_FILE   "/sdcard/left.mjpeg"
#define RIGHT_EYES_FILE  "/sdcard/right.mjpeg"

typedef struct {
    FILE                     *fp;
    bool                      eos;
    uint8_t                   idx;
    uint8_t                  *last_data;
    int                       last_size;
    esp_video_render_frame_t  frame;
} eyes_info_t;

static int get_frame_end(uint8_t *data, int size, bool eof)
{
    for (int i = 0; i < size - 1; i++) {
        if (data[i] == 0xFF && data[i + 1] == 0xD9) {
            if (i + 3 < size) {
                if (data[i + 2] == 0xFF && data[i + 3] == 0xD8) {
                    return i + 2;
                }
            }
            if (eof && i == size - 2) {
                return i + 2;
            }
        }
    }
    return -1;
}

int read_mjpeg_frame(esp_video_render_dual_stream_handle_t eye, eyes_info_t *info)
{
    esp_video_render_frame_t *frame = &info->frame;
    frame->format = ESP_VIDEO_RENDER_FORMAT_MJPEG;
    int ret = esp_video_render_dual_stream_get_buffer(eye, info->idx, frame);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Fail to get buffer ret %d", ret);
        return ret;
    }
    int filled = 0;
    if (info->last_size) {
        memmove(frame->data, info->last_data, info->last_size);
        filled = info->last_size;
        info->last_size = 0;
    } else if (info->eos) {
        return 1;
    }
    int frame_end = -1;
    do {
        frame_end = get_frame_end(frame->data, filled, info->eos);
        if (frame_end > 0) {
            break;
        }
        int left = frame->size - filled;
        if (left <= 0) {
            return -1;
        }
        int rd = (int)fread(frame->data + filled, 1, left, info->fp);
        if (rd == 0 && ferror(info->fp)) {
            return -1;
        }
        filled += rd;
        info->eos = feof(info->fp);
        if (rd) {
            frame_end = get_frame_end(frame->data, filled, info->eos);
        }
    } while (0);
    if (frame_end <= 0) {
        ESP_LOGE(TAG, "Not find in filled %d", filled);
        return -1;
    }
    frame->size = frame_end;
    info->last_data = frame->data + frame->size;
    info->last_size = filled - frame->size;
    return 0;
}

int demo_dual_eyes_on_single_display(int repeat_count)
{
    bool success = false;
    esp_video_render_dual_stream_handle_t eye = NULL;
    eyes_info_t eye_info[2] = {};
    do {
        eye_info[0].idx = 0;
        eye_info[1].idx = 1;
        eye_info[0].fp = fopen(LEFT_EYES_FILE, "rb");
        eye_info[1].fp = fopen(RIGHT_EYES_FILE, "rb");
        if (eye_info[0].fp == NULL || eye_info[1].fp == NULL) {
            success = true;
            ESP_LOGW(TAG, "File can not open skip test");
            break;
        }
        int ret = create_video_render(40);
        BREAK_ON_FAIL(ret);
        esp_video_render_handle_t render = get_video_render();
        esp_video_render_dual_stream_cfg_t cfg = {
            .render = {render, render},
            .frame_count = 2,
        };
        ret = esp_video_render_dual_stream_open(&cfg, &eye);
        BREAK_ON_FAIL(ret);
        int eye_width = 240;
        int eye_height = 240;
        backend_cfg_t *backend_cfg = get_lcd_backend_cfg();
        uint16_t display_width = backend_cfg->is_lvgl ? backend_cfg->lvgl_cfg.width : backend_cfg->lcd_cfg.width;
        uint16_t display_height = backend_cfg->is_lvgl ? backend_cfg->lvgl_cfg.height : backend_cfg->lcd_cfg.height;
        while (display_width < eye_width * 2 || display_height < eye_height) {
            // Scale to half
            eye_width /= 2;
            eye_height /= 2;
        }

        // Place into middle of screen
        esp_video_render_rect_t rect;
        rect.x = (display_width - eye_width * 2) / 4;
        rect.y = (display_height - eye_height) / 2;
        rect.width = eye_width;
        rect.height = eye_height;
        ret = esp_video_render_dual_stream_set_display_rect(eye, 0, &rect);
        BREAK_ON_FAIL(ret);

        rect.x += display_width / 2;
        ret = esp_video_render_dual_stream_set_display_rect(eye, 1, &rect);
        BREAK_ON_FAIL(ret);

        for (int i = 0; i < repeat_count; i++) {
            // Read until eos
            while (1) {
                ret = read_mjpeg_frame(eye, &eye_info[0]);
                if (ret != 0) {
                    esp_video_render_dual_stream_release_buffer(eye, 0, &eye_info[0].frame);
                    break;
                }
                ret = read_mjpeg_frame(eye, &eye_info[1]);
                if (ret != 0) {
                    esp_video_render_dual_stream_release_buffer(eye, 0, &eye_info[0].frame);
                    esp_video_render_dual_stream_release_buffer(eye, 1, &eye_info[1].frame);
                    break;
                }
                ret = esp_video_render_dual_stream_send_buffer(eye, &eye_info[0].frame, &eye_info[1].frame);
                BREAK_ON_FAIL(ret);
            }
            if (ret > 0) {
                ret = 0;
            }
            BREAK_ON_FAIL(ret);
            // Seek back and retry
            for (int i = 0; i < 2; i++) {
                fseek(eye_info[i].fp, 0, SEEK_SET);
                eye_info[i].eos = false;
                eye_info[i].last_size = 0;
                eye_info[i].last_data = NULL;
            }
        }
        BREAK_ON_FAIL(ret);

        success = true;
    } while (0);
    if (eye) {
        esp_video_render_dual_stream_close(eye);
    }
    for (int i = 0; i < 2; i++) {
        if (eye_info[i].fp) {
            fclose(eye_info[i].fp);
        }
    }
    destroy_video_render();
    return success ? 0 : -1;
}

int demo_dual_eyes_single_display_on_lvgl(int repeat_count)
{
    bool success = false;
    esp_video_render_dual_stream_handle_t eye = NULL;
    eyes_info_t eye_info[2] = {};
    esp_video_render_handle_t lvgl_render[2] = {NULL};
    do {
        eye_info[0].idx = 0;
        eye_info[1].idx = 1;
        eye_info[0].fp = fopen(LEFT_EYES_FILE, "rb");
        eye_info[1].fp = fopen(RIGHT_EYES_FILE, "rb");
        if (eye_info[0].fp == NULL || eye_info[1].fp == NULL) {
            success = true;
            ESP_LOGW(TAG, "File can not open skip test");
            break;
        }
        video_render_use_lvgl(true);
        int ret = create_video_render(30);
        BREAK_ON_FAIL(ret);
        lvgl_render[0] = create_lvgl_render(240, 240, 30);
        if (lvgl_render[0] == NULL) {
            BREAK_ON_FAIL(-1);
        }
        lvgl_render[1] = create_lvgl_render(240, 240, 30);
        if (lvgl_render[1] == NULL) {
            BREAK_ON_FAIL(-1);
        }
        esp_video_render_dual_stream_cfg_t cfg = {
            .render = {lvgl_render[0], lvgl_render[1]},
            .frame_count = 2,
        };
        ret = esp_video_render_dual_stream_open(&cfg, &eye);
        BREAK_ON_FAIL(ret);
        int eye_width = 240;
        int eye_height = 240;

        // Place into middle of screen
        esp_video_render_rect_t rect;
        backend_cfg_t *backend_cfg = get_lcd_backend_cfg();
        uint16_t display_width = backend_cfg->is_lvgl ? backend_cfg->lvgl_cfg.width : backend_cfg->lcd_cfg.width;
        uint16_t display_height = backend_cfg->is_lvgl ? backend_cfg->lvgl_cfg.height : backend_cfg->lcd_cfg.height;
        rect.x = display_width / 4 - eye_width / 2;
        rect.y = (display_height - eye_height) / 2;
        rect.width = eye_width;
        rect.height = eye_height;
        ret = esp_video_render_dual_stream_set_display_rect(eye, 0, &rect);
        BREAK_ON_FAIL(ret);

        rect.x += display_width / 2;
        ret = esp_video_render_dual_stream_set_display_rect(eye, 1, &rect);
        BREAK_ON_FAIL(ret);

        for (int i = 0; i < repeat_count; i++) {
            // Read until eos
            while (1) {
                ret = read_mjpeg_frame(eye, &eye_info[0]);
                if (ret != 0) {
                    esp_video_render_dual_stream_release_buffer(eye, 0, &eye_info[0].frame);
                    break;
                }
                ret = read_mjpeg_frame(eye, &eye_info[1]);
                if (ret != 0) {
                    esp_video_render_dual_stream_release_buffer(eye, 0, &eye_info[0].frame);
                    esp_video_render_dual_stream_release_buffer(eye, 1, &eye_info[1].frame);
                    break;
                }
                ret = esp_video_render_dual_stream_send_buffer(eye, &eye_info[0].frame, &eye_info[1].frame);
                BREAK_ON_FAIL(ret);
            }
            if (ret > 0) {
                ret = 0;
            }
            BREAK_ON_FAIL(ret);
            // Seek back and retry
            for (int i = 0; i < 2; i++) {
                fseek(eye_info[i].fp, 0, SEEK_SET);
                eye_info[i].eos = false;
                eye_info[i].last_size = 0;
                eye_info[i].last_data = NULL;
            }
        }
        BREAK_ON_FAIL(ret);

        success = true;
    } while (0);
    if (eye) {
        esp_video_render_dual_stream_close(eye);
    }
    for (int i = 0; i < 2; i++) {
        if (eye_info[i].fp) {
            fclose(eye_info[i].fp);
        }
    }
    for (int i = 0; i < 2; i++) {
        if (lvgl_render[i]) {
            delete_lvgl_render(lvgl_render[i]);
        }
    }
    destroy_video_render();
    return success ? 0 : -1;
}

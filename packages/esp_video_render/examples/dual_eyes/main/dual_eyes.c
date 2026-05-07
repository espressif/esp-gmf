/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <string.h>
#include "dual_eyes.h"
#include "settings.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_gmf_oal_thread.h"
#include "esp_gmf_oal_sys.h"
#include "esp_video_render_dual_stream.h"

#define TAG  "DUAL_EYES"

typedef struct {
    FILE    *fp;
    char    *fp_cache;
    bool     eos;
    uint8_t  idx;
    uint8_t *last_data;
    int      last_size;
} eyes_info_t;

static int play_repeat_count = 5;

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

int read_mjpeg_frame(esp_video_render_dual_stream_handle_t eye, eyes_info_t *info, esp_video_render_frame_t *frame)
{
    frame->format = ESP_VIDEO_RENDER_FORMAT_MJPEG;
    frame->size = 0;
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

void dual_eyes_set_repeat_count(int repeat_count)
{
    if (repeat_count > 0) {
        play_repeat_count = repeat_count;
    }
}

volatile enum {
    MONITOR_STAT_NONE,
    MONITOR_STAT_START,
    MONITOR_STAT_STOP,
} monitor_sys_state = MONITOR_STAT_NONE;

static void monitor_thread(void *arg)
{
    while (monitor_sys_state == MONITOR_STAT_START) {
        esp_gmf_oal_sys_get_real_time_stats(5000, false);
    }
    monitor_sys_state = MONITOR_STAT_NONE;
    ESP_LOGI(TAG, "Monitor thread exit");
    esp_gmf_oal_thread_delete(NULL);
}

static void monitor_enable(bool enable)
{
    if (enable) {
        if (monitor_sys_state != MONITOR_STAT_START) {
            monitor_sys_state = MONITOR_STAT_START;
            if (esp_gmf_oal_thread_create(NULL, "Monitor", monitor_thread, NULL, 4096, 10, false, 0)) {
                monitor_sys_state = MONITOR_STAT_NONE;
            }
        }
    } else {
        if (monitor_sys_state == MONITOR_STAT_START) {
            monitor_sys_state = MONITOR_STAT_STOP;
        }
        while (monitor_sys_state != MONITOR_STAT_NONE) {
            vTaskDelay(50);
        }
    }
}

int dual_eyes_on_one_display(char *left_mjpeg, char *right_mjpeg, int fps, bool use_lvgl)
{
    bool success = false;
    esp_video_render_dual_stream_handle_t eye = NULL;
    eyes_info_t eye_info[2] = {};
    do {
        // Open files
        eye_info[0].idx = 0;
        eye_info[1].idx = 1;
        eye_info[0].fp = fopen(left_mjpeg, "rb");
        eye_info[1].fp = fopen(right_mjpeg, "rb");
        if (eye_info[0].fp == NULL || eye_info[1].fp == NULL) {
            success = true;
            ESP_LOGW(TAG, "File can not open skip test");
            break;
        }
        // Create render
        video_render_use_lvgl(use_lvgl);
        int ret = create_video_render(fps);
        BREAK_ON_FAIL(ret);
        esp_video_render_handle_t render = get_video_render(0);
        if (render == NULL) {
            BREAK_ON_FAIL(-1);
        }

        // Set background color
        esp_video_render_clr_t bg_color = {.r = 0x40, .g = 0x40, .b = 0x40};
        esp_video_render_set_bg_color(render, &bg_color);

        // Open dual eyes render
        esp_video_render_dual_stream_cfg_t cfg = {
            .render = {render, render},
            .frame_count = 3,
            .fps = fps,
        };
        render = get_video_render(1);
        if (render) {
            //  cfg.render[1] = render;
        }
        ret = esp_video_render_dual_stream_open(&cfg, &eye);
        BREAK_ON_FAIL(ret);

        // Calculation display rectangle for each eye
        int eye_width = VIDEO_WIDTH;
        int eye_height = VIDEO_HEIGHT;
        video_render_info_t render_info = {};
        get_video_render_info(0, &render_info);
        if (render_info.width < eye_width || render_info.height < eye_height) {
            ESP_LOGE(TAG, "Display too small for dual eyes");
            BREAK_ON_FAIL(-1);
        }
        if (render_info.width < eye_width * 2 || render_info.height < eye_height * 2) {
            // Scale to half
            eye_width /= 2;
            eye_height /= 2;
        }
        esp_video_render_rect_t rect;
        rect.x = (render_info.width / 2 - eye_width) / 2;
        rect.y = (render_info.height - eye_height) / 2;
        rect.width = eye_width;
        rect.height = eye_height;
        ret = esp_video_render_dual_stream_set_display_rect(eye, 0, &rect);
        BREAK_ON_FAIL(ret);
        rect.x += render_info.width / 2;
        ret = esp_video_render_dual_stream_set_display_rect(eye, 1, &rect);
        BREAK_ON_FAIL(ret);

        // Loop read frames and render it out
        int loop_count = play_repeat_count;
        monitor_enable(true);
        for (; loop_count; loop_count--) {
            // Read until eos
            esp_video_render_frame_t frame[2] = {};
            int start_time = esp_timer_get_time() / 1000;
            int frame_num = 0;
            while (1) {
                // Read for frames
                for (int i = 0; i < 2; i++) {
                    ret = read_mjpeg_frame(eye, &eye_info[i], &frame[i]);
                    if (ret != 0) {
                        for (int j = 0; j <= i; j++) {
                            esp_video_render_dual_stream_release_buffer(eye, j, &frame[j]);
                        }
                        break;
                    }
                }
                if (ret > 0) {
                    break;
                }
                BREAK_ON_FAIL(ret);
                ret = esp_video_render_dual_stream_send_buffer(eye, &frame[0], &frame[1]);
                BREAK_ON_FAIL(ret);
                frame_num++;
            }
            if (ret > 0) {
                int cur = esp_timer_get_time() / 1000;
                if (frame_num && cur > start_time) {
                    ESP_LOGI(TAG, "FPS: %d fps", frame_num * 1000 / (cur - start_time));
                }
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
    monitor_enable(false);
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

int dual_eyes_on_dual_display(char *left_mjpeg, char *right_mjpeg, int fps, bool use_lvgl)
{
    bool success = false;
    esp_video_render_dual_stream_handle_t eye = NULL;
    eyes_info_t eye_info[2] = {};
    do {
        // Open files
        for (int i = 0; i < 2; i++) {
            eye_info[i].idx = i;
            eye_info[i].fp = fopen(i ? RIGHT_FILE : LEFT_FILE, "rb");
            if (eye_info[i].fp == NULL) {
                break;
            }
            int fp_cache_size = 4 * 1024;
            eye_info[i].fp_cache = (char *)heap_caps_malloc(fp_cache_size, MALLOC_CAP_INTERNAL);
            if (eye_info[i].fp_cache) {
                setvbuf(eye_info[i].fp, eye_info[i].fp_cache, _IOFBF, fp_cache_size);
            }
        }
        if (eye_info[0].fp == NULL || eye_info[1].fp == NULL) {
            success = true;
            ESP_LOGW(TAG, "File can not open skip test");
            break;
        }
        int ret = 0;
        esp_video_render_dual_stream_cfg_t cfg = {
            .frame_count = 3,
            .fps = fps,  // Set fps will limit decode speed <= such fps
            .render_async = true,
        };
        // Create render
        video_render_use_lvgl(use_lvgl);
        ret = create_video_render(fps);
        BREAK_ON_FAIL(ret);

        for (int i = 0; i < 2; i++) {
            cfg.render[i] = get_video_render(i);
            if (cfg.render[i] == NULL) {
                ret = -1;
                BREAK_ON_FAIL(ret);
            }
            // Set background color
            esp_video_render_clr_t bg_color = {.r = 0x40, .g = 0x40, .b = 0x40};
            esp_video_render_set_bg_color(cfg.render[i], &bg_color);
        }
        BREAK_ON_FAIL(ret);

        // Open dual eyes render
        ret = esp_video_render_dual_stream_open(&cfg, &eye);
        BREAK_ON_FAIL(ret);

        // Calculation display rectangle for each eye
        for (int i = 0; i < 2; i++) {
            int eye_width = VIDEO_WIDTH;
            int eye_height = VIDEO_HEIGHT;
            video_render_info_t render_info = {};
            get_video_render_info(i, &render_info);
            if (render_info.width < eye_width || render_info.height < eye_height) {
                ESP_LOGE(TAG, "Display too small for dual eyes");
                ret = -1;
                BREAK_ON_FAIL(ret);
            }
            esp_video_render_rect_t rect = {};
            rect.x = (render_info.width - eye_width) / 2;
            rect.y = (render_info.height - eye_height) / 2;
            rect.width = eye_width;
            rect.height = eye_height;
            ret = esp_video_render_dual_stream_set_display_rect(eye, i, &rect);
            BREAK_ON_FAIL(ret);
        }
        monitor_enable(true);
        // Loop read frames and render it out
        int loop_count = play_repeat_count;
        for (; loop_count; loop_count--) {
            // Read until eos
            esp_video_render_frame_t frame[2] = {};
            int start_time = esp_timer_get_time() / 1000;
            int frame_num = 0;
            while (1) {
                // Read for frames
                for (int i = 0; i < 2; i++) {
                    ret = read_mjpeg_frame(eye, &eye_info[i], &frame[i]);
                    if (ret != 0) {
                        for (int j = 0; j <= i; j++) {
                            esp_video_render_dual_stream_release_buffer(eye, j, &frame[j]);
                        }
                        break;
                    }
                }
                if (ret > 0) {
                    break;
                }
                BREAK_ON_FAIL(ret);
                ret = esp_video_render_dual_stream_send_buffer(eye, &frame[0], &frame[1]);
                BREAK_ON_FAIL(ret);
                frame_num++;
            }
            if (ret > 0) {
                int cur = esp_timer_get_time() / 1000;
                if (frame_num && cur > start_time) {
                    ESP_LOGI(TAG, "FPS: %d fps", frame_num * 1000 / (cur - start_time));
                }
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
    monitor_enable(false);
    if (eye) {
        esp_video_render_dual_stream_close(eye);
    }
    for (int i = 0; i < 2; i++) {
        if (eye_info[i].fp) {
            fclose(eye_info[i].fp);
        }
        if (eye_info[i].fp_cache) {
            free(eye_info[i].fp_cache);
        }
    }
    destroy_video_render();
    return success ? 0 : -1;
}

/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <SDL.h>

#include "esp_log.h"
#include "esp_video_render.h"
#include "esp_video_render_backend.h"
#include "esp_vui_overlay.h"
#include "esp_vui_container.h"
#include "esp_vui_widget.h"
#include "esp_vui_widget_default.h"
#include "esp_video_render_dual_stream.h"
#include "assets_path.h"
#include "sdl_backend.h"

#define TAG  "VR_EMU"

const int width = 480;
const int height = 320;

static void fill_test_pattern_rgb565(uint8_t *buf, int w, int h, int frame)
{
    // Simple moving gradient
    uint16_t *p = (uint16_t *)buf;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int r = (x + frame) & 0x1F;
            int g = (y + frame) & 0x3F;
            int b = (x + y + frame) & 0x1F;
            uint16_t rgb565 = (uint16_t)((r << 11) | (g << 5) | b);
            p[y * w + x] = rgb565;
        }
    }
}

int main(int argc, char **argv)
{
    bool use_dual_eyes = false;
    for (int iarg = 1; iarg < argc; iarg++) {
        if (strcmp(argv[iarg], "--dual-eyes") == 0) {
            use_dual_eyes = true;
        }
    }
    esp_video_render_cfg_t cfg = {
        .pool = NULL,
        .fps = 10,
    };
    esp_video_render_handle_t render = NULL;
    if (esp_video_render_create(&cfg, &render) != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "esp_video_render_create failed");
        return 1;
    }

    sdl_backend_cfg_t bk_cfg = {
        .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
        .width = width,
        .height = height,
    };
    esp_video_render_backend_cfg_t disp = {
        .ops = esp_video_render_get_sdl_backend(),
        .cfg = &bk_cfg,
        .cfg_size = sizeof(bk_cfg),
    };
    if (esp_video_render_set_display(render, &disp) != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "esp_video_render_set_display failed");
        return 1;
    }

    // Stream (single)
    esp_video_render_stream_info_t stream_info = {
        .info = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = width,
            .height = height,
            .fps = 30,
        },
        .cached = false,
    };
    esp_video_render_stream_handle_t stream = NULL;
    if (esp_video_render_stream_open(render, &stream_info, &stream) != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "stream_open failed");
        return 1;
    }
    (void)esp_video_render_stream_render_async(stream);

    // Overlay + a text widget (emoji)
    esp_vui_overlay_handle_t overlay = NULL;
    (void)esp_video_render_stream_get_overlay(stream, &overlay);

    // Container covers full display
    esp_vui_container_handle_t container = NULL;
    esp_video_render_frame_info_t container_info = {
        .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
        .width = width,
        .height = height,
    };
    esp_video_render_pos_t container_pos = {.x = 0, .y = 0};
    (void)esp_vui_container_create(overlay, &container_info, &container_pos, true, &container);

    esp_video_render_pos_t text_pos = {.x = 20, .y = 20};
    esp_vui_widget_t *txt = esp_vui_text_widget_init(container, &container_info, &text_pos, 200, 120);
    if (txt) {
        (void)esp_vui_text_widget_set_align(txt, 1  /*center*/, 1  /*middle*/);
        (void)esp_vui_text_widget_set_text(txt, "😊");

        char font_path[1024];
        if (get_assets_path("DejaVuSans.ttf", font_path, sizeof(font_path)) == 0) {
            (void)esp_vui_text_widget_set_font(txt, font_path, 24);
        }
        if (get_assets_path("NotoColorEmojiBitmap-Subset.ttf", font_path, sizeof(font_path)) == 0) {
            (void)esp_vui_text_widget_set_emoji_font(txt, font_path, 80);
        }
    }

    // Optional: dual-eyes mode (two sources displayed left/right in the same SDL window).
    // This reuses the existing `video_render_dual_eyes` implementation.
    esp_video_render_dual_stream_handle_t eyes = NULL;
    if (use_dual_eyes) {
        esp_video_render_dual_stream_cfg_t eyes_cfg = {
            .render = {render, render},
            .frame_count = 2,
            .max_frame_size = (uint32_t)(width * height * 2),
            .fps = 30,
            .render_async = true,
        };
        if (esp_video_render_dual_stream_open(&eyes_cfg, &eyes) == ESP_VIDEO_RENDER_ERR_OK) {
            esp_video_render_rect_t left = {.x = 0, .y = 0, .width = width / 2, .height = height};
            esp_video_render_rect_t right = {.x = width / 2, .y = 0, .width = width / 2, .height = height};
            (void)esp_video_render_dual_stream_set_display_rect(eyes, 0, &left);
            (void)esp_video_render_dual_stream_set_display_rect(eyes, 1, &right);
            ESP_LOGI(TAG, "Dual-eyes enabled: left/right half-screen");
        } else {
            ESP_LOGW(TAG, "Dual-eyes open failed, falling back to single stream");
            eyes = NULL;
        }
    }

    // Main loop: push synthetic frames; in dual-eyes mode we send two buffers.
    uint8_t *frame_buf = (uint8_t *)malloc(width * height * 2);
    if (frame_buf) {
        esp_video_render_frame_t frame = {
            .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
            .width = width,
            .height = height,
            .data = frame_buf,
            .size = width * height * 2,
        };

        bool running = true;
        int i = 0;
        while (running && i < 100) {
            if (eyes) {
                esp_video_render_frame_t fa = {0}, fb = {0};
                if (esp_video_render_dual_stream_get_buffer(eyes, 0, &fa) == ESP_VIDEO_RENDER_ERR_OK &&
                    esp_video_render_dual_stream_get_buffer(eyes, 1, &fb) == ESP_VIDEO_RENDER_ERR_OK) {
                    fa.format = ESP_VIDEO_RENDER_FORMAT_RGB565;
                    fa.width = width / 2;
                    fa.height = height;
                    fa.size = (uint32_t)(fa.width * fa.height * 2);

                    fb.format = ESP_VIDEO_RENDER_FORMAT_RGB565;
                    fb.width = width / 2;
                    fb.height = height;
                    fb.size = (uint32_t)(fb.width * fb.height * 2);

                    fill_test_pattern_rgb565(fa.data, fa.width, fa.height, i);
                    fill_test_pattern_rgb565(fb.data, fb.width, fb.height, i + 77);
                    (void)esp_video_render_dual_stream_send_buffer(eyes, &fa, &fb);
                }
                i++;
            } else {
                fill_test_pattern_rgb565(frame_buf, width, height, i++);
                (void)esp_video_render_stream_write(stream, &frame);
            }
            usleep(10000);
        }

        free(frame_buf);
    }

    if (eyes) {
        esp_video_render_dual_stream_close(eyes);
    }
    esp_video_render_stream_close(stream);
    esp_video_render_destroy(render);
    return 0;
}

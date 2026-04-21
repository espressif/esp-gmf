/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "esp_video_render.h"
#include "esp_video_render_backend.h"
#include "esp_vui_overlay.h"
#include "esp_vui_container.h"
#include "esp_vui_widget.h"
#include "esp_vui_widget_default.h"
#include "sdl_backend.h"

static void fill_checker_rgb565(uint8_t *buf, int w, int h, uint16_t a, uint16_t b)
{
    for (int y = 0; y < h; y++) {
        uint16_t *row = (uint16_t *)(buf + y * w * 2);
        for (int x = 0; x < w; x++) {
            row[x] = (((x ^ y) & 1) ? a : b);
        }
    }
}

static int count_nonzero_bytes(const uint8_t *buf, size_t n)
{
    int cnt = 0;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] != 0) {
            cnt++;
        }
    }
    return cnt;
}

int main(void)
{
    // Only use dummy driver if explicitly set (for CI)
    // Otherwise, show the window for visual testing
    const char *env_driver = getenv("SDL_VIDEODRIVER");
    if (!env_driver) {
        printf("SDL window will be displayed (set SDL_VIDEODRIVER=dummy for headless)\n");
    }

    const int W = 320, H = 240;  // Larger window to see the image widget
    printf("[test] Initializing video render with %dx%d display...\n", W, H);

    esp_video_render_cfg_t cfg = {.pool = NULL, .fps = 30};
    esp_video_render_handle_t render = NULL;
    assert(esp_video_render_create(&cfg, &render) == ESP_VIDEO_RENDER_ERR_OK);

    sdl_backend_cfg_t bk_cfg = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = W, .height = H};
    esp_video_render_backend_cfg_t disp = {
        .ops = esp_video_render_get_sdl_backend(),
        .cfg = &bk_cfg,
        .cfg_size = sizeof(bk_cfg),
    };
    assert(esp_video_render_set_display(render, &disp) == ESP_VIDEO_RENDER_ERR_OK);

    esp_video_render_stream_info_t stream_info = {
        .info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = W, .height = H, .fps = 30},
        .cached = false,
    };
    esp_video_render_stream_handle_t stream = NULL;
    assert(esp_video_render_stream_open(render, &stream_info, &stream) == ESP_VIDEO_RENDER_ERR_OK);
    printf("[test] Stream opened: %dx%d\n", W, H);

    esp_vui_overlay_handle_t overlay = NULL;
    assert(esp_video_render_stream_get_overlay(stream, &overlay) == ESP_VIDEO_RENDER_ERR_OK);
    printf("[test] Overlay obtained\n");

    // Create a larger container and image widget so it's visible
    const int img_w = 128, img_h = 128;
    esp_vui_container_handle_t container = NULL;
    esp_video_render_frame_info_t container_info = {
        .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
        .width = img_w,
        .height = img_h};
    esp_video_render_pos_t container_pos = {
        .x = (W - img_w) / 2,  // Center the container
        .y = (H - img_h) / 2};
    assert(esp_vui_container_create(overlay, &container_info, &container_pos, true, &container) == ESP_VIDEO_RENDER_ERR_OK);
    printf("[test] Container created at (%d, %d), size %dx%d\n",
           container_pos.x, container_pos.y, img_w, img_h);

    // Create a checker pattern image
    esp_video_render_img_t img = {
        .info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = img_w, .height = img_h},
        .size = img_w * img_h * 2,
    };
    img.data = (uint8_t *)malloc(img.size);
    assert(img.data);
    fill_checker_rgb565(img.data, img_w, img_h, 0xFFFF  /*white*/, 0xF800  /*red*/);
    printf("[test] Image created: %dx%d checker pattern (white/red)\n", img_w, img_h);

    esp_video_render_pos_t pos = {.x = 0, .y = 0};
    esp_vui_widget_t *w = esp_vui_image_widget_init(container, &img, &pos);
    assert(w);
    printf("[test] Image widget created\n");

    // One black video frame (so any non-zero pixels are from the image widget overlay).
    uint8_t *frame_buf = (uint8_t *)calloc(1, (size_t)(W * H * 2));
    assert(frame_buf);
    esp_video_render_frame_t frame = {
        .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
        .width = W,
        .height = H,
        .data = frame_buf,
        .size = (uint32_t)(W * H * 2),
    };
    printf("[test] Created black video frame\n");

    (void)esp_vui_container_notify_compose_changed(container, &w->rect, true);
    printf("[test] Notified container compose changed\n");

    assert(esp_video_render_stream_write(stream, &frame) == ESP_VIDEO_RENDER_ERR_OK);
    printf("[test] Wrote frame to stream\n");

    // Keep window visible for a few seconds so user can see it
    if (!env_driver || strcmp(env_driver, "dummy") != 0) {
        printf("[test] Keeping window open for 3 seconds...\n");
        sleep(3);
    }

    printf("[test] Cleaning up...\n");
    free(img.data);
    free(frame_buf);
    esp_video_render_stream_close(stream);
    esp_video_render_destroy(render);
    printf("[test] Test completed successfully!\n");
    return 0;
}

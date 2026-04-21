/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_video_render.h"
#include "esp_video_render_backend.h"
#include "esp_vui_overlay.h"
#include "esp_vui_container.h"
#include "esp_vui_widget.h"
#include "esp_vui_widget_default.h"
#include "sdl_backend.h"
#include "assets_path.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX  4096
#endif  /* PATH_MAX */

int main(void)
{
    // Only use dummy driver if explicitly set (for CI)
    // Otherwise, show the window for visual testing
    const char *env_driver = getenv("SDL_VIDEODRIVER");
    if (!env_driver) {
        printf("SDL window will be displayed (set SDL_VIDEODRIVER=dummy for headless)\n");
    }

    const int W = 640, H = 480;  // Larger window to see the text widget
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

    // Overlay -> container -> text widget
    esp_vui_overlay_handle_t overlay = NULL;
    assert(esp_video_render_stream_get_overlay(stream, &overlay) == ESP_VIDEO_RENDER_ERR_OK);
    printf("[test] Overlay obtained\n");

    // Create a larger container and text widget so it's visible (larger for emojis)
    const int text_w = 640, text_h = 200;
    esp_vui_container_handle_t container = NULL;
    esp_video_render_frame_info_t container_info = {
        .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
        .width = text_w,
        .height = text_h};
    esp_video_render_pos_t container_pos = {
        .x = (W - text_w) / 2,  // Center the container
        .y = (H - text_h) / 2};
    assert(esp_vui_container_create(overlay, &container_info, &container_pos, true, &container) == ESP_VIDEO_RENDER_ERR_OK);
    printf("[test] Container created at (%d, %d), size %dx%d\n",
           container_pos.x, container_pos.y, text_w, text_h);

    esp_video_render_pos_t text_pos = {.x = 0, .y = 0};
    esp_vui_widget_t *txt = esp_vui_text_widget_init(container, &container_info, &text_pos, text_w, text_h);
    assert(txt);
    printf("[test] Text widget created\n");

    // Ensure widget is visible (should be by default, but make sure)
    (void)esp_vui_widget_set_visible(txt, true);
    printf("[test] Text widget set to visible\n");

    // Set alignment: 0=left, 1=center, 2=right for horizontal; 0=top, 1=middle, 2=bottom for vertical
    (void)esp_vui_text_widget_set_align(txt, 0  /*left*/, 1  /*middle*/);

    // Load fonts FIRST before setting text (ensures metrics can be calculated)
    char font_path[PATH_MAX];
    if (get_assets_path("DejaVuSans.ttf", font_path, sizeof(font_path))) {
        printf("[test] ERROR: Failed to find DejaVuSans.ttf in assets folder\n");
        esp_video_render_stream_close(stream);
        esp_video_render_destroy(render);
        return 1;
    }
    int font_size = 32;  // Larger font size for visibility
    (void)esp_vui_text_widget_set_font(txt, font_path, font_size);
    printf("[test] Regular font loaded: %s, size %d\n", font_path, font_size);

    // Try to load emoji font (optional - test will work without it, emojis just won't show)
    const char *emoji_font_names[] = {
        "NotoColorEmojiBitmap-Subset.ttf",  // Bitmap color emoji (preferred)
        "NotoEmoji-Regular.ttf",            // Fallback monochrome emoji
    };
    int emoji_font_size = 40;  // Slightly larger for emojis
    bool emoji_font_loaded = false;
    for (int i = 0; i < sizeof(emoji_font_names) / sizeof(emoji_font_names[0]); i++) {
        if (get_assets_path(emoji_font_names[i], font_path, sizeof(font_path))) {
            continue;
        }
        esp_video_render_err_t ret = esp_vui_text_widget_set_emoji_font(txt, font_path, emoji_font_size);
        if (ret == ESP_VIDEO_RENDER_ERR_OK) {
            printf("[test] Emoji font loaded: %s, size %d\n", font_path, emoji_font_size);
            emoji_font_loaded = true;
            break;
        }
    }
    if (!emoji_font_loaded) {
        printf("[test] Warning: No emoji font found, emojis may not render\n");
    }

    // Set text with emojis mixed with regular text AFTER fonts are loaded
    // Test various emoji types: face, celebration, transport, symbol
    (void)esp_vui_text_widget_set_text(txt, "Hello 😊 World! 🎉 Test 🚀 ⭐");
    printf("[test] Text set to: \"Hello 😊 World! 🎉 Test 🚀 ⭐\"\n");

    esp_video_render_clr_t bg = {.r = 0, .g = 0, .b = 255};      // Blue background
    esp_video_render_clr_t fg = {.r = 255, .g = 255, .b = 255};  // White text
    (void)esp_vui_text_widget_set_bg_color(txt, &bg, true);
    (void)esp_vui_text_widget_set_text_color(txt, &fg);
    printf("[test] Colors set: blue background, white text\n");

    // Mark container dirty after all text/font setup to ensure redraw
    esp_video_render_rect_t dirty_region = {.x = 0, .y = 0, .width = text_w, .height = text_h};
    (void)esp_vui_container_notify_compose_changed(container, &dirty_region, true);
    printf("[test] Container marked dirty after text/font setup\n");

    // One black video frame (so any non-zero pixels are from the widget overlay).
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

    // Ensure container is marked dirty at least once.
    (void)esp_vui_container_notify_compose_changed(container, &dirty_region, true);
    printf("[test] Notified container compose changed\n");

    assert(esp_video_render_stream_write(stream, &frame) == ESP_VIDEO_RENDER_ERR_OK);
    printf("[test] Wrote frame to stream\n");

    // Keep window visible for a few seconds so user can see it
    if (!env_driver || strcmp(env_driver, "dummy") != 0) {
        printf("[test] Keeping window open for 3 seconds...\n");
        sleep(3);
    }

    printf("[test] Cleaning up...\n");
    free(frame_buf);
    esp_video_render_stream_close(stream);
    esp_video_render_destroy(render);
    printf("[test] Test completed successfully!\n");
    return 0;
}

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
#include "video_render_sys.h"
#include "../../test_apps/main/xiaozhi_panel_widgets.h"
#include "sdl_backend.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    bool with_cache = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--with-cache") == 0) {
            with_cache = true;
        }
    }

    // Only use dummy driver if explicitly set (for CI)
    const char *env_driver = getenv("SDL_VIDEODRIVER");
    if (!env_driver) {
        printf("SDL window will be displayed (set SDL_VIDEODRIVER=dummy for headless)\n");
    }

    const int W = 320, H = 240;  // Panel size
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
        .info = {.format = ESP_VIDEO_RENDER_FORMAT_RGB565, .width = W, .height = H},
    };
    esp_video_render_stream_handle_t stream = NULL;
    assert(esp_video_render_stream_open(render, &stream_info, &stream) == ESP_VIDEO_RENDER_ERR_OK);
    printf("[test] Stream opened: %dx%d\n", W, H);

    // Render async so that no use video stream write
    assert(esp_video_render_stream_render_async(stream) == ESP_VIDEO_RENDER_ERR_OK);

    // Get overlay
    esp_vui_overlay_handle_t overlay = NULL;
    assert(esp_video_render_stream_get_overlay(stream, &overlay) == ESP_VIDEO_RENDER_ERR_OK);
    printf("[test] Overlay obtained\n");

    // Create xiaozhi panel
    esp_video_render_frame_info_t panel_info = {
        .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
        .width = W,
        .height = H};
    esp_video_render_pos_t panel_pos = {.x = 0, .y = 0};
    void *panel = NULL;
    esp_video_render_err_t ret = xiaozhi_panel_create(overlay, &panel_info, &panel_pos, with_cache, &panel);
    assert(ret == ESP_VIDEO_RENDER_ERR_OK);
    printf("[test] Xiaozhi panel created with cache %d\n", with_cache);

    int frame_delay = 50;
    int frame_count = 10000 / frame_delay;
    for (int frame = 0; frame < frame_count; frame++) {
        if ((frame % 20 == 0)) {
            int wifi_strength = (frame / 20) % 4;
            int battery_level = 4 - ((frame / 20) % 5);
            const char *status_texts[] = {"Registered", "Connected", "Running"};
            const char *status_text = status_texts[(frame / 20) % 3];

            ret = xiaozhi_panel_set_status(panel, status_text,
                                           (wifi_strength_t)wifi_strength, (battery_level_t)battery_level);
            assert(ret == ESP_VIDEO_RENDER_ERR_OK);
        }

        if ((frame % 10 == 0)) {
            bool is_speaking = (frame / 10) % 2 == 1;
            ret = xiaozhi_panel_set_speaking(panel, is_speaking);
            assert(ret == ESP_VIDEO_RENDER_ERR_OK);
        }

        if (frame % 2 == 0) {
            static const char *const emojis[] = {
                // Keep this list aligned with `NotoColorEmojiBitmap-Subset.ttf` contents.
                // We only keep single-codepoint emojis (no ZWJ sequences, no VS16).
                "😀", "😁", "😂", "🤣", "😃", "😄", "😅", "😆", "😉", "😊", "😋", "😎", "😍", "😘", "😗", "😙", "😚", "🙂", "🤗", "🤩",
                "🤔", "🤨", "😐", "😑", "😶", "🙄", "😏", "😣", "😥", "😮", "🤐", "😯", "😪", "😫", "😴", "😌", "😛", "😜", "😝", "🤤",
                "😒", "😓", "😔", "😕", "🙃", "🫠", "🫢", "🫣", "🫡", "🤫", "🤭", "🫥", "😳", "🥺", "😦", "😧", "😨", "😰", "😱", "😵",
                "🤯", "😠", "😡", "🤬", "😷", "🤒", "🤕", "🤢", "🤮", "🤧", "🥵", "🥶", "🥴", "😇", "🤠", "🥳", "🥸", "😈", "👿", "💀",
                "👻", "👽", "👾", "🤖", "🎃", "😺", "😸", "😹", "😻", "😼", "😽", "🙀", "😿", "😾", "👋", "🤚", "✋", "🖖", "👌", "🤌",
                "🤏", "🤞", "🫰", "🤟", "🤘", "🤙", "👈", "👉", "👆", "🖕", "👇", "👍", "👎", "✊", "👊", "🤛", "🤜", "👏", "🙌", "👐",
            };
            const size_t emoji_count = sizeof(emojis) / sizeof(emojis[0]);
            const char *emoji = emojis[(frame / 2) % emoji_count];
            ret = xiaozhi_panel_set_emoji(panel, emoji);
            assert(ret == ESP_VIDEO_RENDER_ERR_OK);
        }

        if ((frame % 3 == 0)) {
            char new_lines[128];
            snprintf(new_lines, sizeof(new_lines), "Simulation\nFrame: %d\nStatus: OK", frame);
            ret = xiaozhi_panel_set_text_lines(panel, new_lines);
            assert(ret == ESP_VIDEO_RENDER_ERR_OK);
        }
        video_render_delay(frame_delay);
    }

    // Keep window visible for a few seconds so user can see it
    if (!env_driver || strcmp(env_driver, "dummy") != 0) {
        printf("[test] Keeping window open for 3 seconds...\n");
        sleep(3);
    }

    printf("[test] Cleaning up...\n");
    xiaozhi_panel_destroy(panel);
    esp_video_render_stream_close(stream);
    esp_video_render_destroy(render);
    printf("[test] Test completed successfully!\n");
    return 0;
}

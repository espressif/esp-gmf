/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_video_render.h"
#include "esp_video_render_backend.h"
#include "esp_video_render_dual_stream.h"
#include "assets_path.h"
#include "sdl_backend.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX  4096
#endif  /* PATH_MAX */

typedef struct {
    FILE    *fp;
    bool     eos;
    uint8_t  idx;
    uint8_t *last_data;
    int      last_size;
} eyes_info_t;

// Find MJPEG frame boundary (0xFF 0xD9)
static int get_frame_end(uint8_t *data, int size, bool eof)
{
    for (int i = 0; i < size - 1; i++) {
        if (data[i] == 0xFF && data[i + 1] == 0xD9) {
            // Check if next frame starts immediately (0xFF 0xD8)
            if (i + 3 < size) {
                if (data[i + 2] == 0xFF && data[i + 3] == 0xD8) {
                    return i + 2;  // End before next frame
                }
            }
            // If EOF and this is the last frame
            if (eof && i == size - 2) {
                return i + 2;
            }
        }
    }
    return -1;
}

// Read one MJPEG frame from file
static int read_mjpeg_frame(esp_video_render_dual_stream_handle_t eye, eyes_info_t *info, esp_video_render_frame_t *frame)
{
    frame->format = ESP_VIDEO_RENDER_FORMAT_MJPEG;
    frame->size = 0;

    int ret = esp_video_render_dual_stream_get_buffer(eye, info->idx, frame);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        printf("[test] Failed to get buffer for eye %d, ret=%d\n", info->idx, ret);
        return ret;
    }

    int filled = 0;
    // Copy leftover data from previous read
    if (info->last_size) {
        memmove(frame->data, info->last_data, info->last_size);
        filled = info->last_size;
        info->last_size = 0;
    } else if (info->eos) {
        // End of stream
        return 1;
    }

    // Read until we find a frame boundary
    int frame_end = -1;
    do {
        frame_end = get_frame_end(frame->data, filled, info->eos);
        if (frame_end > 0) {
            break;
        }

        int left = frame->size - filled;
        if (left <= 0) {
            printf("[test] Buffer too small for frame (filled=%d, size=%u)\n", filled, frame->size);
            return -1;
        }

        int rd = (int)fread(frame->data + filled, 1, left, info->fp);
        if (rd == 0 && ferror(info->fp)) {
            printf("[test] Read error\n");
            return -1;
        }
        filled += rd;
        info->eos = feof(info->fp);

        if (rd > 0) {
            frame_end = get_frame_end(frame->data, filled, info->eos);
        }
    } while (0);

    if (frame_end <= 0) {
        printf("[test] Frame boundary not found (filled=%d, eos=%d)\n", filled, info->eos);
        return -1;
    }

    frame->size = frame_end;
    info->last_data = frame->data + frame->size;
    info->last_size = filled - frame->size;

    return 0;
}

int main(void)
{
    // Only use dummy driver if explicitly set (for CI)
    const char *env_driver = getenv("SDL_VIDEODRIVER");
    if (!env_driver) {
        printf("SDL window will be displayed (set SDL_VIDEODRIVER=dummy for headless)\n");
    }
    const int W = 640, H = 480;  // Large enough for side-by-side display
    printf("[test] Initializing dual eyes test with %dx%d display...\n", W, H);
    // Create video render
    esp_video_render_cfg_t cfg = {.pool = NULL, .fps = 30};
    esp_video_render_handle_t render = NULL;
    assert(esp_video_render_create(&cfg, &render) == ESP_VIDEO_RENDER_ERR_OK);
    printf("[test] Video render created\n");
    // Setup SDL backend
    sdl_backend_cfg_t bk_cfg = {
        .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
        .width = W,
        .height = H};
    esp_video_render_backend_cfg_t disp = {
        .ops = esp_video_render_get_sdl_backend(),
        .cfg = &bk_cfg,
        .cfg_size = sizeof(bk_cfg),
    };
    assert(esp_video_render_set_display(render, &disp) == ESP_VIDEO_RENDER_ERR_OK);
    printf("[test] SDL backend set: %dx%d\n", W, H);
    // Set background color (dark gray)
    esp_video_render_clr_t bg_color = {.r = 0x40, .g = 0x40, .b = 0x40};
    esp_video_render_set_bg_color(render, &bg_color);
    // Open MJPEG files from assets folder
    char left_file[PATH_MAX], right_file[PATH_MAX];
    int ret = 0;
    ret |= get_assets_path("left.mjpeg", left_file, sizeof(left_file));
    ret |= get_assets_path("right.mjpeg", right_file, sizeof(right_file));
    if (ret) {
        printf("[test] ERROR: Failed to find asset files (left.mjpeg, right.mjpeg) in assets folder\n");
        esp_video_render_destroy(render);
        return 1;
    }
    eyes_info_t eye_info[2] = {};

    eye_info[0].idx = 0;
    eye_info[0].fp = fopen(left_file, "rb");
    eye_info[1].idx = 1;
    eye_info[1].fp = fopen(right_file, "rb");

    if (eye_info[0].fp == NULL || eye_info[1].fp == NULL) {
        printf("[test] ERROR: Cannot open MJPEG files:\n");
        printf("[test]   left.mjpeg: %s\n", eye_info[0].fp ? "OK" : "FAILED");
        printf("[test]   right.mjpeg: %s\n", eye_info[1].fp ? "OK" : "FAILED");
        printf("[test] Test skipped (files not found)\n");
        if (eye_info[0].fp) {
            fclose(eye_info[0].fp);
        }
        if (eye_info[1].fp) {
            fclose(eye_info[1].fp);
        }
        esp_video_render_destroy(render);
        return 0;  // Skip test if files not available
    }
    printf("[test] MJPEG files opened successfully\n");

    // Configure dual eyes
    esp_video_render_dual_stream_cfg_t eyes_cfg = {
        .render = {render, render},   // Both eyes use same render (side-by-side)
        .frame_count = 3,             // Buffer 3 frames per eye
        .max_frame_size = 64 * 1024,  // Max 64KB per MJPEG frame
        .fps = 30,                    // Target 30 FPS
        .render_async = true,         // Enable async rendering
    };

    esp_video_render_dual_stream_handle_t eyes = NULL;
    ret = esp_video_render_dual_stream_open(&eyes_cfg, &eyes);
    assert(ret == ESP_VIDEO_RENDER_ERR_OK);
    printf("[test] Dual eyes opened: frame_count=%u, max_frame_size=%u, fps=%u\n",
           eyes_cfg.frame_count, eyes_cfg.max_frame_size, eyes_cfg.fps);

    // Calculate display rectangles (side-by-side, scaled if needed)
    const int source_w = 240;
    const int source_h = 240;
    int eye_w = source_w;
    int eye_h = source_h;

    // Scale down if needed to fit side-by-side
    if (W < source_w * 2 || H < source_h) {
        eye_w = W / 2;
        eye_h = (eye_w * source_h) / source_w;  // Maintain aspect ratio
        if (eye_h > H) {
            eye_h = H;
            eye_w = (eye_h * source_w) / source_h;
        }
    }

    // Left eye: left half of screen
    esp_video_render_rect_t left_rect = {
        .x = 0,
        .y = (H - eye_h) / 2,  // Center vertically
        .width = (uint16_t)eye_w,
        .height = (uint16_t)eye_h,
    };
    ret = esp_video_render_dual_stream_set_display_rect(eyes, 0, &left_rect);
    assert(ret == ESP_VIDEO_RENDER_ERR_OK);
    // Right eye: right half of screen
    esp_video_render_rect_t right_rect = {
        .x = (uint16_t)eye_w,
        .y = (H - eye_h) / 2,  // Center vertically
        .width = (uint16_t)eye_w,
        .height = (uint16_t)eye_h,
    };
    ret = esp_video_render_dual_stream_set_display_rect(eyes, 1, &right_rect);
    assert(ret == ESP_VIDEO_RENDER_ERR_OK);
    // Play frames (test decode and scale)
    const int max_frames = 300;  // Limit to 300 frames for testing
    int frame_count = 0;
    int success_frames = 0;

    printf("[test] Starting to play frames (max %d frames)...\n", max_frames);

    while (frame_count < max_frames) {
        esp_video_render_frame_t frame[2] = {};

        // Read frames for both eyes
        for (int i = 0; i < 2; i++) {
            ret = read_mjpeg_frame(eyes, &eye_info[i], &frame[i]);
            if (ret != 0) {
                for (int j = 0; j <= i; j++) {
                    if (frame[j].data) {
                        esp_video_render_dual_stream_release_buffer(eyes, j, &frame[j]);
                    }
                }
                // End of stream or error
                if (ret > 0) {
                    for (int i = 0; i < 2; i++) {
                        fseek(eye_info[i].fp, 0, SEEK_SET);
                        eye_info[i].eos = false;
                        eye_info[i].last_size = 0;
                        eye_info[i].last_data = NULL;
                    }
                    i = -1;
                } else {
                    printf("[test] Error reading frame for eye %d: %d\n", i, ret);
                    goto done;
                }
            }
        }

        // Send both frames for rendering (this triggers decode and scale)
        ret = esp_video_render_dual_stream_send_buffer(eyes, &frame[0], &frame[1]);
        if (ret == ESP_VIDEO_RENDER_ERR_OK) {
            success_frames++;
        } else {
            printf("[test] Failed to send buffer at frame %d: %d\n", frame_count, ret);
            break;
        }
        frame_count++;
        // Small delay to allow rendering
        usleep(33000);  // ~30 FPS
    }

done:
    printf("[test] Playback complete: %d frames sent, %d successful\n", frame_count, success_frames);
    printf("[test] Cleaning up...\n");
    esp_video_render_dual_stream_close(eyes);
    for (int i = 0; i < 2; i++) {
        if (eye_info[i].fp) {
            fclose(eye_info[i].fp);
        }
    }
    esp_video_render_destroy(render);
    printf("[test] Test completed successfully!\n");
    return 0;
}

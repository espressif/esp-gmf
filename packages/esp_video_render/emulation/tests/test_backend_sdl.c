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
#include <math.h>
#include "esp_video_render_backend.h"
#include "esp_video_render_types.h"
#include "sdl_backend.h"

static inline uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

// Generate a colorful pattern image
static void generate_pattern_rgb565(uint8_t *buf, int w, int h)
{
    uint16_t *pixels = (uint16_t *)buf;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            // Create a colorful pattern:
            // - Checkerboard pattern in the corners
            // - Gradient from top to bottom
            // - Circular color wheel in the center

            int cx = w / 2;
            int cy = h / 2;
            int dx = x - cx;
            int dy = y - cy;
            int dist_sq = dx * dx + dy * dy;
            int max_dist_sq = (w * w + h * h) / 4;

            uint8_t r, g, b;

            // Center circular color wheel
            if (dist_sq < max_dist_sq / 2) {
                float angle = atan2f((float)dy, (float)dx);
                if (angle < 0) {
                    angle += 2.0f * 3.14159265f;
                }
                float radius = sqrtf((float)dist_sq / (float)max_dist_sq) * 2.0f;
                if (radius > 1.0f) {
                    radius = 1.0f;
                }

                // HSV to RGB conversion
                float h = angle / (2.0f * 3.14159265f) * 360.0f;
                float s = 1.0f;
                float v = 1.0f - radius * 0.5f;

                int i = (int)(h / 60.0f) % 6;
                float f = (h / 60.0f) - (int)(h / 60.0f);
                float p = v * (1.0f - s);
                float q = v * (1.0f - s * f);
                float t = v * (1.0f - s * (1.0f - f));

                switch (i) {
                    case 0:
                        r = (uint8_t)(v * 255);
                        g = (uint8_t)(t * 255);
                        b = (uint8_t)(p * 255);
                        break;
                    case 1:
                        r = (uint8_t)(q * 255);
                        g = (uint8_t)(v * 255);
                        b = (uint8_t)(p * 255);
                        break;
                    case 2:
                        r = (uint8_t)(p * 255);
                        g = (uint8_t)(v * 255);
                        b = (uint8_t)(t * 255);
                        break;
                    case 3:
                        r = (uint8_t)(p * 255);
                        g = (uint8_t)(q * 255);
                        b = (uint8_t)(v * 255);
                        break;
                    case 4:
                        r = (uint8_t)(t * 255);
                        g = (uint8_t)(p * 255);
                        b = (uint8_t)(v * 255);
                        break;
                    case 5:
                        r = (uint8_t)(v * 255);
                        g = (uint8_t)(p * 255);
                        b = (uint8_t)(q * 255);
                        break;
                    default:
                        r = 0;
                        g = 0;
                        b = 0;
                        break;
                }
            } else {
                // Checkerboard pattern in corners
                int checker = ((x / 8) + (y / 8)) % 2;
                if (checker) {
                    r = 255;
                    g = 255;
                    b = 255;  // White
                } else {
                    r = 0;
                    g = 0;
                    b = 0;  // Black
                }
            }

            // Add vertical gradient overlay
            float grad = (float)y / (float)h;
            r = (uint8_t)(r * (1.0f - grad * 0.3f));
            g = (uint8_t)(g * (1.0f - grad * 0.3f));
            b = (uint8_t)(b * (1.0f - grad * 0.3f));

            pixels[y * w + x] = rgb888_to_rgb565(r, g, b);
        }
    }
}

static void fill_rect_rgb565(uint8_t *buf, int w, int h, esp_video_render_rect_t r, uint16_t pix)
{
    (void)h;
    for (int y = r.y; y < r.y + r.height; y++) {
        uint16_t *row = (uint16_t *)(buf + (y * w * 2));
        for (int x = r.x; x < r.x + r.width; x++) {
            row[x] = pix;
        }
    }
}

int main(void)
{
    // Only use dummy driver if explicitly set (for CI)
    // Otherwise, show the window for visual testing
    const char *env_driver = getenv("SDL_VIDEODRIVER");
    if (!env_driver) {
        // Not set, so we'll show the window
        printf("SDL window will be displayed (set SDL_VIDEODRIVER=dummy for headless)\n");
    }

    const esp_video_render_backend_ops_t *ops = esp_video_render_get_sdl_backend();
    assert(ops);
    printf("[test] Initializing SDL backend...\n");

    // Use a larger window to better see the pattern
    sdl_backend_cfg_t cfg = {
        .format = ESP_VIDEO_RENDER_FORMAT_RGB565,
        .width = 320,
        .height = 240,
    };

    esp_video_render_backend_handle_t backend = NULL;
    assert(ops->init(&cfg, (int)sizeof(cfg), &backend) == ESP_VIDEO_RENDER_ERR_OK);
    printf("[test] Backend initialized: %dx%d\n", cfg.width, cfg.height);

    esp_video_render_fb_t fb = {0};
    assert(ops->get_fb(backend, &fb) == ESP_VIDEO_RENDER_ERR_OK);
    assert(fb.data);
    printf("[test] Got framebuffer: %dx%d, size=%u\n", fb.info.width, fb.info.height, fb.size);

    // Generate pattern image
    printf("[test] Generating pattern image...\n");
    generate_pattern_rgb565(fb.data, fb.info.width, fb.info.height);

    // Write full screen (no dirty rect = full update)
    printf("[test] Displaying pattern on screen...\n");
    esp_video_render_rect_t full_screen = {
        .x = 0,
        .y = 0,
        .width = (uint16_t)fb.info.width,
        .height = (uint16_t)fb.info.height};
    assert(ops->write_fb(backend, &fb, &full_screen, NULL) == ESP_VIDEO_RENDER_ERR_OK);

    printf("[test] Pattern displayed successfully!\n");

    // Keep window visible for a few seconds so user can see it
    if (!env_driver || strcmp(env_driver, "dummy") != 0) {
        printf("[test] Keeping window open for 3 seconds...\n");
        sleep(3);
    }

    printf("[test] Cleaning up...\n");
    assert(ops->deinit(backend) == ESP_VIDEO_RENDER_ERR_OK);
    printf("[test] Test completed successfully!\n");
    return 0;
}

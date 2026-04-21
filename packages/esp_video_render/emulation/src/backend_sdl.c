/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_video_render_backend.h"
#include "esp_video_render_types.h"
#include "esp_video_render_log.h"
#include "video_render_sys.h"
#include "esp_log.h"
#include "sdl_backend.h"

#include <SDL.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG  "VR_SDL_BACKEND"

typedef struct {
    SDL_Window   *win;
    SDL_Renderer *ren;
    SDL_Texture  *tex;

    esp_video_render_fb_t        fb;
    video_render_mutex_handle_t  lock;

    // SDL event loop thread
    pthread_t      event_thread;
    volatile bool  event_thread_running;   // Use volatile for thread-safe flag
    bool           sdl_initialized_by_us;  // Track if we initialized SDL
} sdl_backend_t;

static esp_video_render_err_t sdl_backend_deinit(esp_video_render_backend_handle_t backend);
static sdl_backend_t *s_last_backend;

static inline Uint32 sdl_pixfmt_from_render_fmt(esp_video_render_format_t fmt)
{
    // Keep it minimal: emulation currently targets RGB565.
    if (fmt == ESP_VIDEO_RENDER_FORMAT_RGB565 || fmt == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
        return SDL_PIXELFORMAT_RGB565;
    }
    if (fmt == ESP_VIDEO_RENDER_FORMAT_RGB888) {
        return SDL_PIXELFORMAT_RGB24;
    }
    if (fmt == ESP_VIDEO_RENDER_FORMAT_BGR888) {
        return SDL_PIXELFORMAT_BGR24;
    }
    return SDL_PIXELFORMAT_RGB565;
}

// SDL event loop thread - processes events continuously to keep window responsive
static void *sdl_event_thread(void *arg)
{
    volatile sdl_backend_t *bk = (sdl_backend_t *)arg;
    while (bk->event_thread_running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                // Force to close the program
                exit(0);
                break;
            }
            if (bk->event_thread_running == false) {
                break;
            }
        }
        SDL_Delay(10);
    }
    return NULL;
}

static esp_video_render_err_t sdl_backend_init(void *cfg, int cfg_size, esp_video_render_backend_handle_t *out)
{
    if (!cfg || cfg_size != (int)sizeof(sdl_backend_cfg_t) || !out) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    sdl_backend_cfg_t *c = (sdl_backend_cfg_t *)cfg;
    if (c->width <= 0 || c->height <= 0) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    bool sdl_was_init = (SDL_WasInit(SDL_INIT_VIDEO) != 0);
    if (!sdl_was_init) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            return ESP_VIDEO_RENDER_ERR_FAIL;
        }
    }

    sdl_backend_t *bk = (sdl_backend_t *)video_render_calloc(1, sizeof(*bk));
    do {
        if (!bk) {
            return ESP_VIDEO_RENDER_ERR_NO_MEM;
        }
        bk->sdl_initialized_by_us = !sdl_was_init;
        bk->lock = video_render_mutex_create();
        if (!bk->lock) {
            break;
        }
        // CI/headless friendly: if using dummy driver, create hidden window and use software renderer.
        const char *drv = getenv("SDL_VIDEODRIVER");
        const bool headless = (drv && strcmp(drv, "dummy") == 0);

        const Uint32 win_flags = headless ? SDL_WINDOW_HIDDEN : 0;
        bk->win = SDL_CreateWindow("esp_video_render emulation",
                                   SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   c->width, c->height, win_flags);
        if (!bk->win) {
            break;
        }

        /* Prefer software renderer to avoid GL/DRI allocations that show up as
         * valgrind "possibly lost" from vendor drivers. */
        const char *prefer_hw = getenv("ESP_VIDEO_RENDER_SDL_HW");
        if (prefer_hw && strcmp(prefer_hw, "1") == 0) {
            bk->ren = SDL_CreateRenderer(bk->win, -1, SDL_RENDERER_ACCELERATED);
            if (!bk->ren) {
                bk->ren = SDL_CreateRenderer(bk->win, -1, SDL_RENDERER_SOFTWARE);
            }
        } else {
            bk->ren = SDL_CreateRenderer(bk->win, -1, SDL_RENDERER_SOFTWARE);
            if (!bk->ren) {
                bk->ren = SDL_CreateRenderer(bk->win, -1, SDL_RENDERER_ACCELERATED);
            }
        }
        if (!bk->ren) {
            break;
        }
        bk->tex = SDL_CreateTexture(bk->ren, sdl_pixfmt_from_render_fmt(c->format),
                                    SDL_TEXTUREACCESS_STREAMING, c->width, c->height);
        if (!bk->tex) {
            break;
        }

        bk->fb.info.format = c->format;
        bk->fb.info.width = c->width;
        bk->fb.info.height = c->height;
        bk->fb.size = (uint32_t)(c->width * c->height * 2);  // RGB565
        bk->fb.data = (uint8_t *)video_render_malloc_align(bk->fb.size, 64);
        if (!bk->fb.data) {
            break;
        }
        memset(bk->fb.data, 0, bk->fb.size);
        // Start SDL event processing thread
        bk->event_thread_running = true;
        if (pthread_create(&bk->event_thread, NULL, sdl_event_thread, bk) != 0) {
            bk->event_thread_running = false;
            break;
        }
        s_last_backend = bk;
        *out = (esp_video_render_backend_handle_t)bk;
        return ESP_VIDEO_RENDER_ERR_OK;
    } while (0);
    if (bk) {
        sdl_backend_deinit((esp_video_render_backend_handle_t)bk);
    } else if (!sdl_was_init) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        SDL_Quit();
    }
    return ESP_VIDEO_RENDER_ERR_NO_MEM;
}

static bool sdl_backend_with_gram(esp_video_render_backend_handle_t backend)
{
    (void)backend;
    return true;
}

static esp_video_render_err_t sdl_backend_get_display_info(esp_video_render_backend_handle_t backend,
                                                           esp_video_render_disp_info_t *info)
{
    if (!backend || !info) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    sdl_backend_t *bk = (sdl_backend_t *)backend;
    info->format = bk->fb.info.format;
    info->width = bk->fb.info.width;
    info->height = bk->fb.info.height;
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t sdl_backend_get_fb(esp_video_render_backend_handle_t backend, esp_video_render_fb_t *fb)
{
    if (!backend || !fb) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    sdl_backend_t *bk = (sdl_backend_t *)backend;
    *fb = bk->fb;
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t sdl_backend_lock_fb(esp_video_render_backend_handle_t backend,
                                                  esp_video_render_fb_t *fb, bool lock)
{
    (void)fb;
    if (!backend) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    sdl_backend_t *bk = (sdl_backend_t *)backend;
    if (lock) {
        video_render_mutex_lock(bk->lock, VIDEO_RENDER_MAX_LOCK_TIME);
    } else {
        video_render_mutex_unlock(bk->lock);
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t sdl_backend_write_fb(esp_video_render_backend_handle_t backend,
                                                   esp_video_render_fb_t *fb,
                                                   const esp_video_render_rect_t *dirty_rect,
                                                   const esp_video_render_pos_t *pos)
{
    if (!backend || !fb || !fb->data) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    sdl_backend_t *bk = (sdl_backend_t *)backend;

    const int bpp = 2;  // RGB565
    const int dst_pitch = bk->fb.info.width * bpp;
    const int src_pitch = fb->info.width * bpp;
    esp_video_render_rect_t d = {0};
    if (dirty_rect && dirty_rect->width > 0 && dirty_rect->height > 0) {
        d = *dirty_rect;
    } else {
        d.x = 0;
        d.y = 0;
        d.width = fb->info.width;
        d.height = fb->info.height;
    }
    const int dst_x = pos ? (int)pos->x : (int)d.x;
    const int dst_y = pos ? (int)pos->y : (int)d.y;

    // Clamp to fb bounds
    if (d.x >= fb->info.width || d.y >= fb->info.height) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    if (d.x + d.width > fb->info.width) {
        d.width = fb->info.width - d.x;
    }
    if (d.y + d.height > fb->info.height) {
        d.height = fb->info.height - d.y;
    }

    // Clamp to display bounds
    if (dst_x >= bk->fb.info.width || dst_y >= bk->fb.info.height) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    if (dst_x + d.width > bk->fb.info.width) {
        d.width = bk->fb.info.width - dst_x;
    }
    if (dst_y + d.height > bk->fb.info.height) {
        d.height = bk->fb.info.height - dst_y;
    }

    // Copy into shadow display buffer
    if (fb->data != bk->fb.data) {
        const uint8_t *src0 = fb->data + (d.y * src_pitch) + (d.x * bpp);
        uint8_t *dst0 = bk->fb.data + (dst_y * dst_pitch) + (dst_x * bpp);
        for (int y = 0; y < (int)d.height; y++) {
            if (bk->fb.info.format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
                for (int x = 0; x < (int)d.width; x++) {
                    uint16_t src_native = __builtin_bswap16(src0[x]);
                    dst0[x] = src_native;
                }
            } else {
                memcpy(dst0, src0, (size_t)d.width * bpp);
            }
            src0 += src_pitch;
            dst0 += dst_pitch;
        }
    } else {
        if (bk->fb.info.format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
            uint16_t *line = (uint16_t *)fb->data;
            for (int y = 0; y < (int)d.height; y++) {
                for (int x = 0; x < (int)d.width; x++) {
                    uint16_t src_native = __builtin_bswap16(line[x]);
                    line[x] = src_native;
                }
                line += fb->info.width;
            }
        }
    }

    SDL_Rect r = {
        .x = dst_x,
        .y = dst_y,
        .w = d.width,
        .h = d.height,
    };

    const uint8_t *src = bk->fb.data + (r.y * dst_pitch) + (r.x * bpp);

    // SDL_UpdateTexture expects contiguous rows; we provide pitch for the full buffer.
    if (SDL_UpdateTexture(bk->tex, &r, src, dst_pitch) != 0) {
        ESP_LOGE(TAG, "SDL_UpdateTexture failed: %s\n", SDL_GetError());
        return ESP_VIDEO_RENDER_ERR_FAIL;
    }
    SDL_RenderClear(bk->ren);
    SDL_RenderCopy(bk->ren, bk->tex, NULL, NULL);
    SDL_RenderPresent(bk->ren);
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t sdl_backend_deinit(esp_video_render_backend_handle_t backend)
{
    if (!backend) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    sdl_backend_t *bk = (sdl_backend_t *)backend;
    // Stop event thread
    if (bk->event_thread_running) {
        bk->event_thread_running = false;
        pthread_join(bk->event_thread, NULL);
    }

    // Clean up SDL resources in proper order to avoid leaks
    // 1. Destroy texture first (releases texture resources)
    if (bk->tex) {
        SDL_DestroyTexture(bk->tex);
        bk->tex = NULL;
    }

    // 2. Flush renderer to ensure all pending operations complete
    if (bk->ren) {
        SDL_RenderClear(bk->ren);
        SDL_RenderPresent(bk->ren);
        // Small delay to let renderer finish any pending operations
        SDL_Delay(10);
        SDL_DestroyRenderer(bk->ren);
        bk->ren = NULL;
    }

    // 3. Destroy window (this releases window and OpenGL context if any)
    if (bk->win) {
        SDL_DestroyWindow(bk->win);
        bk->win = NULL;
    }

    // Free framebuffer
    if (bk->fb.data) {
        video_render_free(bk->fb.data);
        bk->fb.data = NULL;
    }
    // Destroy mutex
    if (bk->lock) {
        video_render_mutex_destroy(bk->lock);
    }

    // Quit SDL only if we initialized it
    // This ensures all SDL internal allocations (OpenGL contexts, texture buffers) are freed
    if (bk->sdl_initialized_by_us) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        SDL_Quit();
    }
    if (s_last_backend == bk) {
        s_last_backend = NULL;
    }

    video_render_free(bk);
    return ESP_VIDEO_RENDER_ERR_OK;
}

int sdl_backend_dump_jpg(const char *jpg_path)
{
    if (jpg_path == NULL || s_last_backend == NULL || s_last_backend->fb.data == NULL) {
        return -1;
    }
    sdl_backend_t *bk = s_last_backend;
    size_t frame_size = (size_t)bk->fb.info.width * bk->fb.info.height * 2;
    if (bk->fb.size < frame_size) {
        return -2;
    }
    uint8_t *frame = (uint8_t *)video_render_malloc(frame_size);
    if (frame == NULL) {
        return -2;
    }
    video_render_mutex_lock(bk->lock, VIDEO_RENDER_MAX_LOCK_TIME);
    memcpy(frame, bk->fb.data, frame_size);
    video_render_mutex_unlock(bk->lock);

    char cmd[1536];
    snprintf(cmd, sizeof(cmd),
             "gst-launch-1.0 -q -e "
             "fdsrc fd=0 blocksize=%zu ! "
             "rawvideoparse format=rgb16 width=%d height=%d framerate=1/1 ! "
             "videoconvert ! jpegenc ! "
             "filesink location=\"%s\"",
             frame_size, bk->fb.info.width, bk->fb.info.height, jpg_path);
    FILE *pipe = popen(cmd, "w");
    if (pipe == NULL) {
        video_render_free(frame);
        return -3;
    }
    size_t written = fwrite(frame, 1, frame_size, pipe);
    video_render_free(frame);
    int ret = pclose(pipe);
    if (written != frame_size) {
        return -4;
    }
    return ret == 0 ? 0 : -5;
}

const esp_video_render_backend_ops_t *esp_video_render_get_sdl_backend(void)
{
    static const esp_video_render_backend_ops_t ops = {
        .init = sdl_backend_init,
        .with_gram = sdl_backend_with_gram,
        .get_display_info = sdl_backend_get_display_info,
        .get_fb = sdl_backend_get_fb,
        .lock_fb = sdl_backend_lock_fb,
        .write_fb = sdl_backend_write_fb,
        .deinit = sdl_backend_deinit,
    };
    return &ops;
}

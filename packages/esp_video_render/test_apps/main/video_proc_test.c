/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_video_render_proc.h"
#include "video_render_sys.h"
#include "video_render_utils.h"

#ifdef __EMU__
#include <pthread.h>
#include <unistd.h>
#include "assets_path.h"
#else
#include "video_pattern.h"
#endif  /* __EMU__ */
#include "esp_log.h"

#define TAG             "PROC_WRAP_TEST"
#define PROC_TEST_W     32
#define PROC_TEST_H     32
#define PROC_TEST_FPS   30
#define PROC_OUT_W      64
#define PROC_OUT_H      64
#define PROC_IN_CACHE   4096
#define PROC_OUT_CACHE  (320 * 240 * 4)
#ifdef __EMU__
#define PROC_TEST_STACK  (64 * 1024)
#define PROC_TEST_POOL   ((esp_gmf_pool_handle_t)1)
#else
#define PROC_TEST_STACK  4096
#define PROC_BAR_COUNT   8
#endif  /* __EMU__ */

int create_video_render(uint8_t fps);
void *get_render_pool(void);
void destroy_video_render(void);
int video_render_proc_test_prepare_image(esp_video_render_img_t *image, esp_video_render_frame_info_t *out_info);

typedef struct {
    video_render_mutex_handle_t  lock;
    int                          count;
    esp_video_render_frame_t     last;
    uint8_t                     *copy;
    uint32_t                     copy_size;
} frame_sink_t;

typedef struct {
    frame_sink_t                   sink;
    uint8_t                       *buffer;
    uint32_t                       size;
    esp_video_render_frame_info_t  info;
    int                            acquire_count;
    int                            release_count;
} fb_sink_t;

static void sink_lock_init(frame_sink_t *sink)
{
    sink->lock = video_render_mutex_create();
}

static void sink_lock(frame_sink_t *sink)
{
    video_render_mutex_lock(sink->lock, VIDEO_RENDER_MAX_LOCK_TIME);
}

static void sink_unlock(frame_sink_t *sink)
{
    video_render_mutex_unlock(sink->lock);
}

static void sink_lock_deinit(frame_sink_t *sink)
{
    video_render_mutex_destroy(sink->lock);
    sink->lock = NULL;
}

static void wait_ms(int ms)
{
    video_render_delay(ms);
}

static void frame_sink_init(frame_sink_t *sink)
{
    memset(sink, 0, sizeof(*sink));
    sink_lock_init(sink);
}

static void frame_sink_reset(frame_sink_t *sink)
{
    sink_lock(sink);
    video_render_free(sink->copy);
    sink->copy = NULL;
    sink->copy_size = 0;
    sink->count = 0;
    memset(&sink->last, 0, sizeof(sink->last));
    sink_unlock(sink);
}

static void frame_sink_deinit(frame_sink_t *sink)
{
    frame_sink_reset(sink);
    sink_lock_deinit(sink);
}

static void frame_sink_store(frame_sink_t *sink, const esp_video_render_frame_t *frame)
{
    uint8_t *copy = (uint8_t *)video_render_malloc_align(frame->size, 64);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, frame->data, frame->size);
    sink_lock(sink);
    video_render_free(sink->copy);
    sink->copy = copy;
    sink->copy_size = frame->size;
    sink->last = *frame;
    sink->last.data = sink->copy;
    sink->count++;
    sink_unlock(sink);
}

static int on_frame_cb(esp_video_render_frame_t *frame, void *ctx)
{
    frame_sink_store((frame_sink_t *)ctx, frame);
    return 0;
}

static void fb_sink_init(fb_sink_t *sink, const esp_video_render_frame_info_t *info)
{
    memset(sink, 0, sizeof(*sink));
    frame_sink_init(&sink->sink);
    sink->info = *info;
    sink->size = video_render_get_image_size(info);
    sink->buffer = (uint8_t *)video_render_malloc_align(sink->size, 64);
}

static void fb_sink_deinit(fb_sink_t *sink)
{
    frame_sink_deinit(&sink->sink);
    video_render_free(sink->buffer);
    memset(sink, 0, sizeof(*sink));
}

static int acquire_fb_cb(esp_video_render_fb_t *fb, void *ctx)
{
    fb_sink_t *sink = (fb_sink_t *)ctx;
    if (sink->buffer == NULL) {
        return -1;
    }
    sink->acquire_count++;
    fb->data = sink->buffer;
    fb->size = sink->size;
    fb->info = sink->info;
    return 0;
}

static int release_fb_cb(esp_video_render_fb_t *fb, void *ctx)
{
    (void)fb;
    ((fb_sink_t *)ctx)->release_count++;
    return 0;
}

static int on_fb_cb(esp_video_render_fb_t *fb, void *ctx)
{
    esp_video_render_frame_t frame = {
        .format = fb->info.format,
        .width = fb->info.width,
        .height = fb->info.height,
        .data = fb->data,
        .size = fb->size,
    };
    frame_sink_store(&((fb_sink_t *)ctx)->sink, &frame);
    return 0;
}

static bool wait_for_frame_count(frame_sink_t *sink, int expected, int timeout_ms)
{
    while (timeout_ms >= 0) {
        bool ready;
        sink_lock(sink);
        ready = sink->count >= expected;
        sink_unlock(sink);
        if (ready) {
            return true;
        }
        wait_ms(1);
        timeout_ms--;
    }
    return false;
}

static int expect_sink_matches(const char *label, frame_sink_t *sink, frame_sink_t *golden)
{
    int ret = 0;
    sink_lock(sink);
    sink_lock(golden);
    do {
        if (sink->count != 1 || golden->count != 1) {
            printf("[proc][%s] expected single output sink=%d golden=%d\n", label, sink->count, golden->count);
            ret = -1;
            break;
        }
        if (sink->last.format != golden->last.format ||
            sink->last.width != golden->last.width ||
            sink->last.height != golden->last.height ||
            sink->copy_size != golden->copy_size) {
            printf("[proc][%s] frame info mismatch\n", label);
            ret = -1;
            break;
        }
        if (memcmp(sink->copy, golden->copy, golden->copy_size) != 0) {
            printf("[proc][%s] frame content mismatch size=%u\n", label, (unsigned)golden->copy_size);
            ret = -1;
            break;
        }
    } while (0);
    sink_unlock(golden);
    sink_unlock(sink);
    return ret;
}

static int expect_frame_matches_golden(const char *label, esp_video_render_frame_t *frame, frame_sink_t *golden)
{
    int ret = 0;
    sink_lock(golden);
    do {
        if (golden->count != 1) {
            printf("[proc][%s] golden not ready\n", label);
            ret = -1;
            break;
        }
        if (frame == NULL ||
            frame->format != golden->last.format ||
            frame->width != golden->last.width ||
            frame->height != golden->last.height ||
            frame->size != golden->copy_size) {
            printf("[proc][%s] acquired frame info mismatch\n", label);
            ret = -1;
            break;
        }
        if (memcmp(frame->data, golden->copy, golden->copy_size) != 0) {
            printf("[proc][%s] acquired frame content mismatch\n", label);
            ret = -1;
            break;
        }
    } while (0);
    sink_unlock(golden);
    return ret;
}

static esp_video_render_proc_in_worker_cfg_t make_in_worker_cfg(void)
{
    esp_video_render_proc_in_worker_cfg_t cfg = {
        .task_cfg = {
            .stack_size = PROC_TEST_STACK,
            .priority = 5,
            .core_id = 0,
        },
        .in_cache_size = PROC_IN_CACHE,
    };
    return cfg;
}

static esp_video_render_proc_out_worker_cfg_t make_out_worker_cfg(bool cache_only)
{
    esp_video_render_proc_out_worker_cfg_t cfg = {
        .task_cfg = {
            .stack_size = PROC_TEST_STACK,
            .priority = 5,
            .core_id = 0,
        },
        .out_cache_size = PROC_OUT_CACHE,
        .cache_only = cache_only,
    };
    return cfg;
}

static esp_gmf_pool_handle_t get_proc_test_pool(void)
{
#ifdef __EMU__
    return PROC_TEST_POOL;
#else
    return (esp_gmf_pool_handle_t)get_render_pool();
#endif  /* __EMU__ */
}

static int setup_proc_test_env(void)
{
#ifdef __EMU__
    return 0;
#else
    return create_video_render(PROC_TEST_FPS);
#endif  /* __EMU__ */
}

static void teardown_proc_test_env(void)
{
#ifndef __EMU__
    destroy_video_render();
#endif  /* __EMU__ */
}

static void free_test_image(esp_video_render_img_t *image)
{
    if (image) {
        free(image->data);
        memset(image, 0, sizeof(*image));
    }
}

static esp_video_render_frame_t make_input_frame(const esp_video_render_img_t *image)
{
    esp_video_render_frame_t frame = {
        .format = image->info.format,
        .width = image->info.width,
        .height = image->info.height,
        .data = image->data,
        .size = image->size,
    };
    return frame;
}

#ifndef __EMU__
int video_render_proc_test_prepare_image(esp_video_render_img_t *image, esp_video_render_frame_info_t *out_info)
{
    memset(image, 0, sizeof(*image));
    image->info.format = ESP_VIDEO_RENDER_FORMAT_MJPEG;
    image->info.width = PROC_TEST_W;
    image->info.height = PROC_TEST_H;
    image->info.fps = PROC_TEST_FPS;
    out_info->format = ESP_VIDEO_RENDER_FORMAT_RGB565;
    out_info->width = PROC_OUT_W;
    out_info->height = PROC_OUT_H;
    out_info->fps = PROC_TEST_FPS;
    if (gen_image(image, true, PROC_BAR_COUNT) == 0) {
        return 0;
    }
    free_test_image(image);
    image->info.format = ESP_VIDEO_RENDER_FORMAT_RGB565;
    image->info.width = PROC_TEST_W;
    image->info.height = PROC_TEST_H;
    image->info.fps = PROC_TEST_FPS;
    *out_info = image->info;
    return gen_image(image, true, PROC_BAR_COUNT);
}
#endif  /* __EMU__ */

static int prepare_test_image(esp_video_render_img_t *image,
                              esp_video_render_frame_info_t *out_info,
                              const char **label)
{
    *label = "generated-jpeg";
    return video_render_proc_test_prepare_image(image, out_info);
}

static int run_direct_callback(const esp_video_render_img_t *image,
                               const esp_video_render_frame_info_t *out_info,
                               frame_sink_t *sink)
{
    esp_video_render_proc_cfg_t cfg = {
        .pool = get_proc_test_pool(),
        .in_frame_info = image->info,
        .out_frame_info = *out_info,
        .out_cfg = {
            .fb_out = false,
            .on_frame = on_frame_cb,
            .out_ctx = sink,
        },
    };
    esp_video_render_proc_handle_t proc = NULL;
    esp_video_render_frame_t frame = make_input_frame(image);
    int ret = -1;
    if (esp_video_render_proc_open(&cfg, &proc) != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to open proc");
        return -1;
    }
    do {
        if (esp_video_render_proc_write(proc, &frame) != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to write frame");
            break;
        }
        if (!wait_for_frame_count(sink, 1, 3000)) {
            ESP_LOGE(TAG, "Failed to wait for frame");
            break;
        }
        ret = 0;
    } while (0);
    (void)esp_video_render_proc_close(proc);
    return ret;
}

static int run_direct_fb(const esp_video_render_img_t *image,
                         const esp_video_render_frame_info_t *out_info,
                         frame_sink_t *golden)
{
    fb_sink_t sink;
    fb_sink_init(&sink, out_info);
    int ret = -1;
    esp_video_render_proc_cfg_t cfg = {
        .pool = get_proc_test_pool(),
        .in_frame_info = image->info,
        .out_frame_info = *out_info,
        .out_cfg = {
            .fb_out = true,
            .fb_cfg = {
                .acquire_fb = acquire_fb_cb,
                .release_fb = release_fb_cb,
            },
            .on_fb = on_fb_cb,
            .out_ctx = &sink,
        },
    };
    esp_video_render_proc_handle_t proc = NULL;
    esp_video_render_frame_t frame = make_input_frame(image);
    if (sink.buffer == NULL) {
        fb_sink_deinit(&sink);
        ESP_LOGE(TAG, "Failed to initialize fb sink");
        return -1;
    }
    if (esp_video_render_proc_open(&cfg, &proc) != ESP_VIDEO_RENDER_ERR_OK) {
        fb_sink_deinit(&sink);
        ESP_LOGE(TAG, "Failed to open proc");
        return -1;
    }
    do {
        if (esp_video_render_proc_write(proc, &frame) != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to write frame");
            break;
        }
        if (!wait_for_frame_count(&sink.sink, 1, 3000)) {
            ESP_LOGE(TAG, "Failed to wait for frame");
            break;
        }
        if (expect_sink_matches("direct-fb", &sink.sink, golden) != 0) {
            ESP_LOGE(TAG, "Failed to expect sink matches");
            break;
        }
        if (sink.acquire_count != 1 || sink.release_count != 1) {
            ESP_LOGE(TAG, "Acquire and release count mismatch");
            break;
        }
        ret = 0;
    } while (0);
    (void)esp_video_render_proc_close(proc);
    fb_sink_deinit(&sink);
    return ret;
}

static int run_input_worker_callback(const esp_video_render_img_t *image,
                                     const esp_video_render_frame_info_t *out_info,
                                     frame_sink_t *golden)
{
    frame_sink_t sink;
    frame_sink_init(&sink);
    int ret = -1;
    esp_video_render_proc_cfg_t cfg = {
        .pool = get_proc_test_pool(),
        .in_frame_info = image->info,
        .out_frame_info = *out_info,
        .out_cfg = {
            .fb_out = false,
            .on_frame = on_frame_cb,
            .out_ctx = &sink,
        },
    };
    esp_video_render_proc_handle_t proc = NULL;
    esp_video_render_frame_t frame = make_input_frame(image);
    if (esp_video_render_proc_open(&cfg, &proc) != ESP_VIDEO_RENDER_ERR_OK) {
        frame_sink_deinit(&sink);
        ESP_LOGE(TAG, "Failed to open proc");
        return -1;
    }
    do {
        esp_video_render_proc_in_worker_cfg_t in_cfg = make_in_worker_cfg();
        if (esp_video_render_proc_set_in_worker(proc, &in_cfg) != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to set in worker");
            break;
        }
        if (esp_video_render_proc_write(proc, &frame) != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to write frame");
            break;
        }
        if (!wait_for_frame_count(&sink, 1, 3000)) {
            ESP_LOGE(TAG, "Failed to wait for frame");
            break;
        }
        ret = expect_sink_matches("in-worker-callback", &sink, golden);
    } while (0);
    (void)esp_video_render_proc_close(proc);
    frame_sink_deinit(&sink);
    return ret;
}

static int run_input_worker_fb(const esp_video_render_img_t *image,
                               const esp_video_render_frame_info_t *out_info,
                               frame_sink_t *golden)
{
    fb_sink_t sink;
    fb_sink_init(&sink, out_info);
    int ret = -1;
    esp_video_render_proc_cfg_t cfg = {
        .pool = get_proc_test_pool(),
        .in_frame_info = image->info,
        .out_frame_info = *out_info,
        .out_cfg = {
            .fb_out = true,
            .fb_cfg = {
                .acquire_fb = acquire_fb_cb,
                .release_fb = release_fb_cb,
            },
            .on_fb = on_fb_cb,
            .out_ctx = &sink,
        },
    };
    esp_video_render_proc_handle_t proc = NULL;
    esp_video_render_frame_t frame = make_input_frame(image);
    if (sink.buffer == NULL) {
        fb_sink_deinit(&sink);
        ESP_LOGE(TAG, "Failed to initialize fb sink");
        return -1;
    }
    if (esp_video_render_proc_open(&cfg, &proc) != ESP_VIDEO_RENDER_ERR_OK) {
        fb_sink_deinit(&sink);
        ESP_LOGE(TAG, "Failed to open proc");
        return -1;
    }
    do {
        esp_video_render_proc_in_worker_cfg_t in_cfg = make_in_worker_cfg();
        if (esp_video_render_proc_set_in_worker(proc, &in_cfg) != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to set in worker");
            break;
        }
        if (esp_video_render_proc_write(proc, &frame) != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to write frame");
            break;
        }
        if (!wait_for_frame_count(&sink.sink, 1, 3000)) {
            ESP_LOGE(TAG, "Failed to wait for frame");
            break;
        }
        if (expect_sink_matches("in-worker-fb", &sink.sink, golden) != 0) {
            ESP_LOGE(TAG, "Failed to expect sink matches");
            break;
        }
        if (sink.acquire_count != 1 || sink.release_count != 1) {
            ESP_LOGE(TAG, "Acquire and release count mismatch");
            break;
        }
        ret = 0;
    } while (0);
    (void)esp_video_render_proc_close(proc);
    fb_sink_deinit(&sink);
    return ret;
}

static int run_cache_only(const esp_video_render_img_t *image,
                          const esp_video_render_frame_info_t *out_info,
                          frame_sink_t *golden,
                          bool with_in_worker)
{
    frame_sink_t sink;
    frame_sink_init(&sink);
    int ret = -1;
    esp_video_render_proc_cfg_t cfg = {
        .pool = get_proc_test_pool(),
        .in_frame_info = image->info,
        .out_frame_info = *out_info,
        .out_cfg = {
            .fb_out = false,
            .on_frame = on_frame_cb,
            .out_ctx = &sink,
        },
    };
    esp_video_render_proc_handle_t proc = NULL;
    esp_video_render_frame_t frame = make_input_frame(image);
    if (esp_video_render_proc_open(&cfg, &proc) != ESP_VIDEO_RENDER_ERR_OK) {
        frame_sink_deinit(&sink);
        ESP_LOGE(TAG, "Failed to open proc");
        return -1;
    }
    do {
        if (with_in_worker) {
            esp_video_render_proc_in_worker_cfg_t in_cfg = make_in_worker_cfg();
            if (esp_video_render_proc_set_in_worker(proc, &in_cfg) != ESP_VIDEO_RENDER_ERR_OK) {
                ESP_LOGE(TAG, "Failed to set in worker");
                break;
            }
        }
        esp_video_render_proc_out_worker_cfg_t out_cfg = make_out_worker_cfg(true);
        if (esp_video_render_proc_set_out_worker(proc, &out_cfg) != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to set out worker");
            break;
        }
        if (esp_video_render_proc_write(proc, &frame) != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to write frame");
            break;
        }
        esp_video_render_frame_t *out = NULL;
        if (esp_video_render_proc_acquire_out(proc, &out) != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to acquire out");
            break;
        }
        ret = expect_frame_matches_golden(with_in_worker ? "cache-only-in" : "cache-only", out, golden);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to expect frame matches");
            (void)esp_video_render_proc_release_out(proc, out);
            break;
        }
        if (esp_video_render_proc_release_out(proc, out) != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to release out");
            ret = -1;
            break;
        }
        sink_lock(&sink);
        ret = (sink.count == 0) ? 0 : -1;
        sink_unlock(&sink);
    } while (0);
    (void)esp_video_render_proc_close(proc);
    frame_sink_deinit(&sink);
    return ret;
}

static int run_output_worker(const esp_video_render_img_t *image,
                             const esp_video_render_frame_info_t *out_info,
                             frame_sink_t *golden,
                             bool with_in_worker)
{
    frame_sink_t sink;
    frame_sink_init(&sink);
    int ret = -1;
    esp_video_render_proc_cfg_t cfg = {
        .pool = get_proc_test_pool(),
        .in_frame_info = image->info,
        .out_frame_info = *out_info,
        .out_cfg = {
            .fb_out = false,
            .on_frame = on_frame_cb,
            .out_ctx = &sink,
        },
    };
    esp_video_render_proc_handle_t proc = NULL;
    esp_video_render_frame_t frame = make_input_frame(image);
    if (esp_video_render_proc_open(&cfg, &proc) != ESP_VIDEO_RENDER_ERR_OK) {
        frame_sink_deinit(&sink);
        ESP_LOGE(TAG, "Failed to open proc");
        return -1;
    }
    do {
        if (with_in_worker) {
            esp_video_render_proc_in_worker_cfg_t in_cfg = make_in_worker_cfg();
            if (esp_video_render_proc_set_in_worker(proc, &in_cfg) != ESP_VIDEO_RENDER_ERR_OK) {
                ESP_LOGE(TAG, "Failed to set in worker");
                break;
            }
        }
        esp_video_render_proc_out_worker_cfg_t out_cfg = make_out_worker_cfg(false);
        if (esp_video_render_proc_set_out_worker(proc, &out_cfg) != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to set out worker");
            break;
        }
        if (esp_video_render_proc_write(proc, &frame) != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to write frame");
            break;
        }
        if (!wait_for_frame_count(&sink, 1, 3000)) {
            ESP_LOGE(TAG, "Failed to wait for frame");
            break;
        }
        ret = expect_sink_matches(with_in_worker ? "out-worker-in" : "out-worker", &sink, golden);
    } while (0);
    (void)esp_video_render_proc_close(proc);
    frame_sink_deinit(&sink);
    return ret;
}

static int run_fb_conflict_case(const esp_video_render_img_t *image,
                                const esp_video_render_frame_info_t *out_info)
{
    fb_sink_t sink;
    fb_sink_init(&sink, out_info);
    int ret = -1;
    esp_video_render_proc_cfg_t cfg = {
        .pool = get_proc_test_pool(),
        .in_frame_info = image->info,
        .out_frame_info = *out_info,
        .out_cfg = {
            .fb_out = true,
            .fb_cfg = {
                .acquire_fb = acquire_fb_cb,
                .release_fb = release_fb_cb,
            },
            .on_fb = on_fb_cb,
            .out_ctx = &sink,
        },
    };
    esp_video_render_proc_handle_t proc = NULL;
    if (sink.buffer == NULL) {
        fb_sink_deinit(&sink);
        ESP_LOGE(TAG, "Failed to initialize fb sink");
        return -1;
    }
    if (esp_video_render_proc_open(&cfg, &proc) != ESP_VIDEO_RENDER_ERR_OK) {
        fb_sink_deinit(&sink);
        ESP_LOGE(TAG, "Failed to open proc");
        return -1;
    }
    do {
        esp_video_render_proc_out_worker_cfg_t worker_cfg = make_out_worker_cfg(false);
        esp_video_render_proc_out_worker_cfg_t cache_cfg = make_out_worker_cfg(true);
        if (esp_video_render_proc_set_out_worker(proc, &worker_cfg) != ESP_VIDEO_RENDER_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to set out worker");
            break;
        }
        if (esp_video_render_proc_set_out_worker(proc, &cache_cfg) != ESP_VIDEO_RENDER_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to set out worker");
            break;
        }
        ret = 0;
    } while (0);
    (void)esp_video_render_proc_close(proc);
    fb_sink_deinit(&sink);
    return ret;
}

static int run_proc_matrix_with_image(const esp_video_render_img_t *image,
                                      const esp_video_render_frame_info_t *out_info,
                                      const char *label)
{
    int ret = -1;
    frame_sink_t golden;
    frame_sink_init(&golden);
    printf("[proc] input=%s fmt=0x%x %ux%u out=%ux%u size=%u\n",
           label ? label : "unknown",
           (unsigned)image->info.format,
           image->info.width, image->info.height,
           out_info->width, out_info->height,
           (unsigned)image->size);
    do {
        if (run_direct_callback(image, out_info, &golden) != 0) {
            ESP_LOGE(TAG, "direct callback baseline failed");
            break;
        }
        if (run_direct_fb(image, out_info, &golden) != 0) {
            ESP_LOGE(TAG, "direct fb case failed");
            break;
        }
        if (run_input_worker_callback(image, out_info, &golden) != 0) {
            ESP_LOGE(TAG, "input worker callback case failed");
            break;
        }
        if (run_input_worker_fb(image, out_info, &golden) != 0) {
            ESP_LOGE(TAG, "input worker fb case failed");
            break;
        }
        if (run_cache_only(image, out_info, &golden, false) != 0) {
            ESP_LOGE(TAG, "cache only case failed");
            break;
        }
        if (run_cache_only(image, out_info, &golden, true) != 0) {
            ESP_LOGE(TAG, "cache only + input worker case failed");
            break;
        }
        if (run_output_worker(image, out_info, &golden, false) != 0) {
            ESP_LOGE(TAG, "output worker case failed");
            break;
        }
        if (run_output_worker(image, out_info, &golden, true) != 0) {
            ESP_LOGE(TAG, "input + output worker case failed");
            break;
        }
        if (run_fb_conflict_case(image, out_info) != 0) {
            ESP_LOGE(TAG, "fb conflict validation failed");
            break;
        }
        ret = 0;
    } while (0);
    frame_sink_deinit(&golden);
    return ret;
}

int video_render_proc_wrapper_test(int count)
{
    (void)count;
    int ret = -1;
    const char *label = NULL;
    esp_video_render_img_t image = {0};
    esp_video_render_frame_info_t out_info = {0};
    do {
        if (setup_proc_test_env() != 0) {
            ESP_LOGE(TAG, "failed to setup env");
            break;
        }
        if (prepare_test_image(&image, &out_info, &label) != 0) {
            ESP_LOGE(TAG, "failed to prepare image input");
            break;
        }
        ret = run_proc_matrix_with_image(&image, &out_info, label);
    } while (0);
    free_test_image(&image);
    teardown_proc_test_env();
    return ret;
}

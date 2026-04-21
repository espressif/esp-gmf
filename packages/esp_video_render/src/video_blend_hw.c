/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>
#include "esp_log.h"
#ifndef __linux__
#include "soc/soc_caps.h"
#endif  /* __linux__ */
#include "video_render_sys.h"
#include "video_render_utils.h"
#include "video_blend_hw.h"

#if CONFIG_SOC_PPA_SUPPORTED
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_private/esp_cache_private.h"

#define TAG  "VIDEO_BLEND_HW"

#define HW_BLEND_MIN_PIXELS    (100 * 100)
#define TRANS_COLOR_TOLERANCE  8

#define HW_CTX_READY_BIT       (1U << 0)
#define HW_CTX_CREATING_BIT    (1U << 1)
#define HW_BLEND_READY_BIT     (1U << 2)
#define HW_BLEND_CREATING_BIT  (1U << 3)
#define HW_FILL_READY_BIT      (1U << 4)
#define HW_FILL_CREATING_BIT   (1U << 5)
#define HW_SRM_READY_BIT       (1U << 6)
#define HW_SRM_CREATING_BIT    (1U << 7)
#define HW_ANY_CREATING_BITS   (HW_CTX_CREATING_BIT | HW_BLEND_CREATING_BIT | HW_FILL_CREATING_BIT | HW_SRM_CREATING_BIT)

struct video_render_blend_hw_ctx {
    ppa_client_handle_t  blend;
    ppa_client_handle_t  fill;
    ppa_client_handle_t  srm;
};

static video_render_blend_hw_ctx_t s_hw_ctx_storage;
static atomic_uint s_hw_flags = 0;
static atomic_int s_hw_ref_count = 0;
static atomic_uint s_hw_active_ops = 0;

void ppa_srm_lock(void);
void ppa_srm_unlock(void);

static inline video_render_blend_hw_ctx_t *hw_ctx_ptr(void)
{
    return &s_hw_ctx_storage;
}

static inline bool hw_ctx_ready(void)
{
    return (atomic_load(&s_hw_flags) & HW_CTX_READY_BIT) != 0;
}

static inline void hw_ref_inc(void)
{
    atomic_fetch_add(&s_hw_ref_count, 1);
}

static bool hw_ref_try_dec(void)
{
    int refs = atomic_load(&s_hw_ref_count);
    while (refs > 0) {
        if (atomic_compare_exchange_weak(&s_hw_ref_count, &refs, refs - 1)) {
            return true;
        }
    }
    return false;
}

static inline void hw_op_enter(void)
{
    atomic_fetch_add(&s_hw_active_ops, 1);
}

static void try_release_hw_ctx(void);

static inline void hw_op_leave(void)
{
    atomic_fetch_sub(&s_hw_active_ops, 1);
    try_release_hw_ctx();
}

static esp_video_render_err_t ensure_hw_ctx_ready(video_render_blend_hw_ctx_t **ctx)
{
    if (ctx == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    for (;;) {
        uint32_t flags = atomic_load(&s_hw_flags);
        if (flags & HW_CTX_READY_BIT) {
            *ctx = hw_ctx_ptr();
            return ESP_VIDEO_RENDER_ERR_OK;
        }
        if (flags & HW_CTX_CREATING_BIT) {
            continue;
        }
        uint32_t expected = flags;
        if (!atomic_compare_exchange_weak(&s_hw_flags, &expected, flags | HW_CTX_CREATING_BIT)) {
            continue;
        }

        memset(&s_hw_ctx_storage, 0, sizeof(s_hw_ctx_storage));
        atomic_store(&s_hw_flags, HW_CTX_READY_BIT);
        *ctx = hw_ctx_ptr();
        return ESP_VIDEO_RENDER_ERR_OK;
    }
}

static ppa_client_handle_t *get_client_slot(video_render_blend_hw_ctx_t *ctx, uint32_t ready_bit)
{
    if (ready_bit == HW_BLEND_READY_BIT) {
        return &ctx->blend;
    }
    if (ready_bit == HW_FILL_READY_BIT) {
        return &ctx->fill;
    }
    return &ctx->srm;
}

static esp_video_render_err_t ensure_hw_client(video_render_blend_hw_ctx_t *ctx,
                                               uint32_t ready_bit,
                                               uint32_t creating_bit,
                                               uint32_t oper_type)
{
    if (ctx == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    for (;;) {
        uint32_t flags = atomic_load(&s_hw_flags);
        if (flags & ready_bit) {
            return ESP_VIDEO_RENDER_ERR_OK;
        }
        if (flags & creating_bit) {
            video_render_delay(1);
            continue;
        }
        uint32_t expected = flags;
        if (!atomic_compare_exchange_weak(&s_hw_flags, &expected, flags | creating_bit)) {
            video_render_delay(1);
            continue;
        }

        ppa_client_config_t cfg = {
            .oper_type = oper_type,
            .max_pending_trans_num = 1,
        };
        ppa_client_handle_t client = NULL;
        if (ppa_register_client(&cfg, &client) != ESP_OK) {
            atomic_fetch_and(&s_hw_flags, ~creating_bit);
            return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
        }
        ppa_client_handle_t *slot = get_client_slot(ctx, ready_bit);
        *slot = client;
        atomic_fetch_or(&s_hw_flags, ready_bit);
        atomic_fetch_and(&s_hw_flags, ~creating_bit);
        return ESP_VIDEO_RENDER_ERR_OK;
    }
}

static esp_video_render_err_t get_or_create_hw_ctx(video_render_blend_hw_ctx_t **ctx, bool add_ref)
{
    esp_video_render_err_t ret = ensure_hw_ctx_ready(ctx);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        return ret;
    }
    if (add_ref) {
        hw_ref_inc();
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t resolve_hw_ctx(video_render_blend_hw_ctx_t *ctx, video_render_blend_hw_ctx_t **use_ctx)
{
    if (use_ctx == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (ctx != NULL) {
        if (ctx != hw_ctx_ptr() || !hw_ctx_ready()) {
            return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
        }
        *use_ctx = ctx;
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    return get_or_create_hw_ctx(use_ctx, false);
}

static void try_release_hw_ctx(void)
{
    if (atomic_load(&s_hw_ref_count) != 0 || atomic_load(&s_hw_active_ops) != 0) {
        return;
    }
    for (;;) {
        uint32_t flags = atomic_load(&s_hw_flags);
        if ((flags & HW_CTX_READY_BIT) == 0 || (flags & HW_ANY_CREATING_BITS) != 0) {
            return;
        }
        uint32_t desired = flags | HW_CTX_CREATING_BIT;
        if (atomic_compare_exchange_weak(&s_hw_flags, &flags, desired)) {
            break;
        }
    }

    if (atomic_load(&s_hw_ref_count) != 0 || atomic_load(&s_hw_active_ops) != 0) {
        atomic_fetch_and(&s_hw_flags, ~HW_CTX_CREATING_BIT);
        return;
    }

    uint32_t flags = atomic_load(&s_hw_flags);
    if ((flags & HW_SRM_READY_BIT) && s_hw_ctx_storage.srm) {
        ppa_unregister_client(s_hw_ctx_storage.srm);
        s_hw_ctx_storage.srm = NULL;
    }
    if ((flags & HW_FILL_READY_BIT) && s_hw_ctx_storage.fill) {
        ppa_unregister_client(s_hw_ctx_storage.fill);
        s_hw_ctx_storage.fill = NULL;
    }
    if ((flags & HW_BLEND_READY_BIT) && s_hw_ctx_storage.blend) {
        ppa_unregister_client(s_hw_ctx_storage.blend);
        s_hw_ctx_storage.blend = NULL;
    }
    memset(&s_hw_ctx_storage, 0, sizeof(s_hw_ctx_storage));
    atomic_store(&s_hw_flags, 0);
}

static bool rect_valid(const esp_video_render_fb_t *fb, const esp_video_render_rect_t *rect)
{
    if (!fb || !rect || !fb->data) {
        return false;
    }
    if (rect->width == 0 || rect->height == 0) {
        return false;
    }
    if ((uint32_t)rect->x + rect->width > fb->info.width || (uint32_t)rect->y + rect->height > fb->info.height) {
        return false;
    }
    return true;
}

static bool get_rgb565_hw_cfg(esp_video_render_format_t format, bool *byte_swap)
{
    if (byte_swap == NULL) {
        return false;
    }
    if (format == ESP_VIDEO_RENDER_FORMAT_RGB565) {
        *byte_swap = false;
        return true;
    }
    return false;
}

static size_t get_buf_align(uint8_t *buf)
{
    size_t align = 64;
    uint32_t caps = esp_ptr_external_ram(buf) ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
    if (esp_cache_get_alignment(caps, &align) != ESP_OK || align == 0) {
        align = 64;
    }
    return align;
}

static bool out_buf_ready(const esp_video_render_fb_t *fb)
{
    if (fb == NULL || fb->data == NULL) {
        return false;
    }
    if (fb->size == 0) {
        return false;
    }
    return true;
}

static size_t get_aligned_fb_size(const esp_video_render_fb_t *fb)
{
    size_t align = get_buf_align(fb->data);
    return ALIGN_UP(fb->size, align);
}

static esp_err_t cache_sync_rect(const esp_video_render_fb_t *fb, const esp_video_render_rect_t *rect, int flag)
{
    if (!rect_valid(fb, rect)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!esp_ptr_external_ram(fb->data)) {
        return ESP_OK;
    }
    uint8_t pixel_bytes = video_render_get_pixel_bits(fb->info.format) / 8;
    if (pixel_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t stride = (size_t)fb->info.width * pixel_bytes;
    size_t row_size = (size_t)rect->width * pixel_bytes;
    size_t align = get_buf_align(fb->data);
    uint8_t *row = fb->data + (size_t)rect->y * stride + (size_t)rect->x * pixel_bytes;
    for (uint16_t y = 0; y < rect->height; y++) {
        uintptr_t addr = (uintptr_t)row;
        uintptr_t aligned_addr = addr & ~(uintptr_t)(align - 1);
        size_t aligned_size = ALIGN_UP((size_t)(addr - aligned_addr) + row_size, align);
        esp_err_t ret = esp_cache_msync((void *)aligned_addr, aligned_size, flag);
        if (ret != ESP_OK) {
            return ret;
        }
        row += stride;
    }
    return ESP_OK;
}

static inline esp_video_render_err_t hw_err_from_esp(esp_err_t ret)
{
    if (ret == ESP_OK) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    if (ret == ESP_ERR_INVALID_ARG) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
}

static inline uint8_t clamp_u8(int v)
{
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return (uint8_t)v;
}

bool video_render_blend_hw_can_accel(const esp_video_render_fb_t *dst,
                                     const esp_video_render_fb_t *src,
                                     const esp_video_render_rect_t *dst_rect,
                                     const esp_video_render_rect_t *src_rect)
{
    bool byte_swap = false;
    if (!dst || !src || !dst_rect || !src_rect) {
        return false;
    }
    if (dst->info.format != src->info.format) {
        return false;
    }
    if (!get_rgb565_hw_cfg(dst->info.format, &byte_swap)) {
        return false;
    }
    if (!rect_valid(dst, dst_rect) || !rect_valid(src, src_rect)) {
        return false;
    }
    if (dst_rect->width != src_rect->width || dst_rect->height != src_rect->height) {
        return false;
    }
    if (!out_buf_ready(dst) || !out_buf_ready(src)) {
        return false;
    }
    return true;
}

bool video_render_blend_hw_can_fill(const esp_video_render_fb_t *dst, const esp_video_render_rect_t *dst_rect)
{
    bool byte_swap = false;
    if (!dst || !dst_rect) {
        return false;
    }
    if (!get_rgb565_hw_cfg(dst->info.format, &byte_swap)) {
        return false;
    }
    if (!rect_valid(dst, dst_rect)) {
        return false;
    }
    if (!out_buf_ready(dst)) {
        return false;
    }
    return true;
}

esp_video_render_err_t video_render_blend_hw_open(video_render_blend_hw_ctx_t **ctx)
{
    if (ctx == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    return get_or_create_hw_ctx(ctx, true);
}

esp_video_render_err_t video_render_blend_hw_close(video_render_blend_hw_ctx_t *ctx)
{
    if (ctx == NULL || ctx != hw_ctx_ptr()) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    (void)hw_ref_try_dec();
    try_release_hw_ctx();
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_blend_hw_process(video_render_blend_hw_ctx_t *ctx,
                                                     esp_video_render_fb_t *dst,
                                                     esp_video_render_fb_t *src,
                                                     esp_video_render_rect_t *dst_rect,
                                                     esp_video_render_rect_t *src_rect,
                                                     uint8_t global_alpha)
{
    esp_video_render_err_t ret_v = ESP_VIDEO_RENDER_ERR_OK;
    bool byte_swap = false;
    video_render_blend_hw_ctx_t *use_ctx = NULL;

    hw_op_enter();
    ret_v = resolve_hw_ctx(ctx, &use_ctx);
    if (ret_v != ESP_VIDEO_RENDER_ERR_OK) {
        goto done;
    }
    ret_v = ensure_hw_client(use_ctx, HW_BLEND_READY_BIT, HW_BLEND_CREATING_BIT, PPA_OPERATION_BLEND);
    if (ret_v != ESP_VIDEO_RENDER_ERR_OK) {
        goto done;
    }
    if (!video_render_blend_hw_can_accel(dst, src, dst_rect, src_rect)) {
        ret_v = ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
        goto done;
    }
    if (!get_rgb565_hw_cfg(dst->info.format, &byte_swap)) {
        ret_v = ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
        goto done;
    }
    if (global_alpha == 0) {
        goto done;
    }

    esp_err_t ret = cache_sync_rect(dst, dst_rect, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (ret != ESP_OK) {
        ret_v = hw_err_from_esp(ret);
        goto done;
    }
    ret = cache_sync_rect(src, src_rect, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (ret != ESP_OK) {
        ret_v = hw_err_from_esp(ret);
        goto done;
    }

    ppa_blend_oper_config_t cfg = {
        .in_bg = {
            .buffer = dst->data,
            .pic_w = dst->info.width,
            .pic_h = dst->info.height,
            .block_w = dst_rect->width,
            .block_h = dst_rect->height,
            .block_offset_x = dst_rect->x,
            .block_offset_y = dst_rect->y,
            .blend_cm = PPA_BLEND_COLOR_MODE_RGB565,
        },
        .in_fg = {
            .buffer = src->data,
            .pic_w = src->info.width,
            .pic_h = src->info.height,
            .block_w = src_rect->width,
            .block_h = src_rect->height,
            .block_offset_x = src_rect->x,
            .block_offset_y = src_rect->y,
            .blend_cm = PPA_BLEND_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = dst->data,
            .buffer_size = get_aligned_fb_size(dst),
            .pic_w = dst->info.width,
            .pic_h = dst->info.height,
            .block_offset_x = dst_rect->x,
            .block_offset_y = dst_rect->y,
            .blend_cm = PPA_BLEND_COLOR_MODE_RGB565,
        },
        .bg_byte_swap = byte_swap,
        .fg_byte_swap = byte_swap,
        .bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE,
        .bg_alpha_fix_val = 255 - global_alpha,
        .fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE,
        .fg_alpha_fix_val = global_alpha,
        .bg_ck_en = false,
        .fg_ck_en = false,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    ppa_srm_lock();
    ret = ppa_do_blend(use_ctx->blend, &cfg);
    ppa_srm_unlock();
    if (ret != ESP_OK) {
        ret_v = hw_err_from_esp(ret);
        goto done;
    }
    ret = cache_sync_rect(dst, dst_rect, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    ret_v = hw_err_from_esp(ret);
done:
    hw_op_leave();
    return ret_v;
}

esp_video_render_err_t video_render_blend_hw_transparent_color(video_render_blend_hw_ctx_t *ctx,
                                                               esp_video_render_fb_t *dst,
                                                               esp_video_render_fb_t *src,
                                                               esp_video_render_rect_t *dst_rect,
                                                               esp_video_render_rect_t *src_rect,
                                                               esp_video_render_clr_t *trans_color)
{
    esp_video_render_err_t ret_v = ESP_VIDEO_RENDER_ERR_OK;
    bool byte_swap = false;
    video_render_blend_hw_ctx_t *use_ctx = NULL;

    if (trans_color == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    hw_op_enter();
    ret_v = resolve_hw_ctx(ctx, &use_ctx);
    if (ret_v != ESP_VIDEO_RENDER_ERR_OK) {
        goto done;
    }
    ret_v = ensure_hw_client(use_ctx, HW_BLEND_READY_BIT, HW_BLEND_CREATING_BIT, PPA_OPERATION_BLEND);
    if (ret_v != ESP_VIDEO_RENDER_ERR_OK) {
        goto done;
    }
    if (!video_render_blend_hw_can_accel(dst, src, dst_rect, src_rect)) {
        ret_v = ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
        goto done;
    }
    if (!get_rgb565_hw_cfg(dst->info.format, &byte_swap)) {
        ret_v = ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
        goto done;
    }

    esp_err_t ret = cache_sync_rect(dst, dst_rect, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (ret != ESP_OK) {
        ret_v = hw_err_from_esp(ret);
        goto done;
    }
    ret = cache_sync_rect(src, src_rect, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (ret != ESP_OK) {
        ret_v = hw_err_from_esp(ret);
        goto done;
    }

    ppa_blend_oper_config_t cfg = {
        .in_bg = {
            .buffer = dst->data,
            .pic_w = dst->info.width,
            .pic_h = dst->info.height,
            .block_w = dst_rect->width,
            .block_h = dst_rect->height,
            .block_offset_x = dst_rect->x,
            .block_offset_y = dst_rect->y,
            .blend_cm = PPA_BLEND_COLOR_MODE_RGB565,
        },
        .in_fg = {
            .buffer = src->data,
            .pic_w = src->info.width,
            .pic_h = src->info.height,
            .block_w = src_rect->width,
            .block_h = src_rect->height,
            .block_offset_x = src_rect->x,
            .block_offset_y = src_rect->y,
            .blend_cm = PPA_BLEND_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = dst->data,
            .buffer_size = get_aligned_fb_size(dst),
            .pic_w = dst->info.width,
            .pic_h = dst->info.height,
            .block_offset_x = dst_rect->x,
            .block_offset_y = dst_rect->y,
            .blend_cm = PPA_BLEND_COLOR_MODE_RGB565,
        },
        .bg_byte_swap = byte_swap,
        .fg_byte_swap = byte_swap,
        .bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE,
        .bg_alpha_fix_val = 0,
        .fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE,
        .fg_alpha_fix_val = 255,
        .bg_ck_en = false,
        .fg_ck_en = true,
        .fg_ck_rgb_low_thres = {
            .r = clamp_u8((int)trans_color->r - TRANS_COLOR_TOLERANCE),
            .g = clamp_u8((int)trans_color->g - TRANS_COLOR_TOLERANCE),
            .b = clamp_u8((int)trans_color->b - TRANS_COLOR_TOLERANCE),
        },
        .fg_ck_rgb_high_thres = {
            .r = clamp_u8((int)trans_color->r + TRANS_COLOR_TOLERANCE),
            .g = clamp_u8((int)trans_color->g + TRANS_COLOR_TOLERANCE),
            .b = clamp_u8((int)trans_color->b + TRANS_COLOR_TOLERANCE),
        },
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    ppa_srm_lock();
    ret = ppa_do_blend(use_ctx->blend, &cfg);
    ppa_srm_unlock();
    if (ret != ESP_OK) {
        ret_v = hw_err_from_esp(ret);
        goto done;
    }
    ret = cache_sync_rect(dst, dst_rect, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    ret_v = hw_err_from_esp(ret);
done:
    hw_op_leave();
    return ret_v;
}

esp_video_render_err_t video_render_blend_hw_fill(video_render_blend_hw_ctx_t *ctx,
                                                  esp_video_render_fb_t *dst,
                                                  esp_video_render_rect_t *dst_rect,
                                                  esp_video_render_clr_t *color)
{
    esp_video_render_err_t ret_v = ESP_VIDEO_RENDER_ERR_OK;
    video_render_blend_hw_ctx_t *use_ctx = NULL;

    if (color == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    hw_op_enter();
    ret_v = resolve_hw_ctx(ctx, &use_ctx);
    if (ret_v != ESP_VIDEO_RENDER_ERR_OK) {
        goto done;
    }
    ret_v = ensure_hw_client(use_ctx, HW_FILL_READY_BIT, HW_FILL_CREATING_BIT, PPA_OPERATION_FILL);
    if (ret_v != ESP_VIDEO_RENDER_ERR_OK) {
        goto done;
    }
    if (!video_render_blend_hw_can_fill(dst, dst_rect)) {
        ret_v = ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
        goto done;
    }
    if ((uint32_t)dst_rect->width * dst_rect->height < HW_BLEND_MIN_PIXELS) {
        ret_v = ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
        goto done;
    }

    uint32_t argb = ((uint32_t)0xFF << 24) | ((uint32_t)color->r << 16) | ((uint32_t)color->g << 8) | ((uint32_t)color->b);
    ppa_fill_oper_config_t cfg = {
        .out = {
            .buffer = dst->data,
            .buffer_size = get_aligned_fb_size(dst),
            .pic_w = dst->info.width,
            .pic_h = dst->info.height,
            .block_offset_x = dst_rect->x,
            .block_offset_y = dst_rect->y,
            .fill_cm = PPA_FILL_COLOR_MODE_RGB565,
        },
        .fill_block_w = dst_rect->width,
        .fill_block_h = dst_rect->height,
        .fill_argb_color.val = argb,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    ppa_srm_lock();
    esp_err_t ret = ppa_do_fill(use_ctx->fill, &cfg);
    ppa_srm_unlock();
    if (ret != ESP_OK) {
        ret_v = hw_err_from_esp(ret);
        goto done;
    }
    ret = cache_sync_rect(dst, dst_rect, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    ret_v = hw_err_from_esp(ret);
done:
    hw_op_leave();
    return ret_v;
}

esp_video_render_err_t video_render_blend_hw_bitblt(video_render_blend_hw_ctx_t *ctx,
                                                    esp_video_render_fb_t *dst,
                                                    esp_video_render_fb_t *src,
                                                    esp_video_render_rect_t *dst_rect,
                                                    esp_video_render_rect_t *src_rect)
{
    esp_video_render_err_t ret_v = ESP_VIDEO_RENDER_ERR_OK;
    bool byte_swap = false;
    video_render_blend_hw_ctx_t *use_ctx = NULL;

    if (!video_render_blend_hw_can_accel(dst, src, dst_rect, src_rect)) {
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }
    if (!get_rgb565_hw_cfg(dst->info.format, &byte_swap)) {
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }
    if ((uint32_t)dst_rect->width * dst_rect->height < HW_BLEND_MIN_PIXELS) {
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }

    hw_op_enter();
    ret_v = resolve_hw_ctx(ctx, &use_ctx);
    if (ret_v != ESP_VIDEO_RENDER_ERR_OK) {
        goto done;
    }
    ret_v = ensure_hw_client(use_ctx, HW_SRM_READY_BIT, HW_SRM_CREATING_BIT, PPA_OPERATION_SRM);
    if (ret_v != ESP_VIDEO_RENDER_ERR_OK) {
        goto done;
    }

    esp_err_t ret = cache_sync_rect(src, src_rect, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (ret != ESP_OK) {
        ret_v = hw_err_from_esp(ret);
        goto done;
    }
    ret = cache_sync_rect(dst, dst_rect, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (ret != ESP_OK) {
        ret_v = hw_err_from_esp(ret);
        goto done;
    }

    ppa_srm_oper_config_t cfg = {
        .in = {
            .buffer = src->data,
            .pic_w = src->info.width,
            .pic_h = src->info.height,
            .block_w = src_rect->width,
            .block_h = src_rect->height,
            .block_offset_x = src_rect->x,
            .block_offset_y = src_rect->y,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = dst->data,
            .buffer_size = get_aligned_fb_size(dst),
            .pic_w = dst->info.width,
            .pic_h = dst->info.height,
            .block_offset_x = dst_rect->x,
            .block_offset_y = dst_rect->y,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .mirror_x = false,
        .mirror_y = false,
        .byte_swap = byte_swap,
        .alpha_update_mode = PPA_ALPHA_NO_CHANGE,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    ppa_srm_lock();
    ret = ppa_do_scale_rotate_mirror(use_ctx->srm, &cfg);
    ppa_srm_unlock();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PPA SRM bitblt failed, ret=%d", ret);
        ret_v = hw_err_from_esp(ret);
        goto done;
    }
    ret = cache_sync_rect(dst, dst_rect, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    ret_v = hw_err_from_esp(ret);
done:
    hw_op_leave();
    return ret_v;
}

#else

struct video_render_blend_hw_ctx {
    uint8_t  reserve;
};

bool video_render_blend_hw_can_accel(const esp_video_render_fb_t *dst,
                                     const esp_video_render_fb_t *src,
                                     const esp_video_render_rect_t *dst_rect,
                                     const esp_video_render_rect_t *src_rect)
{
    (void)dst;
    (void)src;
    (void)dst_rect;
    (void)src_rect;
    return false;
}

bool video_render_blend_hw_can_fill(const esp_video_render_fb_t *dst, const esp_video_render_rect_t *dst_rect)
{
    (void)dst;
    (void)dst_rect;
    return false;
}

esp_video_render_err_t video_render_blend_hw_open(video_render_blend_hw_ctx_t **ctx)
{
    (void)ctx;
    return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
}

esp_video_render_err_t video_render_blend_hw_close(video_render_blend_hw_ctx_t *ctx)
{
    (void)ctx;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_blend_hw_process(video_render_blend_hw_ctx_t *ctx,
                                                     esp_video_render_fb_t *dst,
                                                     esp_video_render_fb_t *src,
                                                     esp_video_render_rect_t *dst_rect,
                                                     esp_video_render_rect_t *src_rect,
                                                     uint8_t global_alpha)
{
    (void)ctx;
    (void)dst;
    (void)src;
    (void)dst_rect;
    (void)src_rect;
    (void)global_alpha;
    return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
}

esp_video_render_err_t video_render_blend_hw_transparent_color(video_render_blend_hw_ctx_t *ctx,
                                                               esp_video_render_fb_t *dst,
                                                               esp_video_render_fb_t *src,
                                                               esp_video_render_rect_t *dst_rect,
                                                               esp_video_render_rect_t *src_rect,
                                                               esp_video_render_clr_t *trans_color)
{
    (void)ctx;
    (void)dst;
    (void)src;
    (void)dst_rect;
    (void)src_rect;
    (void)trans_color;
    return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
}

esp_video_render_err_t video_render_blend_hw_fill(video_render_blend_hw_ctx_t *ctx,
                                                  esp_video_render_fb_t *dst,
                                                  esp_video_render_rect_t *dst_rect,
                                                  esp_video_render_clr_t *color)
{
    (void)ctx;
    (void)dst;
    (void)dst_rect;
    (void)color;
    return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
}

esp_video_render_err_t video_render_blend_hw_bitblt(video_render_blend_hw_ctx_t *ctx,
                                                    esp_video_render_fb_t *dst,
                                                    esp_video_render_fb_t *src,
                                                    esp_video_render_rect_t *dst_rect,
                                                    esp_video_render_rect_t *src_rect)
{
    (void)ctx;
    (void)dst;
    (void)src;
    (void)dst_rect;
    (void)src_rect;
    return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
}

#endif  /* CONFIG_SOC_PPA_SUPPORTED */

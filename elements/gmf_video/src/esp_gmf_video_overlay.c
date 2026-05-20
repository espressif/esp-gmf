/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_idf_version.h"
#include "esp_gmf_err.h"
#include "esp_fourcc.h"
#include "esp_gmf_node.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_mutex.h"
#include "esp_gmf_video_element.h"
#include "esp_gmf_info.h"
#include "esp_gmf_video_overlay.h"
#include "esp_gmf_video_methods_def.h"
#include "esp_gmf_caps_def.h"
#include "gmf_video_common.h"

#if CONFIG_SOC_PPA_SUPPORTED
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_memory_utils.h"
#include "esp_private/esp_cache_private.h"
#endif  /* CONFIG_SOC_PPA_SUPPORTED */

static const char *TAG = "OVERLAY_MIXER";

#define TRANS_COLOR_TOLERANCE  8
#define HW_BLEND_MIN_PIXELS    (100 * 100)
#define OVERLAY_WINDOW_FMT     ESP_FOURCC_RGB16

typedef enum {
    OVERLAY_DST_UNSUPPORTED = 0,
    OVERLAY_DST_RGB565_LE   = 1,
    OVERLAY_DST_RGB565_BE   = 2,
    OVERLAY_DST_OUYY_EVYY   = 3,
} overlay_dst_fmt_t;

typedef struct {
    overlay_dst_fmt_t  type;
    uint8_t            pixel_bytes;
    bool               hw_blend;
    bool               dst_byte_swap;
} overlay_dst_desc_t;

typedef struct {
    uint8_t  *data;
    uint32_t  size;
} esp_gmf_video_pixel_data_t;

typedef struct {
    esp_gmf_video_element_t     parent;
    esp_gmf_port_handle_t       overlay_port;
    bool                        enable;
    bool                        overlay_enabled;
    esp_gmf_overlay_rgn_info_t *overlay_rgn;
    uint8_t                     rgn_count;
    uint8_t                     window_alpha;
    bool                        is_open;
#if CONFIG_SOC_PPA_SUPPORTED
    ppa_client_handle_t         ppa_blend;
#endif  /* CONFIG_SOC_PPA_SUPPORTED */
} gmf_vid_overlay_t;

#if CONFIG_SOC_PPA_SUPPORTED
void ppa_srm_lock(void);
void ppa_srm_unlock(void);
#endif  /* CONFIG_SOC_PPA_SUPPORTED */

static inline uint8_t rgb565_5_to_8(uint8_t v)
{
    return (uint8_t)((v << 3) | (v >> 2));
}

static inline uint8_t rgb565_6_to_8(uint8_t v)
{
    return (uint8_t)((v << 2) | (v >> 4));
}

static inline uint8_t diff_u8(uint8_t a, uint8_t b)
{
    return (a > b) ? (a - b) : (b - a);
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

static inline uint16_t sw_mix_rgb565(uint16_t pixel_a, uint16_t pixel_b, uint8_t alpha)
{
    uint8_t inv_alpha = 255 - alpha;
    uint8_t r_a = (pixel_a >> 11) & 0x1F;
    uint8_t g_a = (pixel_a >> 5) & 0x3F;
    uint8_t b_a = pixel_a & 0x1F;

    uint8_t r_b = (pixel_b >> 11) & 0x1F;
    uint8_t g_b = (pixel_b >> 5) & 0x3F;
    uint8_t b_b = pixel_b & 0x1F;

    uint8_t r = (r_a * inv_alpha + r_b * alpha) >> 8;
    uint8_t g = (g_a * inv_alpha + g_b * alpha) >> 8;
    uint8_t b = (b_a * inv_alpha + b_b * alpha) >> 8;

    return (uint16_t)((r << 11) | (g << 5) | b);
}

static inline bool pixel_match_trans_color_rgb565(uint16_t pixel, const uint8_t trans_rgb[3])
{
    uint8_t pr = rgb565_5_to_8((pixel >> 11) & 0x1F);
    uint8_t pg = rgb565_6_to_8((pixel >> 5) & 0x3F);
    uint8_t pb = rgb565_5_to_8(pixel & 0x1F);
    return diff_u8(pr, trans_rgb[0]) <= TRANS_COLOR_TOLERANCE &&
           diff_u8(pg, trans_rgb[1]) <= TRANS_COLOR_TOLERANCE &&
           diff_u8(pb, trans_rgb[2]) <= TRANS_COLOR_TOLERANCE;
}

static inline overlay_dst_desc_t overlay_get_dst_desc(uint32_t format_id)
{
    overlay_dst_desc_t desc = {
        .type = OVERLAY_DST_UNSUPPORTED,
        .pixel_bytes = 0,
        .hw_blend = false,
        .dst_byte_swap = false,
    };
    switch (format_id) {
        case ESP_FOURCC_RGB16:
            desc.type = OVERLAY_DST_RGB565_LE;
            desc.pixel_bytes = 2;
            desc.hw_blend = true;
            break;
        case ESP_FOURCC_RGB16_BE:
            desc.type = OVERLAY_DST_RGB565_BE;
            desc.pixel_bytes = 2;
            desc.hw_blend = true;
            desc.dst_byte_swap = true;
            break;
        case ESP_FOURCC_OUYY_EVYY:
            desc.type = OVERLAY_DST_OUYY_EVYY;
            desc.hw_blend = true;
            break;
        default:
            break;
    }
    return desc;
}

static inline bool overlay_dst_format_supported(uint32_t format_id)
{
    return overlay_get_dst_desc(format_id).type != OVERLAY_DST_UNSUPPORTED;
}

static inline bool overlay_rgn_format_valid(uint32_t pipeline_fmt, uint32_t rgn_fmt)
{
    if (!overlay_dst_format_supported(pipeline_fmt)) {
        return false;
    }
    if (rgn_fmt == 0) {
        return true;
    }
    if (!overlay_dst_format_supported(rgn_fmt)) {
        return false;
    }
    if (rgn_fmt != pipeline_fmt) {
        ESP_LOGW(TAG, "Region format %s differs from pipeline %s (blend uses pipeline)",
                 esp_gmf_video_get_format_string(rgn_fmt), esp_gmf_video_get_format_string(pipeline_fmt));
    }
    return true;
}

bool esp_gmf_video_overlay_dst_format_supported(uint32_t format_id)
{
    return overlay_dst_format_supported(format_id);
}

static inline uint32_t overlay_dst_frame_size(const esp_gmf_info_video_t *info, overlay_dst_desc_t *desc)
{
    if (desc->type == OVERLAY_DST_OUYY_EVYY) {
        return info->width * info->height * 3 / 2;
    }
    if (desc->pixel_bytes) {
        return info->width * info->height * desc->pixel_bytes;
    }
    return 0;
}

static uint8_t overlay_dst_cache_pixel_bytes(overlay_dst_desc_t *desc)
{
    if (desc->type == OVERLAY_DST_OUYY_EVYY) {
        return 0;
    }
    return desc->pixel_bytes;
}

static inline uint16_t overlay_read_dst_rgb565(uint16_t raw, bool dst_byte_swap)
{
    return dst_byte_swap ? __builtin_bswap16(raw) : raw;
}

static inline uint16_t overlay_write_dst_rgb565(uint16_t native, bool dst_byte_swap)
{
    return dst_byte_swap ? __builtin_bswap16(native) : native;
}

static inline esp_gmf_err_t sw_mixer_open(gmf_vid_overlay_t *mixer)
{
    overlay_dst_desc_t desc = overlay_get_dst_desc(mixer->parent.src_info.format_id);
    if (desc.type == OVERLAY_DST_UNSUPPORTED) {
        ESP_LOGE(TAG, "Unsupported dst format %s", esp_gmf_video_get_format_string(mixer->parent.src_info.format_id));
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    return ESP_GMF_ERR_OK;
}

static void rgb565_to_yuv(uint16_t rgb, uint8_t *y, uint8_t *u, uint8_t *v)
{
    uint8_t r = rgb565_5_to_8((rgb >> 11) & 0x1F);
    uint8_t g = rgb565_6_to_8((rgb >> 5) & 0x3F);
    uint8_t b = rgb565_5_to_8(rgb & 0x1F);
    int y_temp = (77 * r + 150 * g + 29 * b) >> 8;
    int u_temp = ((-43 * r - 85 * g + 128 * b) >> 8) + 128;
    int v_temp = ((128 * r - 107 * g - 21 * b) >> 8) + 128;
    if (y) {
        *y = clamp_u8(y_temp);
    }
    if (u) {
        *u = clamp_u8(u_temp);
    }
    if (v) {
        *v = clamp_u8(v_temp);
    }
}

static inline uint8_t blend_u8(uint8_t dst, uint8_t src, uint8_t alpha)
{
    return (uint8_t)(((src * alpha) + (dst * (255 - alpha))) >> 8);
}

static void ouyy_blend_uyy_pair(uint8_t *uyy, const uint16_t *win, uint8_t alpha, const uint8_t *trans_color)
{
    if (trans_color == NULL) {
        if (alpha == 255) {
            rgb565_to_yuv(win[0], &uyy[1], &uyy[0], NULL);
            rgb565_to_yuv(win[1], &uyy[2], NULL, NULL);
            return;
        }
        uint8_t y0 = 0;
        uint8_t u = 0;
        rgb565_to_yuv(win[0], &y0, &u, NULL);
        uint8_t y1 = 0;
        rgb565_to_yuv(win[1], &y1, NULL, NULL);
        uyy[0] = blend_u8(uyy[0], u, alpha);
        uyy[1] = blend_u8(uyy[1], y0, alpha);
        uyy[2] = blend_u8(uyy[2], y1, alpha);
        return;
    }
    if (alpha == 255) {
        if (!pixel_match_trans_color_rgb565(win[0], trans_color)) {
            rgb565_to_yuv(win[0], &uyy[1], &uyy[0], NULL);
        }
        if (!pixel_match_trans_color_rgb565(win[1], trans_color)) {
            rgb565_to_yuv(win[1], &uyy[2], NULL, NULL);
        }
        return;
    }
    if (!pixel_match_trans_color_rgb565(win[0], trans_color)) {
        uint8_t y0 = 0;
        uint8_t u = 0;
        rgb565_to_yuv(win[0], &y0, &u, NULL);
        uyy[0] = blend_u8(uyy[0], u, alpha);
        uyy[1] = blend_u8(uyy[1], y0, alpha);
    }
    if (!pixel_match_trans_color_rgb565(win[1], trans_color)) {
        uint8_t y1 = 0;
        rgb565_to_yuv(win[1], &y1, NULL, NULL);
        uyy[2] = blend_u8(uyy[2], y1, alpha);
    }
}

static void ouyy_blend_vyy_pair(uint8_t *vyy, const uint16_t *win, uint8_t alpha, const uint8_t *trans_color)
{
    if (trans_color == NULL) {
        if (alpha == 255) {
            rgb565_to_yuv(win[0], &vyy[1], NULL, &vyy[0]);
            rgb565_to_yuv(win[1], &vyy[2], NULL, NULL);
            return;
        }
        uint8_t y0 = 0;
        uint8_t v = 0;
        rgb565_to_yuv(win[0], &y0, NULL, &v);
        uint8_t y1 = 0;
        rgb565_to_yuv(win[1], &y1, NULL, NULL);
        vyy[0] = blend_u8(vyy[0], v, alpha);
        vyy[1] = blend_u8(vyy[1], y0, alpha);
        vyy[2] = blend_u8(vyy[2], y1, alpha);
        return;
    }
    if (alpha == 255) {
        if (!pixel_match_trans_color_rgb565(win[0], trans_color)) {
            rgb565_to_yuv(win[0], &vyy[1], NULL, &vyy[0]);
        }
        if (!pixel_match_trans_color_rgb565(win[1], trans_color)) {
            rgb565_to_yuv(win[1], &vyy[2], NULL, NULL);
        }
        return;
    }
    if (!pixel_match_trans_color_rgb565(win[0], trans_color)) {
        uint8_t y0 = 0;
        uint8_t v = 0;
        rgb565_to_yuv(win[0], &y0, NULL, &v);
        vyy[0] = blend_u8(vyy[0], v, alpha);
        vyy[1] = blend_u8(vyy[1], y0, alpha);
    }
    if (!pixel_match_trans_color_rgb565(win[1], trans_color)) {
        uint8_t y1 = 0;
        rgb565_to_yuv(win[1], &y1, NULL, NULL);
        vyy[2] = blend_u8(vyy[2], y1, alpha);
    }
}

static void ouyy_blend_rgb565_window(uint8_t *dst_base, uint32_t frame_width, esp_gmf_video_rgn_t *window,
                                     uint16_t *window_pixel, uint8_t alpha, const uint8_t *trans_color)
{
    const uint32_t dst_line_pitch = frame_width * 3 / 2;
    const uint32_t dst_pair_pitch = frame_width * 3;
    const int win_pitch = window->width;

    uint8_t *dst_pair = dst_base + (window->y / 2) * dst_pair_pitch + (window->x / 2) * 3;
    uint16_t *win_row = window_pixel;

    for (int i = 0; i < window->height; i += 2) {
        uint8_t *uyy = dst_pair;
        uint8_t *vyy = dst_pair + dst_line_pitch;
        uint16_t *win_odd = win_row;
        uint16_t *win_even = win_row + win_pitch;

        for (int j = 0; j < window->width; j += 2) {
            ouyy_blend_uyy_pair(uyy, &win_odd[j], alpha, trans_color);
            ouyy_blend_vyy_pair(vyy, &win_even[j], alpha, trans_color);
            uyy += 3;
            vyy += 3;
        }
        dst_pair += dst_pair_pitch;
        win_row += win_pitch * 2;
    }
}

static esp_gmf_err_t sw_mixer_rgb565(gmf_vid_overlay_t *mixer, uint8_t alpha, esp_gmf_video_rgn_t *window,
                                     overlay_dst_desc_t *desc, esp_gmf_video_pixel_data_t *dst,
                                     esp_gmf_video_pixel_data_t *window_data)
{
    if (alpha == 0) {
        return ESP_GMF_ERR_OK;
    }
    esp_gmf_info_video_t *src_info = &mixer->parent.src_info;
    uint32_t dst_size = overlay_dst_frame_size(src_info, desc);
    int window_size = window->width * window->height * 2;
    if (dst->size < dst_size || window_data->size < window_size) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    uint16_t *dst_pixel = ((uint16_t *)dst->data) + window->y * src_info->width + window->x;
    uint16_t *window_pixel = (uint16_t *)window_data->data;
    bool dst_swap = desc->dst_byte_swap;
    if (alpha == 255) {
        for (int i = 0; i < window->height; i++) {
            for (int j = 0; j < window->width; j++) {
                dst_pixel[j] = overlay_write_dst_rgb565(window_pixel[j], dst_swap);
            }
            dst_pixel += src_info->width;
            window_pixel += window->width;
        }
        return ESP_GMF_ERR_OK;
    }
    for (int i = 0; i < window->height; i++) {
        for (int j = 0; j < window->width; j++) {
            uint16_t dst_native = overlay_read_dst_rgb565(dst_pixel[j], dst_swap);
            dst_pixel[j] = overlay_write_dst_rgb565(sw_mix_rgb565(dst_native, window_pixel[j], alpha), dst_swap);
        }
        dst_pixel += src_info->width;
        window_pixel += window->width;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t sw_mixer_rgb565_trans(gmf_vid_overlay_t *mixer, const uint8_t trans_color[3],
                                           esp_gmf_video_rgn_t *window, overlay_dst_desc_t *desc,
                                           esp_gmf_video_pixel_data_t *dst, esp_gmf_video_pixel_data_t *window_data)
{
    esp_gmf_info_video_t *src_info = &mixer->parent.src_info;
    uint32_t dst_size = overlay_dst_frame_size(src_info, desc);
    int window_size = window->width * window->height * 2;
    if (dst->size < dst_size || window_data->size < window_size) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    uint16_t *dst_pixel = ((uint16_t *)dst->data) + window->y * src_info->width + window->x;
    uint16_t *window_pixel = (uint16_t *)window_data->data;
    bool dst_swap = desc->dst_byte_swap;
    for (int i = 0; i < window->height; i++) {
        for (int j = 0; j < window->width; j++) {
            if (!pixel_match_trans_color_rgb565(window_pixel[j], trans_color)) {
                dst_pixel[j] = overlay_write_dst_rgb565(window_pixel[j], dst_swap);
            }
        }
        dst_pixel += src_info->width;
        window_pixel += window->width;
    }
    return ESP_GMF_ERR_OK;
}

static void ouyy_align_rgn(esp_gmf_video_rgn_t *window)
{
    window->x = window->x / 2 * 2;
    window->y = window->y / 2 * 2;
    window->width = window->width / 2 * 2;
    window->height = window->height / 2 * 2;
}

static esp_gmf_err_t sw_mixer_ouyy_evyy(gmf_vid_overlay_t *mixer, uint8_t alpha, esp_gmf_video_rgn_t *window,
                                        esp_gmf_video_pixel_data_t *dst, esp_gmf_video_pixel_data_t *window_data)
{
    if (alpha == 0) {
        return ESP_GMF_ERR_OK;
    }
    esp_gmf_info_video_t *src_info = &mixer->parent.src_info;
    ouyy_align_rgn(window);
    overlay_dst_desc_t desc = overlay_get_dst_desc(ESP_FOURCC_OUYY_EVYY);
    uint32_t dst_size = overlay_dst_frame_size(src_info, &desc);
    int window_size = window->width * window->height * 2;
    if (dst->size < dst_size || window_data->size < window_size) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    ouyy_blend_rgb565_window(dst->data, src_info->width, window, (uint16_t *)window_data->data, alpha, NULL);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t sw_mixer_ouyy_evyy_trans(gmf_vid_overlay_t *mixer, const uint8_t trans_color[3],
                                              esp_gmf_video_rgn_t *window, esp_gmf_video_pixel_data_t *dst,
                                              esp_gmf_video_pixel_data_t *window_data)
{
    esp_gmf_info_video_t *src_info = &mixer->parent.src_info;
    ouyy_align_rgn(window);
    overlay_dst_desc_t desc = overlay_get_dst_desc(ESP_FOURCC_OUYY_EVYY);
    uint32_t dst_size = overlay_dst_frame_size(src_info, &desc);
    int window_size = window->width * window->height * 2;
    if (dst->size < dst_size || window_data->size < window_size) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    ouyy_blend_rgb565_window(dst->data, src_info->width, window, (uint16_t *)window_data->data, 255, trans_color);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t sw_mixer_process(gmf_vid_overlay_t *mixer, uint8_t alpha, esp_gmf_video_rgn_t *window,
                                      overlay_dst_desc_t *desc, esp_gmf_video_pixel_data_t *dst,
                                      esp_gmf_video_pixel_data_t *window_data)
{
    switch (desc->type) {
        case OVERLAY_DST_RGB565_LE:
        case OVERLAY_DST_RGB565_BE:
            return sw_mixer_rgb565(mixer, alpha, window, desc, dst, window_data);
        case OVERLAY_DST_OUYY_EVYY:
            return sw_mixer_ouyy_evyy(mixer, alpha, window, dst, window_data);
        default:
            return ESP_GMF_ERR_NOT_SUPPORT;
    }
}

static esp_gmf_err_t sw_mixer_trans_process(gmf_vid_overlay_t *mixer, const uint8_t trans_color[3],
                                            esp_gmf_video_rgn_t *window, overlay_dst_desc_t *desc,
                                            esp_gmf_video_pixel_data_t *dst, esp_gmf_video_pixel_data_t *window_data)
{
    switch (desc->type) {
        case OVERLAY_DST_RGB565_LE:
        case OVERLAY_DST_RGB565_BE:
            return sw_mixer_rgb565_trans(mixer, trans_color, window, desc, dst, window_data);
        case OVERLAY_DST_OUYY_EVYY:
            return sw_mixer_ouyy_evyy_trans(mixer, trans_color, window, dst, window_data);
        default:
            return ESP_GMF_ERR_NOT_SUPPORT;
    }
}

#if CONFIG_SOC_PPA_SUPPORTED

typedef struct {
    ppa_blend_color_mode_t  blend_cm;
    bool                    byte_swap;
    bool                    rgb_swap;
} ppa_blend_fmt_cfg_t;

static uint32_t ppa_blend_frame_size(uint32_t fmt, uint32_t width, uint32_t height)
{
    switch (fmt) {
        case ESP_FOURCC_RGB24:
        case ESP_FOURCC_BGR24:
            return width * height * 3;
        case ESP_FOURCC_RGB16:
        case ESP_FOURCC_RGB16_BE:
            return width * height * 2;
        case ESP_FOURCC_OUYY_EVYY:
            return width * height * 3 / 2;
        case ESP_FOURCC_YUYV:
        case ESP_FOURCC_UYVY:
        case ESP_FOURCC_YVYU:
        case ESP_FOURCC_VYUY:
            return width * height * 2;
        default:
            return 0;
    }
}

static bool get_ppa_blend_clr_mode(uint32_t codec, bool is_fg, ppa_blend_fmt_cfg_t *cfg)
{
    cfg->blend_cm = (ppa_blend_color_mode_t)0;
    cfg->byte_swap = false;
    cfg->rgb_swap = false;

    switch (codec) {
        case ESP_FOURCC_RGB16:
            cfg->blend_cm = PPA_BLEND_COLOR_MODE_RGB565;
            return true;
        case ESP_FOURCC_RGB16_BE:
            cfg->blend_cm = PPA_BLEND_COLOR_MODE_RGB565;
            cfg->byte_swap = true;
            return true;
        case ESP_FOURCC_RGB24:
            cfg->blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
            cfg->rgb_swap = true;
            return is_fg;
        case ESP_FOURCC_BGR24:
            cfg->blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
            return is_fg;
        case ESP_FOURCC_OUYY_EVYY:
            if (is_fg) {
                return false;
            }
            cfg->blend_cm = PPA_BLEND_COLOR_MODE_YUV420;
            return true;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
        case ESP_FOURCC_YUYV:
            if (is_fg) {
                return false;
            }
            cfg->blend_cm = PPA_BLEND_COLOR_MODE_YUV422_YUYV;
            return true;
        case ESP_FOURCC_UYVY:
            if (is_fg) {
                return false;
            }
            cfg->blend_cm = PPA_BLEND_COLOR_MODE_YUV422_UYVY;
            return true;
        case ESP_FOURCC_YVYU:
            if (is_fg) {
                return false;
            }
            cfg->blend_cm = PPA_BLEND_COLOR_MODE_YUV422_YVYU;
            return true;
        case ESP_FOURCC_VYUY:
            if (is_fg) {
                return false;
            }
            cfg->blend_cm = PPA_BLEND_COLOR_MODE_YUV422_VYUY;
            return true;
#endif  /* ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0) */
        default:
            return false;
    }
}

static size_t overlay_get_buf_align(uint8_t *buf)
{
    size_t align = 64;
    uint32_t caps = esp_ptr_external_ram(buf) ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
    if (esp_cache_get_alignment(caps, &align) != ESP_OK || align == 0) {
        align = 64;
    }
    return align;
}

static esp_gmf_err_t overlay_cache_sync_ouyy_frame(uint8_t *data, uint32_t width, uint32_t height, int flag)
{
    if (!esp_ptr_external_ram(data)) {
        return ESP_GMF_ERR_OK;
    }
    size_t frame_bytes = (size_t)width * height * 3 / 2;
    size_t align = overlay_get_buf_align(data);
    uintptr_t addr = (uintptr_t)data;
    uintptr_t aligned_addr = addr & ~(uintptr_t)(align - 1);
    size_t aligned_size = ((addr - aligned_addr) + frame_bytes + align - 1) & ~(align - 1);
    if (esp_cache_msync((void *)aligned_addr, aligned_size, flag) != ESP_OK) {
        return ESP_GMF_ERR_FAIL;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t overlay_cache_sync_rect(uint8_t *data, uint32_t width, esp_gmf_video_rgn_t *rect,
                                             uint8_t pixel_bytes, int flag)
{
    if (!esp_ptr_external_ram(data) || pixel_bytes == 0) {
        return ESP_GMF_ERR_OK;
    }
    size_t stride = (size_t)width * pixel_bytes;
    size_t row_size = (size_t)rect->width * pixel_bytes;
    size_t align = overlay_get_buf_align(data);
    uint8_t *row = data + (size_t)rect->y * stride + (size_t)rect->x * pixel_bytes;
    for (uint16_t y = 0; y < rect->height; y++) {
        uintptr_t addr = (uintptr_t)row;
        uintptr_t aligned_addr = addr & ~(uintptr_t)(align - 1);
        size_t aligned_size = ((addr - aligned_addr) + row_size + align - 1) & ~(align - 1);
        if (esp_cache_msync((void *)aligned_addr, aligned_size, flag) != ESP_OK) {
            return ESP_GMF_ERR_FAIL;
        }
        row += stride;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t hw_mixer_open(gmf_vid_overlay_t *mixer)
{
    if (mixer->ppa_blend) {
        return ESP_GMF_ERR_OK;
    }
    ppa_client_config_t cfg = {
        .oper_type = PPA_OPERATION_BLEND,
        .max_pending_trans_num = 1,
    };
    if (ppa_register_client(&cfg, &mixer->ppa_blend) != ESP_OK) {
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    return ESP_GMF_ERR_OK;
}

static void hw_mixer_close(gmf_vid_overlay_t *mixer)
{
    if (mixer->ppa_blend) {
        ppa_unregister_client(mixer->ppa_blend);
        mixer->ppa_blend = NULL;
    }
}

static void ppa_blend_apply_yuv_fields(ppa_blend_oper_config_t *cfg, ppa_blend_color_mode_t cm)
{
    if (cm != PPA_BLEND_COLOR_MODE_YUV420) {
        return;
    }
    cfg->in_bg.yuv_range = PPA_COLOR_RANGE_LIMIT;
    cfg->in_bg.yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601;
    cfg->out.yuv_range = PPA_COLOR_RANGE_LIMIT;
    cfg->out.yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601;
}

static bool hw_mixer_can_use(esp_gmf_video_rgn_t *window)
{
    return (uint32_t)window->width * window->height >= HW_BLEND_MIN_PIXELS;
}

static bool overlay_hw_path_usable(overlay_dst_desc_t *desc, esp_gmf_video_rgn_t *window,
                                   esp_gmf_info_video_t *src_info, bool use_trans_color)
{
    ppa_blend_fmt_cfg_t fg_cfg = {0};
    ppa_blend_fmt_cfg_t bg_cfg = {0};

    if (!desc->hw_blend || !hw_mixer_can_use(window)) {
        return false;
    }
    if (!get_ppa_blend_clr_mode(OVERLAY_WINDOW_FMT, true, &fg_cfg) ||
        !get_ppa_blend_clr_mode(src_info->format_id, false, &bg_cfg)) {
        return false;
    }
    if (desc->type == OVERLAY_DST_OUYY_EVYY) {
        if ((window->x | window->y | window->width | window->height) & 1) {
            return false;
        }
        return false;
    }
    return true;
}

static esp_gmf_err_t hw_mixer_blend_do(gmf_vid_overlay_t *mixer, esp_gmf_video_rgn_t *blend_win,
                                       overlay_dst_desc_t *desc, esp_gmf_info_video_t *src_info,
                                       esp_gmf_video_pixel_data_t *dst, esp_gmf_video_pixel_data_t *window_data,
                                       ppa_blend_oper_config_t *cfg)
{
    esp_gmf_video_rgn_t dst_rect = *blend_win;
    esp_gmf_video_rgn_t src_rect = {
        .x = 0,
        .y = 0,
        .width = blend_win->width,
        .height = blend_win->height,
    };
    bool ouyy_frame = (desc->type == OVERLAY_DST_OUYY_EVYY);
    uint8_t dst_px_bytes = overlay_dst_cache_pixel_bytes(desc);
    esp_gmf_err_t ret;
    if (ouyy_frame) {
        ret = overlay_cache_sync_ouyy_frame(dst->data, src_info->width, src_info->height, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    } else {
        ret = overlay_cache_sync_rect(dst->data, src_info->width, &dst_rect, dst_px_bytes,
                                      ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    }
    if (ret != ESP_GMF_ERR_OK) {
        return ret;
    }
    ret = overlay_cache_sync_rect(window_data->data, blend_win->width, &src_rect, 2,
                                  ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (ret != ESP_GMF_ERR_OK) {
        return ret;
    }

    ppa_blend_fmt_cfg_t fg_cfg = {0};
    ppa_blend_fmt_cfg_t bg_cfg = {0};
    get_ppa_blend_clr_mode(OVERLAY_WINDOW_FMT, true, &fg_cfg);
    get_ppa_blend_clr_mode(src_info->format_id, false, &bg_cfg);
    size_t out_size = overlay_dst_frame_size(src_info, desc);

    ppa_alpha_update_mode_t bg_alpha_mode = cfg->bg_alpha_update_mode;
    uint8_t bg_alpha_val = cfg->bg_alpha_fix_val;
    ppa_alpha_update_mode_t fg_alpha_mode = cfg->fg_alpha_update_mode;
    uint8_t fg_alpha_val = cfg->fg_alpha_fix_val;
    bool bg_ck_en = cfg->bg_ck_en;
    bool fg_ck_en = cfg->fg_ck_en;
    color_pixel_rgb888_data_t fg_ck_low = cfg->fg_ck_rgb_low_thres;
    color_pixel_rgb888_data_t fg_ck_high = cfg->fg_ck_rgb_high_thres;

    *cfg = (ppa_blend_oper_config_t) {
        .in_bg = {
            .buffer = dst->data,
            .pic_w = src_info->width,
            .pic_h = src_info->height,
            .block_w = blend_win->width,
            .block_h = blend_win->height,
            .block_offset_x = blend_win->x,
            .block_offset_y = blend_win->y,
            .blend_cm = bg_cfg.blend_cm,
        },
        .in_fg = {
            .buffer = window_data->data,
            .pic_w = blend_win->width,
            .pic_h = blend_win->height,
            .block_w = blend_win->width,
            .block_h = blend_win->height,
            .blend_cm = fg_cfg.blend_cm,
        },
        .out = {
            .buffer = dst->data,
            .buffer_size = out_size,
            .pic_w = src_info->width,
            .pic_h = src_info->height,
            .block_offset_x = blend_win->x,
            .block_offset_y = blend_win->y,
            .blend_cm = bg_cfg.blend_cm,
        },
        .bg_rgb_swap = bg_cfg.rgb_swap,
        .bg_byte_swap = bg_cfg.byte_swap,
        .fg_rgb_swap = fg_cfg.rgb_swap,
        .fg_byte_swap = fg_cfg.byte_swap,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    cfg->bg_alpha_update_mode = bg_alpha_mode;
    cfg->bg_alpha_fix_val = bg_alpha_val;
    cfg->fg_alpha_update_mode = fg_alpha_mode;
    cfg->fg_alpha_fix_val = fg_alpha_val;
    cfg->bg_ck_en = bg_ck_en;
    cfg->fg_ck_en = fg_ck_en;
    cfg->fg_ck_rgb_low_thres = fg_ck_low;
    cfg->fg_ck_rgb_high_thres = fg_ck_high;
    ppa_blend_apply_yuv_fields(cfg, bg_cfg.blend_cm);

    ppa_srm_lock();
    esp_err_t err = ppa_do_blend(mixer->ppa_blend, cfg);
    ppa_srm_unlock();
    if (err != ESP_OK) {
        return ESP_GMF_ERR_FAIL;
    }
    if (ouyy_frame) {
        return overlay_cache_sync_ouyy_frame(dst->data, src_info->width, src_info->height, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    }
    return overlay_cache_sync_rect(dst->data, src_info->width, &dst_rect, dst_px_bytes,
                                   ESP_CACHE_MSYNC_FLAG_DIR_M2C);
}

static esp_gmf_err_t hw_mixer_process(gmf_vid_overlay_t *mixer, uint8_t alpha, esp_gmf_video_rgn_t *window,
                                      overlay_dst_desc_t *desc, esp_gmf_video_pixel_data_t *dst,
                                      esp_gmf_video_pixel_data_t *window_data)
{
    esp_gmf_info_video_t *src_info = &mixer->parent.src_info;
    esp_gmf_video_rgn_t blend_win = *window;

    if (alpha == 0) {
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    if (desc->type == OVERLAY_DST_OUYY_EVYY) {
        ouyy_align_rgn(&blend_win);
    }
    if (!overlay_hw_path_usable(desc, &blend_win, src_info, false)) {
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    esp_gmf_err_t ret = hw_mixer_open(mixer);
    if (ret != ESP_GMF_ERR_OK) {
        return ret;
    }
    ppa_blend_oper_config_t cfg = {0};
    cfg.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
    cfg.bg_alpha_fix_val = 255 - alpha;
    cfg.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
    cfg.fg_alpha_fix_val = alpha;
    cfg.bg_ck_en = false;
    cfg.fg_ck_en = false;
    return hw_mixer_blend_do(mixer, &blend_win, desc, src_info, dst, window_data, &cfg);
}

static esp_gmf_err_t hw_mixer_trans_process(gmf_vid_overlay_t *mixer, const uint8_t trans_color[3],
                                            esp_gmf_video_rgn_t *window, overlay_dst_desc_t *desc,
                                            esp_gmf_video_pixel_data_t *dst, esp_gmf_video_pixel_data_t *window_data)
{
    esp_gmf_info_video_t *src_info = &mixer->parent.src_info;
    esp_gmf_video_rgn_t blend_win = *window;

    if (desc->type == OVERLAY_DST_OUYY_EVYY) {
        ouyy_align_rgn(&blend_win);
    }
    if (!overlay_hw_path_usable(desc, &blend_win, src_info, true)) {
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    esp_gmf_err_t ret = hw_mixer_open(mixer);
    if (ret != ESP_GMF_ERR_OK) {
        return ret;
    }
    ppa_blend_oper_config_t cfg = {0};
    cfg.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
    cfg.bg_alpha_fix_val = 0;
    cfg.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
    cfg.fg_alpha_fix_val = 255;
    cfg.bg_ck_en = false;
    cfg.fg_ck_en = true;
    cfg.fg_ck_rgb_low_thres = (color_pixel_rgb888_data_t) {
        .r = clamp_u8((int)trans_color[0] - TRANS_COLOR_TOLERANCE),
        .g = clamp_u8((int)trans_color[1] - TRANS_COLOR_TOLERANCE),
        .b = clamp_u8((int)trans_color[2] - TRANS_COLOR_TOLERANCE),
    };
    cfg.fg_ck_rgb_high_thres = (color_pixel_rgb888_data_t) {
        .r = clamp_u8((int)trans_color[0] + TRANS_COLOR_TOLERANCE),
        .g = clamp_u8((int)trans_color[1] + TRANS_COLOR_TOLERANCE),
        .b = clamp_u8((int)trans_color[2] + TRANS_COLOR_TOLERANCE),
    };
    return hw_mixer_blend_do(mixer, &blend_win, desc, src_info, dst, window_data, &cfg);
}

bool esp_gmf_video_overlay_hw_blend_supported(uint32_t dst_fmt)
{
    overlay_dst_desc_t desc = overlay_get_dst_desc(dst_fmt);
    ppa_blend_fmt_cfg_t fg_cfg = {0};
    ppa_blend_fmt_cfg_t bg_cfg = {0};
    return desc.hw_blend &&
           get_ppa_blend_clr_mode(OVERLAY_WINDOW_FMT, true, &fg_cfg) &&
           get_ppa_blend_clr_mode(dst_fmt, false, &bg_cfg);
}

esp_gmf_err_t gmf_video_ppa_blend_probe(uint32_t src_fmt, uint32_t dst_fmt, uint32_t width, uint32_t height)
{
    ppa_blend_fmt_cfg_t fg_cfg = {0};
    ppa_blend_fmt_cfg_t bg_cfg = {0};
    if (!get_ppa_blend_clr_mode(src_fmt, true, &fg_cfg) || !get_ppa_blend_clr_mode(dst_fmt, false, &bg_cfg)) {
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    if (width < 100 || height < 100) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    uint32_t bg_size = ppa_blend_frame_size(dst_fmt, width, height);
    uint32_t fg_size = ppa_blend_frame_size(src_fmt, width, height);
    if (bg_size == 0 || fg_size == 0) {
        return ESP_GMF_ERR_INVALID_ARG;
    }

    uint8_t align = (uint8_t)esp_gmf_oal_get_spiram_cache_align();
    uint8_t *bg_buf = (uint8_t *)esp_gmf_oal_malloc_align(align, bg_size);
    uint8_t *fg_buf = (uint8_t *)esp_gmf_oal_malloc_align(align, fg_size);
    if (bg_buf == NULL || fg_buf == NULL) {
        esp_gmf_oal_free(bg_buf);
        esp_gmf_oal_free(fg_buf);
        return ESP_GMF_ERR_MEMORY_LACK;
    }
    memset(bg_buf, 0x40, bg_size);
    memset(fg_buf, 0xF8, fg_size);

    ppa_client_handle_t blend = NULL;
    ppa_client_config_t client_cfg = {
        .oper_type = PPA_OPERATION_BLEND,
        .max_pending_trans_num = 1,
    };
    esp_err_t err = ppa_register_client(&client_cfg, &blend);
    if (err != ESP_OK) {
        goto _probe_done;
    }

    ppa_blend_oper_config_t cfg = {
        .in_bg = {
            .buffer = bg_buf,
            .pic_w = width,
            .pic_h = height,
            .block_w = width,
            .block_h = height,
            .blend_cm = bg_cfg.blend_cm,
        },
        .in_fg = {
            .buffer = fg_buf,
            .pic_w = width,
            .pic_h = height,
            .block_w = width,
            .block_h = height,
            .blend_cm = fg_cfg.blend_cm,
        },
        .out = {
            .buffer = bg_buf,
            .buffer_size = bg_size,
            .pic_w = width,
            .pic_h = height,
            .blend_cm = bg_cfg.blend_cm,
        },
        .bg_rgb_swap = bg_cfg.rgb_swap,
        .bg_byte_swap = bg_cfg.byte_swap,
        .fg_rgb_swap = fg_cfg.rgb_swap,
        .fg_byte_swap = fg_cfg.byte_swap,
        .bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE,
        .bg_alpha_fix_val = 128,
        .fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE,
        .fg_alpha_fix_val = 128,
        .bg_ck_en = false,
        .fg_ck_en = false,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    ppa_blend_apply_yuv_fields(&cfg, bg_cfg.blend_cm);
    ppa_srm_lock();
    err = ppa_do_blend(blend, &cfg);
    ppa_srm_unlock();
    ppa_unregister_client(blend);

_probe_done:
    esp_gmf_oal_free(bg_buf);
    esp_gmf_oal_free(fg_buf);
    return err ? ESP_GMF_ERR_NOT_SUPPORT : ESP_GMF_ERR_OK;
}

#endif  /* CONFIG_SOC_PPA_SUPPORTED */

static esp_gmf_err_t mixer_process(gmf_vid_overlay_t *mixer, uint8_t alpha, esp_gmf_overlay_rgn_info_t *overlay_rgn,
                                   esp_gmf_video_pixel_data_t *dst, esp_gmf_video_pixel_data_t *window_data)
{
    esp_gmf_video_rgn_t *window = &overlay_rgn->dst_rgn;
    overlay_dst_desc_t desc = overlay_get_dst_desc(mixer->parent.src_info.format_id);
    if (desc.type == OVERLAY_DST_UNSUPPORTED) {
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
#if CONFIG_SOC_PPA_SUPPORTED
    esp_gmf_err_t ret = ESP_GMF_ERR_NOT_SUPPORT;
    if (overlay_rgn->has_trans_color) {
        ret = hw_mixer_trans_process(mixer, overlay_rgn->trans_color, window, &desc, dst, window_data);
    } else {
        ret = hw_mixer_process(mixer, alpha, window, &desc, dst, window_data);
    }
    if (ret == ESP_GMF_ERR_OK) {
        return ret;
    }
#endif  /* CONFIG_SOC_PPA_SUPPORTED */
    if (overlay_rgn->has_trans_color) {
        return sw_mixer_trans_process(mixer, overlay_rgn->trans_color, window, &desc, dst, window_data);
    }
    return sw_mixer_process(mixer, alpha, window, &desc, dst, window_data);
}

static esp_gmf_err_t overlay_enable(gmf_vid_overlay_t *overlay_mixer)
{
    esp_gmf_info_video_t *src_info = &overlay_mixer->parent.src_info;
    if (overlay_mixer->overlay_port == NULL || overlay_mixer->enable == false || overlay_mixer->is_open == false) {
        return ESP_GMF_ERR_OK;
    }
    if (overlay_mixer->overlay_enabled) {
        return ESP_GMF_ERR_OK;
    }
    if (overlay_mixer->rgn_count == 0 || overlay_mixer->overlay_rgn == NULL) {
        ESP_LOGE(TAG, "No overlay region configured");
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    if (!overlay_dst_format_supported(src_info->format_id)) {
        ESP_LOGE(TAG, "Unsupported dst format %s", esp_gmf_video_get_format_string(src_info->format_id));
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    for (int i = 0; i < overlay_mixer->rgn_count; i++) {
        esp_gmf_overlay_rgn_info_t *rgn = &overlay_mixer->overlay_rgn[i];
        if (!overlay_rgn_format_valid(src_info->format_id, rgn->format_id)) {
            ESP_LOGE(TAG, "Region %d unsupported format %s (pipeline %s)", i,
                     esp_gmf_video_get_format_string(rgn->format_id),
                     esp_gmf_video_get_format_string(src_info->format_id));
            return ESP_GMF_ERR_NOT_SUPPORT;
        }
        esp_gmf_video_rgn_t *dst = &rgn->dst_rgn;
        if (dst->x + dst->width > src_info->width || dst->y + dst->height > src_info->height) {
            ESP_LOGE(TAG, "Wrong overlay region %d", i);
            return ESP_GMF_ERR_NOT_SUPPORT;
        }
    }
    esp_gmf_err_t ret = sw_mixer_open(overlay_mixer);
    if (ret != ESP_GMF_ERR_OK) {
        return ret;
    }
    overlay_mixer->overlay_enabled = true;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t overlay_disable(gmf_vid_overlay_t *overlay_mixer)
{
    if (overlay_mixer->overlay_enabled == false) {
        return ESP_GMF_ERR_INVALID_STATE;
    }
    overlay_mixer->overlay_enabled = false;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t gmf_vid_overlay_open(esp_gmf_element_handle_t self, void *para)
{
    gmf_vid_overlay_t *overlay_mixer = (gmf_vid_overlay_t *)self;
    overlay_mixer->is_open = true;
    esp_gmf_err_t ret = overlay_enable(overlay_mixer);
    if (ret != ESP_GMF_ERR_OK) {
        return ESP_GMF_JOB_ERR_FAIL;
    }
    esp_gmf_element_notify_vid_info(self, &overlay_mixer->parent.src_info);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t gmf_vid_overlay_process(esp_gmf_element_handle_t self, void *para)
{
    gmf_vid_overlay_t *overlay_mixer = (gmf_vid_overlay_t *)self;
    int ret = 0;
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    ret = esp_gmf_port_acquire_in(in_port, &in_load, ESP_GMF_ELEMENT_GET(self)->in_attr.data_size, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_IN_CHECK(TAG, ret, ret, return ret);
    out_load = in_load;
    if (in_load->valid_size > 0 && overlay_mixer->overlay_enabled && overlay_mixer->overlay_port) {
        for (int i = 0; i < overlay_mixer->rgn_count; i++) {
            esp_gmf_overlay_rgn_info_t *overlay_rgn = &overlay_mixer->overlay_rgn[i];
            esp_gmf_payload_t *overlay_load = NULL;
            ret = esp_gmf_port_acquire_in(overlay_mixer->overlay_port, &overlay_load,
                                          ESP_GMF_ELEMENT_GET(self)->in_attr.data_size, ESP_GMF_MAX_DELAY);
            if (ret < 0) {
                ESP_LOGE(TAG, "Fail to fetch overlay data ret %d", ret);
                break;
            }
            uint8_t alpha = (uint8_t)overlay_load->pts;
            esp_gmf_video_pixel_data_t dst_frame = {
                .data = in_load->buf,
                .size = in_load->valid_size,
            };
            esp_gmf_video_pixel_data_t overlay_frame = {
                .data = overlay_load->buf,
                .size = overlay_load->valid_size,
            };
            esp_gmf_err_t mix_ret = mixer_process(overlay_mixer, alpha, overlay_rgn, &dst_frame, &overlay_frame);
            esp_gmf_port_release_in(overlay_mixer->overlay_port, overlay_load, ESP_GMF_MAX_DELAY);
            if (mix_ret != ESP_GMF_ERR_OK) {
                ESP_LOGE(TAG, "Mixer region %d failed ret %d", i, mix_ret);
                ret = mix_ret;
                break;
            }
        }
    }
    ret = esp_gmf_port_acquire_out(out_port, &out_load, in_load->valid_size, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, ret, ret, esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY); return ret);
    esp_gmf_port_release_out(out_port, out_load, ESP_GMF_MAX_DELAY);
    esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY);
    if (out_load->is_done) {
        ret = ESP_GMF_JOB_ERR_DONE;
    }
    return ret;
}

static esp_gmf_job_err_t gmf_vid_overlay_close(esp_gmf_element_handle_t self, void *para)
{
    gmf_vid_overlay_t *overlay_mixer = (gmf_vid_overlay_t *)self;
    overlay_disable(overlay_mixer);
#if CONFIG_SOC_PPA_SUPPORTED
    hw_mixer_close(overlay_mixer);
#endif  /* CONFIG_SOC_PPA_SUPPORTED */
    overlay_mixer->is_open = false;
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_err_t gmf_vid_overlay_destroy(esp_gmf_element_handle_t self)
{
    gmf_vid_overlay_t *overlay_mixer = (gmf_vid_overlay_t *)self;
    esp_gmf_video_el_deinit(self);
    if (overlay_mixer != NULL) {
#if CONFIG_SOC_PPA_SUPPORTED
        hw_mixer_close(overlay_mixer);
#endif  /* CONFIG_SOC_PPA_SUPPORTED */
        esp_gmf_oal_free(overlay_mixer->overlay_rgn);
        esp_gmf_oal_free(overlay_mixer);
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_video_overlay_set_overlay_port(esp_gmf_element_handle_t self, esp_gmf_port_handle_t port)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, port, return ESP_GMF_ERR_INVALID_ARG);
    const esp_gmf_method_t *method_head = NULL;
    const esp_gmf_method_t *method = NULL;
    esp_gmf_element_get_method(self, &method_head);
    esp_gmf_method_found(method_head, VMETHOD(OVERLAY, SET_PORT), &method);
    uint8_t buf[sizeof(esp_gmf_port_handle_t)];
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(OVERLAY, SET_PORT, PORT), buf, (uint8_t *)&port, sizeof(esp_gmf_port_handle_t));
    return esp_gmf_element_exe_method(self, VMETHOD(OVERLAY, SET_PORT), buf, sizeof(buf));
}

esp_gmf_err_t esp_gmf_video_overlay_set_alpha(esp_gmf_element_handle_t self, uint8_t alpha)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    const esp_gmf_method_t *method_head = NULL;
    const esp_gmf_method_t *method = NULL;
    esp_gmf_element_get_method(self, &method_head);
    esp_gmf_method_found(method_head, VMETHOD(OVERLAY, SET_ALPHA), &method);
    uint8_t buf[sizeof(uint8_t)];
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(OVERLAY, SET_ALPHA, ALPHA), buf, (uint8_t *)&alpha, sizeof(uint8_t));
    return esp_gmf_element_exe_method(self, VMETHOD(OVERLAY, SET_ALPHA), buf, sizeof(buf));
}

esp_gmf_err_t esp_gmf_video_overlay_set_rgn(esp_gmf_element_handle_t self, esp_gmf_overlay_rgn_info_t *rgn_info)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, rgn_info, return ESP_GMF_ERR_INVALID_ARG);
    const esp_gmf_method_t *method_head = NULL;
    const esp_gmf_method_t *method = NULL;
    esp_gmf_element_get_method(self, &method_head);
    esp_gmf_method_found(method_head, VMETHOD(OVERLAY, SET_RGN), &method);
    uint8_t buf[17];
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(OVERLAY, SET_RGN, FMT), buf, (uint8_t *)&rgn_info->format_id, sizeof(uint32_t));
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(OVERLAY, SET_RGN, X), buf, (uint8_t *)&rgn_info->dst_rgn.x, sizeof(uint16_t));
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(OVERLAY, SET_RGN, Y), buf, (uint8_t *)&rgn_info->dst_rgn.y, sizeof(uint16_t));
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(OVERLAY, SET_RGN, WIDTH), buf, (uint8_t *)&rgn_info->dst_rgn.width, sizeof(uint16_t));
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(OVERLAY, SET_RGN, HEIGHT), buf, (uint8_t *)&rgn_info->dst_rgn.height, sizeof(uint16_t));
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(OVERLAY, SET_RGN, RGN_INDEX), buf, (uint8_t *)&rgn_info->rgn_index, sizeof(uint8_t));
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(OVERLAY, SET_RGN, HAS_TRANS_COLOR), buf, (uint8_t *)&rgn_info->has_trans_color, sizeof(uint8_t));
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(OVERLAY, SET_RGN, TRANS_COLOR), buf, rgn_info->trans_color, sizeof(rgn_info->trans_color));
    return esp_gmf_element_exe_method(self, VMETHOD(OVERLAY, SET_RGN), buf, sizeof(buf));
}

esp_gmf_err_t esp_gmf_video_overlay_enable(esp_gmf_element_handle_t self, bool enable)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    const esp_gmf_method_t *method_head = NULL;
    const esp_gmf_method_t *method = NULL;
    esp_gmf_element_get_method(self, &method_head);
    esp_gmf_method_found(method_head, VMETHOD(OVERLAY, OVERLAY_ENABLE), &method);
    uint8_t buf[sizeof(bool)];
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(OVERLAY, OVERLAY_ENABLE, ENABLE), buf, (uint8_t *)&enable, sizeof(bool));
    return esp_gmf_element_exe_method(self, VMETHOD(OVERLAY, OVERLAY_ENABLE), buf, sizeof(buf));
}

static esp_gmf_err_t set_mixer_enable(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                      uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    gmf_vid_overlay_t *overlay_mixer = (gmf_vid_overlay_t *)handle;
    overlay_mixer->enable = *(bool *)buf;
    esp_gmf_err_t ret = overlay_mixer->enable ? overlay_enable(overlay_mixer) : overlay_disable(overlay_mixer);
    if (overlay_mixer->enable == false) {
        if (overlay_mixer->overlay_rgn) {
            esp_gmf_oal_free(overlay_mixer->overlay_rgn);
            overlay_mixer->overlay_rgn = NULL;
        }
        overlay_mixer->rgn_count = 0;
    }
    return ret;
}

static esp_gmf_err_t set_mixer_rgn(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                   uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    gmf_vid_overlay_t *overlay_mixer = (gmf_vid_overlay_t *)handle;
    esp_gmf_overlay_rgn_info_t overlay_rgn = {0};
    overlay_rgn.format_id = *(uint32_t *)buf;
    buf += sizeof(uint32_t);
    overlay_rgn.dst_rgn.x = *(uint16_t *)(buf);
    overlay_rgn.dst_rgn.y = *(uint16_t *)(buf + 2);
    overlay_rgn.dst_rgn.width = *(uint16_t *)(buf + 4);
    overlay_rgn.dst_rgn.height = *(uint16_t *)(buf + 6);
    overlay_rgn.rgn_index = *(uint8_t *)(buf + 8);
    overlay_rgn.has_trans_color = (bool)*(uint8_t *)(buf + 9);
    overlay_rgn.trans_color[0] = *(uint8_t *)(buf + 10);
    overlay_rgn.trans_color[1] = *(uint8_t *)(buf + 11);
    overlay_rgn.trans_color[2] = *(uint8_t *)(buf + 12);
    if (overlay_rgn.rgn_index + 1 > overlay_mixer->rgn_count) {
        int size = sizeof(esp_gmf_overlay_rgn_info_t) * (overlay_rgn.rgn_index + 1);
        esp_gmf_overlay_rgn_info_t *new_rgn = (esp_gmf_overlay_rgn_info_t *)esp_gmf_oal_realloc(overlay_mixer->overlay_rgn, size);
        if (new_rgn == NULL) {
            return ESP_GMF_ERR_MEMORY_LACK;
        }
        memset(&new_rgn[overlay_mixer->rgn_count], 0, overlay_rgn.rgn_index + 1 - overlay_mixer->rgn_count);
        overlay_mixer->overlay_rgn = new_rgn;
        overlay_mixer->rgn_count = overlay_rgn.rgn_index + 1;
    }
    memcpy(&overlay_mixer->overlay_rgn[overlay_rgn.rgn_index], &overlay_rgn, sizeof(esp_gmf_overlay_rgn_info_t));
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t set_mixer_port(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                    uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    gmf_vid_overlay_t *overlay_mixer = (gmf_vid_overlay_t *)handle;
    overlay_mixer->overlay_port = *(esp_gmf_port_handle_t *)buf;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t set_mixer_alpha(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                     uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    gmf_vid_overlay_t *overlay_mixer = (gmf_vid_overlay_t *)handle;
    overlay_mixer->window_alpha = *(uint8_t *)buf;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t gmf_vid_overlay_load_methods(esp_gmf_element_handle_t handle)
{
    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_method_t *methods = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    do {
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(OVERLAY, OVERLAY_ENABLE, ENABLE), ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_method_append(&methods, VMETHOD(OVERLAY, OVERLAY_ENABLE), set_mixer_enable, set_args);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        set_args = NULL;
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(OVERLAY, SET_RGN, FMT), ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), 0);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(OVERLAY, SET_RGN, X), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 4);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(OVERLAY, SET_RGN, Y), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 6);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(OVERLAY, SET_RGN, WIDTH), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 8);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(OVERLAY, SET_RGN, HEIGHT), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 10);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(OVERLAY, SET_RGN, RGN_INDEX), ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 12);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(OVERLAY, SET_RGN, HAS_TRANS_COLOR), ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 13);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(OVERLAY, SET_RGN, TRANS_COLOR), ESP_GMF_ARGS_TYPE_UINT8, 3, 14);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_method_append(&methods, VMETHOD(OVERLAY, SET_RGN), set_mixer_rgn, set_args);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        set_args = NULL;
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(OVERLAY, SET_PORT, PORT), ESP_GMF_ARGS_TYPE_UINT32, sizeof(intptr_t), 0);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_method_append(&methods, VMETHOD(OVERLAY, SET_PORT), set_mixer_port, set_args);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        set_args = NULL;
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(OVERLAY, SET_ALPHA, ALPHA), ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_method_append(&methods, VMETHOD(OVERLAY, SET_ALPHA), set_mixer_alpha, set_args);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        ((esp_gmf_element_t *) handle)->method = methods;
        return ESP_GMF_ERR_OK;
    } while (0);
    ESP_LOGE(TAG, "Fail to load methods");
    if (set_args) {
        esp_gmf_args_desc_destroy(set_args);
    }
    if (methods) {
        esp_gmf_method_destroy(methods);
    }
    return ESP_GMF_ERR_MEMORY_LACK;
}

static esp_gmf_err_t gmf_vid_overlay_load_caps(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t cap = {0};
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    do {
        cap.cap_eightcc = ESP_GMF_CAPS_VIDEO_OVERLAY;
        cap.attr_fun = NULL;
        ret = esp_gmf_cap_append(&caps, &cap);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        ((esp_gmf_element_t *) handle)->caps = caps;
        return ret;
    } while (0);
    if (caps) {
        esp_gmf_cap_destroy(caps);
    }
    return ret;
}

static esp_gmf_err_t gmf_vid_overlay_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_video_overlay_init(cfg, (esp_gmf_element_handle_t *)handle);
}

esp_gmf_err_t esp_gmf_video_overlay_init(void *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_MEM_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    gmf_vid_overlay_t *overlay_mixer = esp_gmf_oal_calloc(1, sizeof(gmf_vid_overlay_t));
    ESP_GMF_MEM_CHECK(TAG, overlay_mixer, return ESP_GMF_ERR_MEMORY_LACK);
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)overlay_mixer;
    obj->new_obj = gmf_vid_overlay_new;
    obj->del_obj = gmf_vid_overlay_destroy;

    int ret = esp_gmf_obj_set_tag(obj, "vid_overlay");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto _overlay_init_fail, "Failed set OBJ tag");

    uint8_t align = esp_gmf_oal_get_spiram_cache_align();
    esp_gmf_element_cfg_t el_cfg = {
        .dependency = true,
    };
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, align, align,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, align, align,
                                      ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ret = esp_gmf_video_el_init(obj, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto _overlay_init_fail, "Failed init video rate convert");

    overlay_mixer->parent.base.ops.open = gmf_vid_overlay_open;
    overlay_mixer->parent.base.ops.process = gmf_vid_overlay_process;
    overlay_mixer->parent.base.ops.close = gmf_vid_overlay_close;
    overlay_mixer->parent.base.ops.event_receiver = esp_gmf_video_handle_events;
    overlay_mixer->parent.base.ops.load_methods = gmf_vid_overlay_load_methods;
    overlay_mixer->parent.base.ops.load_caps = gmf_vid_overlay_load_caps;

    *handle = obj;
    ESP_LOGI(TAG, "Create video overlay, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;

_overlay_init_fail:
    esp_gmf_obj_delete(obj);
    return ret;
}

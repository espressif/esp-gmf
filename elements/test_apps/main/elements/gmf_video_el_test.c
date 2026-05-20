
/**
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "esp_gmf_video_ppa.h"
#include "esp_gmf_video_enc.h"
#include "esp_gmf_video_dec.h"
#include "esp_gmf_video_fps_cvt.h"
#include "esp_gmf_video_overlay.h"
#include "esp_gmf_pool.h"
#include "esp_video_enc_default.h"
#include "esp_video_dec_default.h"
#include "esp_video_codec_utils.h"
#include "gmf_video_pattern.h"
#include "esp_gmf_video_param.h"
#include "esp_gmf_caps_def.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_gmf_video_scale.h"
#include "esp_gmf_video_crop.h"
#include "esp_gmf_video_rotate.h"
#include "esp_gmf_video_color_convert.h"
#include "gmf_loader_setup_defaults.h"
#include "esp_fourcc.h"

/** Declared in gmf_video_common.c (not always visible via test app includes). */
const char *esp_gmf_video_get_format_string(uint32_t codec);
int gmf_video_ppa_test(uint32_t from_codec, int32_t to_codec, uint32_t width, uint32_t height, uint8_t *src, uint8_t *dst, int v);
#if CONFIG_SOC_PPA_SUPPORTED
esp_gmf_err_t gmf_video_ppa_blend_probe(uint32_t src_fmt, uint32_t dst_fmt, uint32_t width, uint32_t height);
bool esp_gmf_video_overlay_hw_blend_supported(uint32_t dst_fmt);
#endif  /* CONFIG_SOC_PPA_SUPPORTED */
bool esp_gmf_video_overlay_dst_format_supported(uint32_t format_id);

#define TAG  "VID_EL_TEST"

#if CONFIG_IDF_TARGET_ESP32P4
#define TEST_PATTERN_WIDTH   (1280)
#define TEST_PATTERN_HEIGHT  (720)
#else
#define TEST_PATTERN_WIDTH   (320)
#define TEST_PATTERN_HEIGHT  (240)
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
#define TEST_PATTERN_VERTICAL   (false)
#define TEST_PATTERN_BAR_COUNT  (8)
#define TEST_VIDEO_ALIGNMENT    (128)

/** Region pixels < HW_BLEND_MIN_PIXELS (100*100) forces software overlay path. */
#define OVERLAY_SW_FRAME_WIDTH   (160)
#define OVERLAY_SW_FRAME_HEIGHT  (120)
#if CONFIG_IDF_TARGET_ESP32P4
#define OVERLAY_HW_FRAME_WIDTH   (640)
#define OVERLAY_HW_FRAME_HEIGHT  (480)
#else
#define OVERLAY_HW_FRAME_WIDTH   (320)
#define OVERLAY_HW_FRAME_HEIGHT  (240)
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
#define OVERLAY_HW_BLEND_MIN_PIXELS  (100 * 100)
#define OVERLAY_CASE_TAG_LEN         (64)

#define SAFE_FREE(ptr)  if (ptr) {  \
    esp_gmf_oal_free(ptr);          \
    ptr = NULL;                     \
}

#define ELEMS(arr)               (sizeof(arr) / sizeof((arr)[0]))
#define VIDEO_EL_MAX_STACK_SIZE  (40 * 1024)

typedef struct {
    // Src information
    uint32_t                    in_frame_count;
    uint32_t                    src_codec;
    esp_gmf_video_resolution_t  src_res;
    uint8_t                    *src_pixel;
    uint32_t                    src_size;
    uint8_t                    *src_copy;

    // Overlay information
    uint8_t                    *overlay_data;
    uint32_t                    overlay_size;
    esp_gmf_video_resolution_t  overlay_res;

    // Out information
    uint16_t                    rotate_degree;
    uint32_t                    out_codec;
    esp_gmf_video_resolution_t  out_res;
    uint8_t                    *out_pixel;
    uint32_t                    out_size;
    uint32_t                    out_frame_count;
    uint32_t                    out_max_size;
    bool                        no_need_free;
} video_el_test_t;

typedef struct {
    esp_gmf_pool_handle_t      pool;
    esp_gmf_pipeline_handle_t  pipe;
    esp_gmf_task_handle_t      task;
    esp_gmf_element_handle_t   convert_hd;
    esp_gmf_element_handle_t   enc_hd;
    esp_gmf_element_handle_t   rate_hd;
    esp_gmf_element_handle_t   overlay_hd;
    esp_gmf_element_handle_t   dec_hd;
} convert_res_t;

static int prepare_pool(convert_res_t *res);
static int prepare_convert_pipeline(convert_res_t *res, const char **elements);
static void release_convert_pipeline(convert_res_t *res);

typedef struct {
    esp_gmf_element_handle_t  dec_hd;
    uint32_t                  compressed_fourcc;
    bool                      saw_compressed_report_from_dec;
} vid_dec_open_report_probe_t;

static esp_gmf_err_t vid_dec_open_report_probe_cb(esp_gmf_event_pkt_t *pkt, void *ctx)
{
    vid_dec_open_report_probe_t *p = (vid_dec_open_report_probe_t *)ctx;
    if (p == NULL || p->dec_hd == NULL) {
        return ESP_GMF_ERR_OK;
    }
    if (pkt->type != ESP_GMF_EVT_TYPE_REPORT_INFO || pkt->sub != ESP_GMF_INFO_VIDEO) {
        return ESP_GMF_ERR_OK;
    }
    if (pkt->from != p->dec_hd) {
        return ESP_GMF_ERR_OK;
    }
    if (pkt->payload == NULL || pkt->payload_size < (int)sizeof(esp_gmf_info_video_t)) {
        return ESP_GMF_ERR_OK;
    }
    const esp_gmf_info_video_t *v = (const esp_gmf_info_video_t *)pkt->payload;
    if (v->format_id == p->compressed_fourcc) {
        p->saw_compressed_report_from_dec = true;
    }
    return ESP_GMF_ERR_OK;
}

typedef struct {
    esp_gmf_element_handle_t  enc_hd;
    uint32_t                  vid_report_count_from_enc;
} vid_enc_open_report_probe_t;

static esp_gmf_err_t vid_enc_open_report_probe_cb(esp_gmf_event_pkt_t *pkt, void *ctx)
{
    vid_enc_open_report_probe_t *p = (vid_enc_open_report_probe_t *)ctx;
    if (p == NULL || p->enc_hd == NULL) {
        return ESP_GMF_ERR_OK;
    }
    if (pkt->type != ESP_GMF_EVT_TYPE_REPORT_INFO || pkt->sub != ESP_GMF_INFO_VIDEO) {
        return ESP_GMF_ERR_OK;
    }
    if (pkt->from != p->enc_hd) {
        return ESP_GMF_ERR_OK;
    }
    p->vid_report_count_from_enc++;
    return ESP_GMF_ERR_OK;
}

static video_el_test_t video_el_inst;

static esp_gmf_element_handle_t get_element_by_caps_from_pool(esp_gmf_pool_handle_t pool, uint64_t caps_cc)
{
    const void *iter = NULL;
    esp_gmf_element_handle_t element = NULL;
    while (esp_gmf_pool_iterate_element(pool, &iter, &element) == ESP_GMF_ERR_OK) {
        const esp_gmf_cap_t *caps = NULL;
        esp_gmf_element_get_caps(element, &caps);
        while (caps) {
            if (caps->cap_eightcc == caps_cc) {
                return element;
            }
            caps = caps->next;
        }
    }
    return NULL;
}

static esp_gmf_element_handle_t get_element_by_caps_from_pipe(esp_gmf_pipeline_handle_t pipe, uint64_t caps_cc)
{
    const esp_gmf_cap_t *caps = NULL;
    esp_gmf_element_handle_t element = NULL;
    esp_gmf_pipeline_get_head_el(pipe, &element);

    for (; element; esp_gmf_pipeline_get_next_el(pipe, element, &element)) {
        esp_gmf_element_get_caps(element, &caps);
        for (; caps; caps = caps->next) {
            if (caps->cap_eightcc == caps_cc) {
                return element;
            }
        }
    }
    return NULL;
}

static void get_pattern_info(pattern_info_t *info, bool is_out)
{
    if (is_out == false) {
        info->format_id = video_el_inst.src_codec;
        info->res = video_el_inst.src_res;
        info->pixel = video_el_inst.src_pixel;
        info->data_size = video_el_inst.src_size;
        info->bar_count = TEST_PATTERN_BAR_COUNT;
        info->vertical = TEST_PATTERN_VERTICAL;
    } else {
        info->format_id = video_el_inst.out_codec;
        info->res = video_el_inst.out_res;
        info->pixel = video_el_inst.out_pixel;
        info->data_size = video_el_inst.out_size;
        info->bar_count = TEST_PATTERN_BAR_COUNT;
        if (video_el_inst.rotate_degree == 90 || video_el_inst.rotate_degree == 270) {
            info->vertical = !TEST_PATTERN_VERTICAL;
        }
    }
}

static int allocate_src_pattern(uint32_t src_codec, bool copy)
{
    if (video_el_inst.src_res.width == 0 || video_el_inst.src_res.height == 0) {
        video_el_inst.src_res.width = TEST_PATTERN_WIDTH;
        video_el_inst.src_res.height = TEST_PATTERN_HEIGHT;
    }
    video_el_inst.src_codec = src_codec;
    esp_video_codec_resolution_t res = {
        .width = video_el_inst.src_res.width,
        .height = video_el_inst.src_res.height,
    };
    video_el_inst.src_size = esp_video_codec_get_image_size((esp_video_codec_pixel_fmt_t)src_codec, &res);
    video_el_inst.src_pixel = esp_gmf_oal_malloc_align(TEST_VIDEO_ALIGNMENT, video_el_inst.src_size);
    if (video_el_inst.src_pixel == NULL) {
        return -1;
    }
    pattern_info_t pattern_info = {};
    get_pattern_info(&pattern_info, false);
    gen_pattern_color_bar(&pattern_info);
    return 0;
}

static void show_result_pattern(void)
{
    pattern_info_t src_info = {};
    pattern_info_t out_info = {};
    get_pattern_info(&src_info, false);
    get_pattern_info(&out_info, true);
    draw_convert_result(&src_info, &out_info);
}

static void free_video_el_inst(void)
{
    SAFE_FREE(video_el_inst.src_pixel);
    SAFE_FREE(video_el_inst.src_copy);
    SAFE_FREE(video_el_inst.overlay_data);
    if (video_el_inst.no_need_free == false) {
        SAFE_FREE(video_el_inst.out_pixel);
    }
    video_el_inst.out_max_size = 0;
    video_el_inst.out_pixel = NULL;
}

static esp_gmf_err_io_t in_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    load->pts = video_el_inst.in_frame_count * 100;
    load->buf = video_el_inst.src_pixel;
    load->valid_size = video_el_inst.src_size;
    load->buf_length = load->valid_size;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t in_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    video_el_inst.in_frame_count++;
    vTaskDelay(10 / portTICK_RATE_MS);
    return ESP_GMF_IO_OK;
}

#define PACK_RGB565(r, g, b)  \
    ((uint16_t)((((r) >> 3) & 0x1F) << 11) | ((((g) >> 2) & 0x3F) << 5) | (((b) >> 3) & 0x1F))

#define OVERLAY_TRANS_KEY_R  0
#define OVERLAY_TRANS_KEY_G  255
#define OVERLAY_TRANS_KEY_B  0
#define OVERLAY_TRANS_FG_R   255
#define OVERLAY_TRANS_FG_G   0
#define OVERLAY_TRANS_FG_B   0

/** Center quarter of overlay window: transparent key; outer ring: foreground. */
typedef struct {
    int  cx0;
    int  cy0;
    int  cx1;
    int  cy1;
} overlay_trans_rect_t;

static overlay_trans_rect_t overlay_trans_center_rect(void)
{
    int ow = video_el_inst.overlay_res.width;
    int oh = video_el_inst.overlay_res.height;
    overlay_trans_rect_t r = {
        .cx0 = ow / 4,
        .cy0 = oh / 4,
        .cx1 = ow - ow / 4,
        .cy1 = oh - oh / 4,
    };
    return r;
}

typedef struct {
    uint8_t                     alpha;
    esp_gmf_overlay_rgn_info_t  rgn;
} overlay_test_cfg_t;

typedef struct {
    overlay_test_cfg_t *cfgs;
    int                 cfg_count;
    int                 acquire_idx;
} overlay_port_ctx_t;

static overlay_port_ctx_t s_overlay_port_ctx;

static esp_gmf_err_io_t overlay_test_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    overlay_port_ctx_t *ctx = (overlay_port_ctx_t *)handle;
    overlay_test_cfg_t *cfg = &ctx->cfgs[ctx->acquire_idx];
    load->pts = cfg->alpha;
    load->buf = video_el_inst.overlay_data;
    load->valid_size = video_el_inst.overlay_size;
    load->buf_length = video_el_inst.overlay_size;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t overlay_test_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    overlay_port_ctx_t *ctx = (overlay_port_ctx_t *)handle;
    ctx->acquire_idx++;
    if (ctx->acquire_idx >= ctx->cfg_count) {
        ctx->acquire_idx = 0;
    }
    return ESP_GMF_IO_OK;
}

static uint16_t read_frame_rgb565(const uint8_t *buf, int x, int y, bool byte_swap)
{
    const uint16_t *base = (const uint16_t *)buf;
    uint16_t px = base[y * video_el_inst.out_res.width + x];
    return byte_swap ? __builtin_bswap16(px) : px;
}

static const uint8_t *overlay_result_buf(void)
{
    return video_el_inst.out_pixel ? video_el_inst.out_pixel : video_el_inst.src_pixel;
}

static uint16_t read_out_rgb565(int x, int y, bool byte_swap)
{
    return read_frame_rgb565(overlay_result_buf(), x, y, byte_swap);
}

static void overlay_log_sample(const char *tag, const char *label, int x, int y, uint16_t src_px, uint16_t out_px)
{
    ESP_LOGI(TAG, "[%s] %s (%d,%d) src=0x%04x out=0x%04x %s", tag, label, x, y, src_px, out_px,
             (src_px == out_px) ? "SAME" : "DIFF");
}

typedef struct {
    int         x;
    int         y;
    const char *label;
    bool        expect_same_as_src;
    uint16_t    expect_px;
    bool        has_expect_px;
} overlay_sample_point_t;

static void overlay_check_sample(const char *tag, const overlay_sample_point_t *pt, const uint8_t *src_ref, bool byte_swap)
{
    uint16_t src_px = read_frame_rgb565(src_ref, pt->x, pt->y, byte_swap);
    uint16_t out_px = read_out_rgb565(pt->x, pt->y, byte_swap);
    overlay_log_sample(tag, pt->label, pt->x, pt->y, src_px, out_px);
    if (pt->has_expect_px) {
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(pt->expect_px, out_px, pt->label);
    } else if (pt->expect_same_as_src) {
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(src_px, out_px, pt->label);
    } else {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(src_px, out_px, pt->label);
    }
}

static int overlay_outside_x(const esp_gmf_video_rgn_t *rgn)
{
    if (rgn->x >= 8) {
        return rgn->x - 8;
    }
    if (rgn->x + rgn->width + 8 < (int)video_el_inst.out_res.width) {
        return rgn->x + rgn->width + 8;
    }
    return rgn->x + rgn->width / 2;
}

static void verify_overlay_pipeline_ran(const char *case_name)
{
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, video_el_inst.in_frame_count,
                                            "overlay pipeline must read input frame");
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, video_el_inst.out_frame_count,
                                            "overlay pipeline must write output frame");
    ESP_LOGI(TAG, "[%s] frames in=%u out=%u", case_name,
             (unsigned)video_el_inst.in_frame_count, (unsigned)video_el_inst.out_frame_count);
}

static void verify_overlay_alpha_samples(const char *tag, const esp_gmf_video_rgn_t *rgn, const uint8_t *src_ref,
                                         bool byte_swap)
{
    overlay_sample_point_t pts[] = {
        {
            .x = overlay_outside_x(rgn),
            .y = rgn->y + rgn->height / 2,
            .label = "outside",
            .expect_same_as_src = true,
        },
        {
            .x = rgn->x + rgn->width / 4,
            .y = rgn->y + rgn->height / 4,
            .label = "inside_ring",
            .expect_same_as_src = false,
        },
        {
            .x = rgn->x + rgn->width * 3 / 4,
            .y = rgn->y + rgn->height * 3 / 4,
            .label = "inside_corner",
            .expect_same_as_src = false,
        },
    };
    for (size_t i = 0; i < ELEMS(pts); i++) {
        overlay_check_sample(tag, &pts[i], src_ref, byte_swap);
    }
}

static void verify_overlay_trans_samples(const char *tag, const esp_gmf_video_rgn_t *rgn, const uint8_t *src_ref,
                                         bool byte_swap)
{
    overlay_trans_rect_t ctr = overlay_trans_center_rect();
    const uint16_t fg_px = PACK_RGB565(OVERLAY_TRANS_FG_R, OVERLAY_TRANS_FG_G, OVERLAY_TRANS_FG_B);
    int ow = video_el_inst.overlay_res.width;
    int oh = video_el_inst.overlay_res.height;
    int center_ox = (ctr.cx0 + ctr.cx1) / 2;
    int center_oy = (ctr.cy0 + ctr.cy1) / 2;
    int ring_ox = ctr.cx0 / 2;
    int ring_oy = ctr.cy0 / 2;

    overlay_sample_point_t pts[] = {
        {
            .x = overlay_outside_x(rgn),
            .y = rgn->y + rgn->height / 2,
            .label = "outside",
            .expect_same_as_src = true,
        },
        {
            .x = rgn->x + ring_ox,
            .y = rgn->y + ring_oy,
            .label = "outer_ring",
            .has_expect_px = true,
            .expect_px = fg_px,
        },
        {
            .x = rgn->x + center_ox,
            .y = rgn->y + center_oy,
            .label = "trans_center",
            .expect_same_as_src = true,
        },
    };
    (void)ow;
    (void)oh;
    for (size_t i = 0; i < ELEMS(pts); i++) {
        overlay_check_sample(tag, &pts[i], src_ref, byte_swap);
    }
}

static bool overlay_px_in_trans_center(int ox, int oy, const overlay_trans_rect_t *ctr)
{
    return ox >= ctr->cx0 && ox < ctr->cx1 && oy >= ctr->cy0 && oy < ctr->cy1;
}

typedef enum {
    OVERLAY_VERIFY_NONE   = 0,
    OVERLAY_VERIFY_PIXELS = 1,
} overlay_verify_mode_t;

typedef enum {
    OVERLAY_SCENARIO_ALPHA    = 0,
    OVERLAY_SCENARIO_TRANS    = 1,
    OVERLAY_SCENARIO_DUAL     = 2,
    OVERLAY_SCENARIO_RGB565BE = 3,
    OVERLAY_SCENARIO_OUYY     = 4,
} overlay_scenario_id_t;

typedef void (*overlay_fill_fn_t)(void);

static void fill_overlay_color_bar(void);
static void fill_overlay_trans_key(void);

typedef struct {
    const char            *name;
    overlay_scenario_id_t  id;
    uint32_t               dst_fmt;
    overlay_fill_fn_t      fill_fn;
    overlay_verify_mode_t  verify;
    bool                   run_in_hw_suite;
} overlay_scenario_desc_t;

static uint32_t overlay_rgn_pixels(const esp_gmf_video_rgn_t *rgn)
{
    return (uint32_t)rgn->width * rgn->height;
}

static void overlay_scenario_build_cfgs(overlay_scenario_id_t id, uint32_t fw, uint32_t fh,
                                        overlay_test_cfg_t *cfgs, int *count)
{
    *count = 1;
    memset(cfgs, 0, sizeof(overlay_test_cfg_t) * 2);
    switch (id) {
        case OVERLAY_SCENARIO_ALPHA:
            cfgs[0].alpha = 200;
            cfgs[0].rgn.rgn_index = 0;
            cfgs[0].rgn.has_trans_color = false;
            cfgs[0].rgn.dst_rgn = (esp_gmf_video_rgn_t) {
                .x = fw / 4,
                .y = fh / 4,
                .width = fw / 2,
                .height = fh / 2,
            };
            break;
        case OVERLAY_SCENARIO_TRANS:
            cfgs[0].alpha = 255;
            cfgs[0].rgn.rgn_index = 0;
            cfgs[0].rgn.has_trans_color = true;
            cfgs[0].rgn.trans_color[0] = OVERLAY_TRANS_KEY_R;
            cfgs[0].rgn.trans_color[1] = OVERLAY_TRANS_KEY_G;
            cfgs[0].rgn.trans_color[2] = OVERLAY_TRANS_KEY_B;
            cfgs[0].rgn.dst_rgn = (esp_gmf_video_rgn_t) {
                .x = fw / 4,
                .y = fh / 4,
                .width = fw / 2,
                .height = fh / 2,
            };
            break;
        case OVERLAY_SCENARIO_DUAL:
            *count = 2;
            cfgs[0].alpha = 200;
            cfgs[0].rgn.rgn_index = 0;
            cfgs[0].rgn.has_trans_color = false;
            cfgs[0].rgn.dst_rgn = (esp_gmf_video_rgn_t) {
                .x = 0,
                .y = 0,
                .width = fw / 2,
                .height = fh / 2,
            };
            cfgs[1].alpha = 255;
            cfgs[1].rgn.rgn_index = 1;
            cfgs[1].rgn.has_trans_color = true;
            cfgs[1].rgn.trans_color[0] = OVERLAY_TRANS_KEY_R;
            cfgs[1].rgn.trans_color[1] = OVERLAY_TRANS_KEY_G;
            cfgs[1].rgn.trans_color[2] = OVERLAY_TRANS_KEY_B;
            cfgs[1].rgn.dst_rgn = (esp_gmf_video_rgn_t) {
                .x = fw / 2,
                .y = fh / 2,
                .width = fw / 2,
                .height = fh / 2,
            };
            break;
        case OVERLAY_SCENARIO_RGB565BE:
            cfgs[0].alpha = 200;
            cfgs[0].rgn.rgn_index = 0;
            cfgs[0].rgn.has_trans_color = false;
            cfgs[0].rgn.format_id = ESP_FOURCC_RGB16_BE;
            cfgs[0].rgn.dst_rgn = (esp_gmf_video_rgn_t) {
                .x = fw / 4,
                .y = fh / 4,
                .width = fw / 2,
                .height = fh / 2,
            };
            break;
        case OVERLAY_SCENARIO_OUYY:
            cfgs[0].alpha = 255;
            cfgs[0].rgn.rgn_index = 0;
            cfgs[0].rgn.has_trans_color = true;
            cfgs[0].rgn.trans_color[0] = OVERLAY_TRANS_KEY_R;
            cfgs[0].rgn.trans_color[1] = OVERLAY_TRANS_KEY_G;
            cfgs[0].rgn.trans_color[2] = OVERLAY_TRANS_KEY_B;
            cfgs[0].rgn.format_id = ESP_FOURCC_OUYY_EVYY;
            cfgs[0].rgn.dst_rgn = (esp_gmf_video_rgn_t) {
                .x = fw / 4,
                .y = fh / 4,
                .width = fw / 2,
                .height = fh / 2,
            };
            break;
        default:
            *count = 0;
            break;
    }
}

static const overlay_scenario_desc_t s_overlay_scenarios[] = {
    {"alpha_rgb565", OVERLAY_SCENARIO_ALPHA, ESP_FOURCC_RGB16, fill_overlay_color_bar, OVERLAY_VERIFY_PIXELS, true},
    {"trans_rgb565", OVERLAY_SCENARIO_TRANS, ESP_FOURCC_RGB16, fill_overlay_trans_key, OVERLAY_VERIFY_PIXELS, true},
    {"dual_region", OVERLAY_SCENARIO_DUAL, ESP_FOURCC_RGB16, fill_overlay_trans_key, OVERLAY_VERIFY_PIXELS, true},
    {"alpha_rgb565be", OVERLAY_SCENARIO_RGB565BE, ESP_FOURCC_RGB16_BE, fill_overlay_color_bar, OVERLAY_VERIFY_PIXELS, true},
    {"trans_ouyy", OVERLAY_SCENARIO_OUYY, ESP_FOURCC_OUYY_EVYY, fill_overlay_trans_key, OVERLAY_VERIFY_NONE, true},
};

static void verify_overlay_regions(overlay_test_cfg_t *cfgs, int cfg_count, uint32_t dst_format,
                                   const uint8_t *src_ref, const char *case_name)
{
    if (dst_format != ESP_FOURCC_RGB16 && dst_format != ESP_FOURCC_RGB16_BE) {
        ESP_LOGW(TAG, "[%s] RGB565 sample checks skipped for %s (inspect serial pattern below)",
                 case_name, esp_gmf_video_get_format_string(dst_format));
        return;
    }
    bool byte_swap = (dst_format == ESP_FOURCC_RGB16_BE);
    for (int i = 0; i < cfg_count; i++) {
        const esp_gmf_video_rgn_t *rgn = &cfgs[i].rgn.dst_rgn;
        char tag[OVERLAY_CASE_TAG_LEN];
        snprintf(tag, sizeof(tag), "%.*s_r%d", (int)(sizeof(tag) - 8), case_name, i);
        if (cfgs[i].rgn.has_trans_color) {
            verify_overlay_trans_samples(tag, rgn, src_ref, byte_swap);
        } else if (cfgs[i].alpha > 0) {
            verify_overlay_alpha_samples(tag, rgn, src_ref, byte_swap);
        }
    }
}

static void run_overlay_test(overlay_test_cfg_t *cfgs, int cfg_count, overlay_verify_mode_t verify_mode,
                             overlay_fill_fn_t fill_fn, uint32_t dst_format, uint32_t frame_w, uint32_t frame_h,
                             const char *suite_tag, const char *case_name)
{
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    video_el_inst.src_res.width = frame_w;
    video_el_inst.src_res.height = frame_h;
    prepare_pool(&res);
    const char *name[] = {"vid_overlay", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, name));

    TEST_ASSERT_EQUAL(0, allocate_src_pattern(dst_format, false));
    TEST_ASSERT_TRUE_MESSAGE(esp_gmf_video_overlay_dst_format_supported(dst_format),
                             "unsupported overlay destination format");

    video_el_inst.overlay_res.width = video_el_inst.src_res.width / 2;
    video_el_inst.overlay_res.height = video_el_inst.src_res.height / 2;
    esp_video_codec_resolution_t overlay_res = {
        .width = video_el_inst.overlay_res.width,
        .height = video_el_inst.overlay_res.height,
    };
    video_el_inst.overlay_size = esp_video_codec_get_image_size((esp_video_codec_pixel_fmt_t)ESP_FOURCC_RGB16, &overlay_res);
    video_el_inst.overlay_data = esp_gmf_oal_malloc_align(TEST_VIDEO_ALIGNMENT, video_el_inst.overlay_size);
    TEST_ASSERT_NOT_NULL(video_el_inst.overlay_data);
    if (fill_fn) {
        fill_fn();
    }

    video_el_inst.out_res = video_el_inst.src_res;
    video_el_inst.out_codec = dst_format;

    s_overlay_port_ctx.cfgs = cfgs;
    s_overlay_port_ctx.cfg_count = cfg_count;
    s_overlay_port_ctx.acquire_idx = 0;

    for (int i = 0; i < cfg_count; i++) {
        if (cfgs[i].rgn.format_id == 0) {
            cfgs[i].rgn.format_id = dst_format;
        }
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_video_param_set_overlay_rgn(res.overlay_hd, &cfgs[i].rgn));
    }

    esp_gmf_port_handle_t overlay_port = NEW_ESP_GMF_PORT_IN_BLOCK(overlay_test_acquire, overlay_test_release,
                                                                   NULL, &s_overlay_port_ctx, 0, ESP_GMF_MAX_DELAY);
    esp_gmf_video_param_set_overlay_port(res.overlay_hd, overlay_port);
    esp_gmf_video_param_overlay_enable(res.overlay_hd, true);

    esp_gmf_info_video_t info = {
        .format_id = dst_format,
        .width = frame_w,
        .height = frame_h,
        .fps = 10,
    };
    esp_gmf_pipeline_report_info(res.pipe, ESP_GMF_INFO_VIDEO, &info, sizeof(info));
    esp_gmf_pipeline_reset(res.pipe);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(res.pipe));

    if (verify_mode == OVERLAY_VERIFY_PIXELS) {
        video_el_inst.src_copy = esp_gmf_oal_malloc_align(TEST_VIDEO_ALIGNMENT, video_el_inst.src_size);
        TEST_ASSERT_NOT_NULL(video_el_inst.src_copy);
        memcpy(video_el_inst.src_copy, video_el_inst.src_pixel, video_el_inst.src_size);
    }

    esp_gmf_err_t run_ret = esp_gmf_pipeline_run(res.pipe);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_GMF_ERR_OK, run_ret, "pipeline run failed");
    vTaskDelay(100 / portTICK_RATE_MS);
    esp_gmf_err_t stop_ret = esp_gmf_pipeline_stop(res.pipe);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_GMF_ERR_OK, stop_ret, "pipeline stop failed");
    vTaskDelay(50 / portTICK_RATE_MS);

    verify_overlay_pipeline_ran(case_name);
    if (verify_mode == OVERLAY_VERIFY_PIXELS && video_el_inst.src_copy) {
        verify_overlay_regions(cfgs, cfg_count, dst_format, video_el_inst.src_copy, case_name);
    }
    ESP_LOGI(TAG, "=== [%s/%s] PASS serial preview: src (left) | out (right) ===", suite_tag, case_name);
    show_result_pattern();

    free_video_el_inst();
    release_convert_pipeline(&res);
    esp_gmf_port_deinit(overlay_port);
}

static void overlay_log_subcase_header(const char *suite, const overlay_scenario_desc_t *sc,
                                       uint32_t frame_w, uint32_t frame_h, uint32_t rgn_px, bool expect_hw)
{
    bool hw_capable = false;
#if CONFIG_SOC_PPA_SUPPORTED
    hw_capable = esp_gmf_video_overlay_hw_blend_supported(sc->dst_fmt);
#endif  /* CONFIG_SOC_PPA_SUPPORTED */
    ESP_LOGI(TAG, "--------------------------------------------------------------");
    ESP_LOGI(TAG, "[%s] subcase: %s", suite, sc->name);
    ESP_LOGI(TAG, "  dst=%s frame=%ux%u overlay_win=%ux%u rgn_pixels=%u",
             esp_gmf_video_get_format_string(sc->dst_fmt), (unsigned)frame_w, (unsigned)frame_h,
             (unsigned)(frame_w / 2), (unsigned)(frame_h / 2), (unsigned)rgn_px);
#if CONFIG_SOC_PPA_SUPPORTED
    ESP_LOGI(TAG, "  expect_path=%s hw_capable=%d (region %s HW min %d px)",
             expect_hw ? "HW(PPA)" : "SW", (int)hw_capable,
             rgn_px >= OVERLAY_HW_BLEND_MIN_PIXELS ? ">=" : "<",
             OVERLAY_HW_BLEND_MIN_PIXELS);
    if (expect_hw && sc->id == OVERLAY_SCENARIO_OUYY) {
        ESP_LOGI(TAG, "  note: trans_ouyy uses SW (PPA fg_ck + YUV420 BG blocks); other HW cases use PPA");
    }
#else
    ESP_LOGI(TAG, "  expect_path=SW (no PPA)");
    (void)expect_hw;
    (void)hw_capable;
#endif  /* CONFIG_SOC_PPA_SUPPORTED */
}

static void overlay_run_suite(const char *suite_tag, uint32_t frame_w, uint32_t frame_h, bool expect_hw_path)
{
    ESP_LOGI(TAG, "######## %s overlay suite: frame %ux%u expect %s ########",
             suite_tag, (unsigned)frame_w, (unsigned)frame_h,
             expect_hw_path ? "HW" : "SW");

    for (size_t i = 0; i < ELEMS(s_overlay_scenarios); i++) {
        const overlay_scenario_desc_t *sc = &s_overlay_scenarios[i];
        if (expect_hw_path && !sc->run_in_hw_suite) {
            ESP_LOGI(TAG, "[%s] skip subcase %s (not in HW suite)", suite_tag, sc->name);
            continue;
        }
        overlay_test_cfg_t cfgs[2];
        int cfg_count = 0;
        overlay_scenario_build_cfgs(sc->id, frame_w, frame_h, cfgs, &cfg_count);
        if (cfg_count == 0) {
            continue;
        }
        uint32_t rgn_px = overlay_rgn_pixels(&cfgs[0].rgn.dst_rgn);
        if (cfg_count > 1) {
            rgn_px = overlay_rgn_pixels(&cfgs[1].rgn.dst_rgn);
        }
        if (expect_hw_path) {
            TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(OVERLAY_HW_BLEND_MIN_PIXELS, rgn_px,
                                                 "HW suite needs region >= 100x100");
        } else {
            TEST_ASSERT_LESS_THAN_MESSAGE(OVERLAY_HW_BLEND_MIN_PIXELS, rgn_px,
                                          "SW suite needs region < 100x100");
        }
        overlay_log_subcase_header(suite_tag, sc, frame_w, frame_h, rgn_px, expect_hw_path);
        char case_tag[OVERLAY_CASE_TAG_LEN];
        snprintf(case_tag, sizeof(case_tag), "%s_%s", suite_tag, sc->name);
        run_overlay_test(cfgs, cfg_count, sc->verify, sc->fill_fn, sc->dst_fmt, frame_w, frame_h, suite_tag,
                         case_tag);
    }
    ESP_LOGI(TAG, "######## %s overlay suite: all %u subcases done ########",
             suite_tag, (unsigned)ELEMS(s_overlay_scenarios));
}

static void fill_overlay_color_bar(void)
{
    pattern_info_t pattern_info = {
        .format_id = ESP_FOURCC_RGB16,
        .res = video_el_inst.overlay_res,
        .pixel = video_el_inst.overlay_data,
        .data_size = video_el_inst.overlay_size,
        .bar_count = TEST_PATTERN_BAR_COUNT / 2,
        .vertical = !TEST_PATTERN_VERTICAL,
    };
    gen_pattern_color_bar(&pattern_info);
}

static void fill_overlay_trans_key(void)
{
    overlay_trans_rect_t ctr = overlay_trans_center_rect();
    const uint16_t key_px = PACK_RGB565(OVERLAY_TRANS_KEY_R, OVERLAY_TRANS_KEY_G, OVERLAY_TRANS_KEY_B);
    const uint16_t fg_px = PACK_RGB565(OVERLAY_TRANS_FG_R, OVERLAY_TRANS_FG_G, OVERLAY_TRANS_FG_B);
    const int ow = video_el_inst.overlay_res.width;
    const int oh = video_el_inst.overlay_res.height;
    uint16_t *px = (uint16_t *)video_el_inst.overlay_data;

    for (int y = 0; y < oh; y++) {
        for (int x = 0; x < ow; x++) {
            px[y * ow + x] = overlay_px_in_trans_center(x, y, &ctr) ? key_px : fg_px;
        }
    }
}

static esp_gmf_err_io_t out_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    if (load->buf) {
        video_el_inst.no_need_free = true;
    } else {
        if (wanted_size > video_el_inst.out_max_size) {
            SAFE_FREE(video_el_inst.out_pixel);
            uint8_t *new_buf = esp_gmf_oal_malloc_align(TEST_VIDEO_ALIGNMENT, wanted_size + TEST_VIDEO_ALIGNMENT);
            if (new_buf == NULL) {
                ESP_LOGE(TAG, "Fail to allocate %d bytes for output buffer", (int)wanted_size + TEST_VIDEO_ALIGNMENT);
                return -1;
            }
            video_el_inst.out_pixel = new_buf;
            video_el_inst.out_max_size = wanted_size;
        }
        load->buf = video_el_inst.out_pixel;
        load->buf_length = video_el_inst.out_max_size;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t out_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    video_el_inst.out_pixel = load->buf;
    video_el_inst.out_size = load->valid_size;
    ESP_LOGI(TAG, "Out frame %d size %d", (int)video_el_inst.out_frame_count, (int)video_el_inst.out_size);
    video_el_inst.out_frame_count++;
    // FIXME: why add this ugly code
    if (video_el_inst.no_need_free == false) {
        load->buf = NULL;
    }
    return ESP_GMF_IO_OK;
}

static int prepare_pool(convert_res_t *res)
{
    memset(res, 0, sizeof(*res));
    esp_gmf_pool_init(&res->pool);
    if (res->pool == NULL) {
        return -1;
    }
    gmf_loader_setup_video_codec_default(res->pool);
    gmf_loader_setup_video_effects_default(res->pool);
    ESP_GMF_POOL_SHOW_ITEMS(res->pool);
    return 0;
}

static int prepare_convert_pipeline(convert_res_t *res, const char **elements)
{
    int n = 0;
    const char **header = elements;
    while (*header) {
        n++;
        header++;
    }
    esp_gmf_pool_new_pipeline(res->pool, NULL, elements, n, NULL, &res->pipe);
    if (res->pipe == NULL) {
        return -1;
    }

    // Set input and output buffer
    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BLOCK(in_acquire, in_release, NULL, NULL, 0, ESP_GMF_MAX_DELAY);
    esp_gmf_pipeline_reg_el_port(res->pipe, elements[0], ESP_GMF_IO_DIR_READER, in_port);
    // Use byte block to avoid malloc again
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BLOCK(out_acquire, out_release, NULL, NULL, 0, ESP_GMF_MAX_DELAY);
    esp_gmf_pipeline_reg_el_port(res->pipe, elements[n - 1], ESP_GMF_IO_DIR_WRITER, out_port);

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.thread.stack_in_ext = true;
    cfg.thread.stack = VIDEO_EL_MAX_STACK_SIZE;
    esp_gmf_task_init(&cfg, &res->task);
    if (res->task == NULL) {
        return -1;
    }

    esp_gmf_pipeline_bind_task(res->pipe, res->task);
    esp_gmf_pipeline_loading_jobs(res->pipe);

    esp_gmf_pipeline_get_el_by_name(res->pipe, "vid_ppa", &res->convert_hd);
    esp_gmf_pipeline_get_el_by_name(res->pipe, "vid_enc", &res->enc_hd);
    esp_gmf_pipeline_get_el_by_name(res->pipe, "vid_fps_cvt", &res->rate_hd);
    esp_gmf_pipeline_get_el_by_name(res->pipe, "vid_overlay", &res->overlay_hd);
    esp_gmf_pipeline_get_el_by_name(res->pipe, "vid_dec", &res->dec_hd);

    return 0;
}

static void release_convert_pipeline(convert_res_t *res)
{
    if (res->pipe) {
        esp_gmf_pipeline_stop(res->pipe);
    }
    if (res->task) {
        esp_gmf_task_deinit(res->task);
    }
    if (res->pipe) {
        esp_gmf_pipeline_destroy(res->pipe);
    }
    gmf_loader_teardown_video_codec_default(res->pool);
    gmf_loader_teardown_video_effects_default(res->pool);
    esp_gmf_pool_deinit(res->pool);
}

static int test_color_convert(convert_res_t *res, uint32_t convert_pair[][2], int n)
{
    for (int i = 0; i < n; i++) {
        ESP_LOGI(TAG, "test_color_convert case %d/%d: %s -> %s (SW vid_color_cvt or HW per pool)",
                 i + 1, n,
                 esp_gmf_video_get_format_string(convert_pair[i][0]),
                 esp_gmf_video_get_format_string(convert_pair[i][1]));
        // Gen pattern
        allocate_src_pattern(convert_pair[i][0], false);
        video_el_inst.out_res = video_el_inst.src_res;
        video_el_inst.out_codec = convert_pair[i][1];
        esp_gmf_info_video_t info = {
            .format_id = convert_pair[i][0],
            .width = TEST_PATTERN_WIDTH,
            .height = TEST_PATTERN_HEIGHT,
        };
        esp_gmf_element_handle_t convert_hd;
        convert_hd = get_element_by_caps_from_pipe(res->pipe, ESP_GMF_CAPS_VIDEO_COLOR_CONVERT);
        esp_gmf_video_param_set_dst_format(convert_hd, convert_pair[i][1]);
        esp_gmf_pipeline_report_info(res->pipe, ESP_GMF_INFO_VIDEO, &info, sizeof(info));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res->pipe));
        vTaskDelay(1000 / portTICK_RATE_MS);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res->pipe));
        show_result_pattern();
        free_video_el_inst();
        esp_gmf_pipeline_reset(res->pipe);
        esp_gmf_pipeline_loading_jobs(res->pipe);
    }
    return 0;
}
#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31

TEST_CASE("PPA TEST", "[ESP_GMF_VIDEO]")
{
    static const struct {
        uint32_t  src;
        uint32_t  dst;
        const char *const desc;
    } convert_cases[] = {
        {ESP_FOURCC_YUYV, ESP_FOURCC_RGB16,
         "PPA native color: YUYV -> RGB565 (same res, vid_ppa use_ppa=1, no imgfx wrap)"},
    };
    for (size_t i = 0; i < ELEMS(convert_cases); i++) {
        ESP_LOGI(TAG, "PPA TEST case %u: %s", (unsigned)i, convert_cases[i].desc);
        allocate_src_pattern(convert_cases[i].src, false);
        video_el_inst.out_res = video_el_inst.src_res;
        video_el_inst.out_codec = convert_cases[i].dst;
        esp_video_codec_resolution_t res = {
            .width = video_el_inst.src_res.width,
            .height = video_el_inst.src_res.height,
        };
        video_el_inst.out_size = esp_video_codec_get_image_size(video_el_inst.out_codec, &res);
        video_el_inst.out_pixel = esp_gmf_oal_malloc_align(TEST_VIDEO_ALIGNMENT, video_el_inst.out_size);
        for (int j = 0; j < 1; j++) {
            memset(video_el_inst.out_pixel, 0, video_el_inst.out_size);
            ESP_LOGI(TAG, "  swap probe j=%d: %s -> %s", j,
                     esp_gmf_video_get_format_string(convert_cases[i].src),
                     esp_gmf_video_get_format_string(convert_cases[i].dst));
            gmf_video_ppa_test(convert_cases[i].src, convert_cases[i].dst, TEST_PATTERN_WIDTH, TEST_PATTERN_HEIGHT,
                               video_el_inst.src_pixel, video_el_inst.out_pixel, j);
            show_result_pattern();
        }
        free_video_el_inst();
    }
}

TEST_CASE("PPA blend TEST", "[ESP_GMF_VIDEO]")
{
    static const uint32_t overlay_fg_fmt = ESP_FOURCC_RGB16;
    static const struct {
        uint32_t  dst;
        bool  overlay_hw_blend;
        bool  expect_ppa_ok;
        const char *desc;
    } blend_cases[] = {
        {ESP_FOURCC_RGB16, true, true,
         "PPA blend RGB565 LE dst (native PPA_BLEND_COLOR_MODE_RGB565)"},
        {ESP_FOURCC_RGB16_BE, true, true,
         "PPA blend RGB565 BE dst (RGB565 + bg_byte_swap)"},
    };

    for (size_t i = 0; i < ELEMS(blend_cases); i++) {
        ESP_LOGI(TAG, "PPA blend TEST case %u: %s", (unsigned)i, blend_cases[i].desc);
        bool hw_supported = esp_gmf_video_overlay_hw_blend_supported(blend_cases[i].dst);
        TEST_ASSERT_EQUAL(blend_cases[i].overlay_hw_blend, hw_supported);

        esp_err_t ppa_ret = gmf_video_ppa_blend_probe(overlay_fg_fmt, blend_cases[i].dst,
                                                      TEST_PATTERN_WIDTH, TEST_PATTERN_HEIGHT);
        ESP_LOGI(TAG, "  src %s dst %s overlay_hw=%d ppa_blend_probe=%s",
                 esp_gmf_video_get_format_string(overlay_fg_fmt),
                 esp_gmf_video_get_format_string(blend_cases[i].dst), hw_supported, esp_err_to_name(ppa_ret));

        if (blend_cases[i].expect_ppa_ok) {
            TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ppa_ret, blend_cases[i].desc);
        } else {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_OK, ppa_ret, blend_cases[i].desc);
        }
    }
}

TEST_CASE("vid_ppa soft color convert integration", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    TEST_ASSERT_EQUAL(0, prepare_pool(&res));
    const char *name[] = {"vid_ppa", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, name));

    static const struct {
        uint32_t  src;
        uint32_t  dst;
        const char *const desc;
        bool  scaled_half;  /*!< if true, dst resolution is half of src (forces PPA geometry path) */
    } cases[] = {
        {ESP_FOURCC_RGB24, ESP_FOURCC_RGB16,
         "2D-DMA only: RGB888 -> RGB565 same resolution (check_2ddma_supported, use_ppa=0, no imgfx)", false},
        {ESP_FOURCC_RGB16, ESP_FOURCC_BGR24,
         "2D-DMA only: RGB565_LE -> BGR888 same resolution (check_2ddma_supported, use_ppa=0)", false},
        {ESP_FOURCC_BGR24, ESP_FOURCC_RGB16,
         "2D-DMA only: BGR888 -> RGB565_LE same resolution (check_2ddma_supported, use_ppa=0)", false},
        {ESP_FOURCC_RGB16_BE, ESP_FOURCC_BGR24,
         "2D-DMA only: RGB565_BE -> BGR888 same resolution (check_2ddma_supported, use_ppa=0)", false},
        {ESP_FOURCC_YUYV, ESP_FOURCC_RGB16,
         "PPA native: YUYV -> RGB565 (hw_native via PPA, use_ppa=1, soft_in=0 soft_out=0)", false},
        {ESP_FOURCC_YUV420P, ESP_FOURCC_RGB16,
         "PPA + software pre-color: YUV420P -> RGB565 (soft_in=1, bridge to PPA src fmt)", false},
        {ESP_FOURCC_RGB16, ESP_FOURCC_YUYV,
         "PPA + software post-color: RGB565 -> YUYV (soft_out=1; PPA -> RGB565 bridge per imgfx matrix, then imgfx -> YUYV)", false},
        {ESP_FOURCC_YUV420P, ESP_FOURCC_YUYV,
         "PPA + soft same res: YUV420P -> YUYV (esp_imgfx has no YU12→YUYV; bridge via PPA, not imgfx_only)", false},
        {ESP_FOURCC_YUV420P, ESP_FOURCC_YUYV,
         "PPA when scaled: YUV420P -> YUYV half res (geom=1, use_ppa=1, soft_in/out as needed for PPA)", true},
    };

    for (size_t i = 0; i < ELEMS(cases); i++) {
        ESP_LOGI(TAG, "vid_ppa integration case %u: %s", (unsigned)i, cases[i].desc);
        allocate_src_pattern(cases[i].src, false);
        video_el_inst.out_res = video_el_inst.src_res;
        if (cases[i].scaled_half) {
            video_el_inst.out_res.width = video_el_inst.src_res.width >> 1;
            video_el_inst.out_res.height = video_el_inst.src_res.height >> 1;
        }
        video_el_inst.out_codec = cases[i].dst;
        esp_gmf_video_param_set_dst_format(res.convert_hd, cases[i].dst);
        esp_gmf_video_param_set_dst_resolution(res.convert_hd, &video_el_inst.out_res);
        esp_gmf_info_video_t info = {
            .format_id = cases[i].src,
            .width = TEST_PATTERN_WIDTH,
            .height = TEST_PATTERN_HEIGHT,
        };
        esp_gmf_pipeline_report_info(res.pipe, ESP_GMF_INFO_VIDEO, &info, sizeof(info));
        esp_gmf_err_t run_ret = esp_gmf_pipeline_run(res.pipe);
        TEST_ASSERT_MESSAGE(run_ret == ESP_GMF_ERR_OK, cases[i].desc);
        vTaskDelay(100 / portTICK_RATE_MS);
        esp_gmf_err_t stop_ret = esp_gmf_pipeline_stop(res.pipe);
        TEST_ASSERT_MESSAGE(stop_ret == ESP_GMF_ERR_OK, cases[i].desc);
        show_result_pattern();
        free_video_el_inst();
        esp_gmf_pipeline_reset(res.pipe);
        esp_gmf_pipeline_loading_jobs(res.pipe);
    }
    release_convert_pipeline(&res);
}
#endif  /* CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31 */

#ifdef CONFIG_IDF_TARGET_ESP32P4
TEST_CASE("Color convert HW", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_ppa", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, (const char **)name));
    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_OUYY_EVYY},
        {ESP_FOURCC_OUYY_EVYY, ESP_FOURCC_RGB16},
    };
    test_color_convert(&res, convert_pair, ELEMS(convert_pair));

    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */

TEST_CASE("Color convert SW", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_color_cvt", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, (const char **)name));

    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_OUYY_EVYY},
        {ESP_FOURCC_OUYY_EVYY, ESP_FOURCC_RGB16},
        {ESP_FOURCC_RGB16, ESP_FOURCC_BGR16},
        {ESP_FOURCC_RGB16, ESP_FOURCC_RGB24},
    };
    test_color_convert(&res, convert_pair, ELEMS(convert_pair));

    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Color convert by caps", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    esp_gmf_element_handle_t element;
    element = get_element_by_caps_from_pool(res.pool, ESP_GMF_CAPS_VIDEO_COLOR_CONVERT);
    const char *name[] = {OBJ_GET_TAG(element), NULL};
    printf("Get element name %s\n", OBJ_GET_TAG(element));
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, (const char **)name));

    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_OUYY_EVYY},
        {ESP_FOURCC_OUYY_EVYY, ESP_FOURCC_RGB16},
    };
    test_color_convert(&res, convert_pair, ELEMS(convert_pair));

    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}

static int test_scale(convert_res_t *res, uint32_t convert_pair[][2], int n)
{
    for (int i = 0; i < n; i++) {
        ESP_LOGI(TAG, "test_scale case %d/%d: src %s -> dst %s (half res)",
                 i + 1, n,
                 esp_gmf_video_get_format_string(convert_pair[i][0]),
                 esp_gmf_video_get_format_string(convert_pair[i][1]));
        // Gen pattern
        allocate_src_pattern(convert_pair[i][0], false);
        video_el_inst.out_res.width = video_el_inst.src_res.width >> 1;
        video_el_inst.out_res.height = video_el_inst.src_res.height >> 1;
        video_el_inst.out_codec = convert_pair[i][1];
        esp_gmf_element_handle_t convert_hd;
        convert_hd = get_element_by_caps_from_pipe(res->pipe, ESP_GMF_CAPS_VIDEO_COLOR_CONVERT);
        esp_gmf_video_param_set_dst_format(convert_hd, convert_pair[i][1]);
        esp_gmf_element_handle_t scale_hd;
        scale_hd = get_element_by_caps_from_pipe(res->pipe, ESP_GMF_CAPS_VIDEO_SCALE);
        esp_gmf_video_param_set_dst_resolution(scale_hd, &video_el_inst.out_res);

        esp_gmf_info_video_t info = {
            .format_id = convert_pair[i][0],
            .width = TEST_PATTERN_WIDTH,
            .height = TEST_PATTERN_HEIGHT,
        };

        esp_gmf_pipeline_report_info(res->pipe, ESP_GMF_INFO_VIDEO, &info, sizeof(info));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res->pipe));
        vTaskDelay(1000 / portTICK_RATE_MS);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res->pipe));
        show_result_pattern();
        free_video_el_inst();
        esp_gmf_pipeline_reset(res->pipe);
        esp_gmf_pipeline_loading_jobs(res->pipe);
        esp_cpu_clear_watchpoint(0);
    }
    return 0;
}

#ifdef CONFIG_IDF_TARGET_ESP32P4
TEST_CASE("Scale HW", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_ppa", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, (const char **)name));

    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_OUYY_EVYY},
        {ESP_FOURCC_OUYY_EVYY, ESP_FOURCC_RGB16},
    };
    test_scale(&res, convert_pair, ELEMS(convert_pair));

    // Clearup resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */

TEST_CASE("Scale SW", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_scale", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, (const char **)name));
    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_RGB16},
        {ESP_FOURCC_RGB24, ESP_FOURCC_RGB24},
    };
    test_scale(&res, convert_pair, ELEMS(convert_pair));

    // Clearup resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}

static int test_rotate(convert_res_t *res, uint32_t convert_pair[][2], int n)
{
    for (int i = 0; i < n; i++) {
        ESP_LOGI(TAG, "test_rotate case %d/%d: src %s -> dst %s (90/270 deg)",
                 i + 1, n,
                 esp_gmf_video_get_format_string(convert_pair[i][0]),
                 esp_gmf_video_get_format_string(convert_pair[i][1]));
        // Gen pattern
        allocate_src_pattern(convert_pair[i][0], false);
        video_el_inst.out_res.width = video_el_inst.src_res.height;
        video_el_inst.out_res.height = video_el_inst.src_res.width;
        video_el_inst.out_codec = convert_pair[i][1];
        video_el_inst.rotate_degree = i & 1 ? 90 : 270;

        esp_gmf_element_handle_t convert_hd;
        convert_hd = get_element_by_caps_from_pipe(res->pipe, ESP_GMF_CAPS_VIDEO_COLOR_CONVERT);
        esp_gmf_video_param_set_dst_format(convert_hd, convert_pair[i][1]);
        esp_gmf_element_handle_t rotate_hd;
        rotate_hd = get_element_by_caps_from_pipe(res->pipe, ESP_GMF_CAPS_VIDEO_ROTATE);
        esp_gmf_video_param_set_rotate_angle(rotate_hd, video_el_inst.rotate_degree);
        esp_gmf_video_param_set_dst_resolution(rotate_hd, &video_el_inst.out_res);

        esp_gmf_info_video_t info = {
            .format_id = convert_pair[i][0],
            .width = TEST_PATTERN_WIDTH,
            .height = TEST_PATTERN_HEIGHT,
        };

        esp_gmf_pipeline_report_info(res->pipe, ESP_GMF_INFO_VIDEO, &info, sizeof(info));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res->pipe));
        vTaskDelay(1000 / portTICK_RATE_MS);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res->pipe));
        show_result_pattern();
        free_video_el_inst();
        esp_gmf_pipeline_reset(res->pipe);
        esp_gmf_pipeline_loading_jobs(res->pipe);
    }
    return 0;
}

#ifdef CONFIG_IDF_TARGET_ESP32P4
TEST_CASE("Rotate HW", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_ppa", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, name));
    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_OUYY_EVYY},
        {ESP_FOURCC_OUYY_EVYY, ESP_FOURCC_RGB16},
    };
    test_rotate(&res, convert_pair, ELEMS(convert_pair));

    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */

TEST_CASE("Rotate SW", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_rotate", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, (const char **)name));
    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_RGB16},
    };
    test_rotate(&res, convert_pair, ELEMS(convert_pair));

    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}

static int test_crop(convert_res_t *res, uint32_t convert_pair[][2], int n)
{
    for (int i = 0; i < n; i++) {
        ESP_LOGI(TAG, "test_crop case %d/%d: src %s -> dst %s (quarter crop)",
                 i + 1, n,
                 esp_gmf_video_get_format_string(convert_pair[i][0]),
                 esp_gmf_video_get_format_string(convert_pair[i][1]));
        // Gen pattern
        allocate_src_pattern(convert_pair[i][0], false);
        video_el_inst.out_res.width = video_el_inst.src_res.width >> 1;
        video_el_inst.out_res.height = video_el_inst.src_res.height >> 1;
        video_el_inst.out_codec = convert_pair[i][1];

        esp_gmf_video_rgn_t crop_rgn = {
            .x = video_el_inst.src_res.width >> 2,
            .y = video_el_inst.src_res.height >> 2,
            .width = video_el_inst.out_res.width,
            .height = video_el_inst.out_res.height,
        };
        esp_gmf_element_handle_t convert_hd;
        convert_hd = get_element_by_caps_from_pipe(res->pipe, ESP_GMF_CAPS_VIDEO_COLOR_CONVERT);
        esp_gmf_video_param_set_dst_format(convert_hd, convert_pair[i][1]);

        esp_gmf_element_handle_t crop_hd;
        crop_hd = get_element_by_caps_from_pipe(res->pipe, ESP_GMF_CAPS_VIDEO_CROP);
        esp_gmf_video_param_set_cropped_region(crop_hd, &crop_rgn);
        esp_gmf_video_param_set_dst_resolution(crop_hd, &video_el_inst.out_res);

        esp_gmf_info_video_t info = {
            .format_id = convert_pair[i][0],
            .width = TEST_PATTERN_WIDTH,
            .height = TEST_PATTERN_HEIGHT,
        };

        esp_gmf_pipeline_report_info(res->pipe, ESP_GMF_INFO_VIDEO, &info, sizeof(info));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res->pipe));
        vTaskDelay(1000 / portTICK_RATE_MS);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res->pipe));
        show_result_pattern();
        free_video_el_inst();
        esp_gmf_pipeline_reset(res->pipe);
        esp_gmf_pipeline_loading_jobs(res->pipe);
    }
    return 0;
}

#ifdef CONFIG_IDF_TARGET_ESP32P4
TEST_CASE("Crop HW", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_ppa", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, name));
    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_OUYY_EVYY},
        {ESP_FOURCC_OUYY_EVYY, ESP_FOURCC_RGB16},
    };
    test_crop(&res, convert_pair, ELEMS(convert_pair));
    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */

TEST_CASE("Crop only SW", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);

    const char *name[] = {"vid_crop", NULL};

    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, name));

    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_RGB16},
        // { ESP_FOURCC_RGB24, ESP_FOURCC_RGB16 },
    };
    test_crop(&res, convert_pair, ELEMS(convert_pair));
    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Encoder only", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_enc", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, name));

    uint32_t convert_pair[][2] = {
#if CONFIG_IDF_TARGET_ESP32P4
        {ESP_FOURCC_OUYY_EVYY, ESP_FOURCC_H264},
        {ESP_FOURCC_RGB16, ESP_FOURCC_MJPG},
#else
        {ESP_FOURCC_RGB24, ESP_FOURCC_MJPG},
#if CONFIG_IDF_TARGET_ESP32S3
        {ESP_FOURCC_YUV420P, ESP_FOURCC_H264},
#endif  /* CONFIG_IDF_TARGET_ESP32S3 */
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
    };
    for (int i = 0; i < ELEMS(convert_pair); i++) {
        // Gen pattern
        allocate_src_pattern(convert_pair[i][0], false);
        video_el_inst.out_res = video_el_inst.src_res;
        video_el_inst.out_codec = convert_pair[i][1];

        esp_gmf_info_video_t info = {
            .format_id = convert_pair[i][0],
            .width = TEST_PATTERN_WIDTH,
            .height = TEST_PATTERN_HEIGHT,
            .fps = 10,
        };
        // esp_gmf_video_enc_set_dst_codec(res.enc_hd, convert_pair[i][1]);
        esp_gmf_video_param_set_dst_codec(res.enc_hd, convert_pair[i][1]);
        esp_gmf_pipeline_report_info(res.pipe, ESP_GMF_INFO_VIDEO, &info, sizeof(info));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res.pipe));
        vTaskDelay(1000 / portTICK_RATE_MS);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res.pipe));
        free_video_el_inst();
        esp_gmf_pipeline_reset(res.pipe);
        esp_gmf_pipeline_loading_jobs(res.pipe);
    }
    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Encoder open failure", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    TEST_ASSERT_EQUAL(0, prepare_pool(&res));
    const char *name[] = {"vid_enc", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, name));
    TEST_ASSERT_NOT_NULL(res.enc_hd);

    vid_enc_open_report_probe_t enc_probe = {.enc_hd = res.enc_hd};
    esp_gmf_pipeline_set_event(res.pipe, vid_enc_open_report_probe_cb, &enc_probe);

    allocate_src_pattern(ESP_FOURCC_RGB16, false);
    video_el_inst.out_res = video_el_inst.src_res;
    esp_gmf_video_param_set_dst_codec(res.enc_hd, ESP_FOURCC_PCM);

    enc_probe.vid_report_count_from_enc = 0;
    esp_gmf_info_video_t info = {
        .format_id = ESP_FOURCC_RGB16,
        .width = TEST_PATTERN_WIDTH,
        .height = TEST_PATTERN_HEIGHT,
        .fps = 10,
    };
    esp_gmf_pipeline_report_info(res.pipe, ESP_GMF_INFO_VIDEO, &info, sizeof(info));
    (void)esp_gmf_pipeline_run(res.pipe);
    vTaskDelay(500 / portTICK_RATE_MS);
    (void)esp_gmf_pipeline_stop(res.pipe);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, enc_probe.vid_report_count_from_enc,
                                     "vid_enc must not REPORT_INFO if encoder open failed");

    free_video_el_inst();
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("FPS convert only", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_fps_cvt", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, name));
    uint32_t convert_pair[][2] = {
        {ESP_FOURCC_RGB16, ESP_FOURCC_MJPG},
    };
    for (int i = 0; i < ELEMS(convert_pair); i++) {
        // Gen pattern
        allocate_src_pattern(convert_pair[i][0], false);
        video_el_inst.out_res = video_el_inst.src_res;
        video_el_inst.out_codec = convert_pair[i][1];
        esp_gmf_info_video_t info = {
            .format_id = convert_pair[i][0],
            .width = TEST_PATTERN_WIDTH,
            .height = TEST_PATTERN_HEIGHT,
            .fps = 10,
        };
        // esp_gmf_video_fps_cvt_set_fps(res.rate_hd, 5);
        esp_gmf_video_param_set_fps(res.rate_hd, 5);
        esp_gmf_pipeline_report_info(res.pipe, ESP_GMF_INFO_VIDEO, &info, sizeof(info));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res.pipe));
        vTaskDelay(100 / portTICK_RATE_MS);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res.pipe));
        free_video_el_inst();
        esp_gmf_pipeline_reset(res.pipe);
        esp_gmf_pipeline_loading_jobs(res.pipe);
    }
    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Overlay mixer SW", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    overlay_run_suite("SW", OVERLAY_SW_FRAME_WIDTH, OVERLAY_SW_FRAME_HEIGHT, false);
}

#if CONFIG_SOC_PPA_SUPPORTED
TEST_CASE("Overlay mixer HW", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    overlay_run_suite("HW", OVERLAY_HW_FRAME_WIDTH, OVERLAY_HW_FRAME_HEIGHT, true);
}
#endif  /* CONFIG_SOC_PPA_SUPPORTED */

TEST_CASE("Encoder to Decode", "[ESP_GMF_VIDEO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    convert_res_t res;
    memset(&video_el_inst, 0, sizeof(video_el_test_t));
    prepare_pool(&res);
    const char *name[] = {"vid_enc", "vid_dec", NULL};
    TEST_ASSERT_EQUAL(0, prepare_convert_pipeline(&res, name));
    TEST_ASSERT_NOT_NULL(res.dec_hd);

    vid_dec_open_report_probe_t dec_probe = {.dec_hd = res.dec_hd};
    esp_gmf_pipeline_set_event(res.pipe, vid_dec_open_report_probe_cb, &dec_probe);

    uint32_t convert_pair[][3] = {
#if CONFIG_IDF_TARGET_ESP32P4
        {ESP_FOURCC_OUYY_EVYY, ESP_FOURCC_H264, ESP_FOURCC_YUV420P},
        {ESP_FOURCC_RGB16, ESP_FOURCC_MJPG, ESP_FOURCC_RGB16},
#else
        {ESP_FOURCC_RGB24, ESP_FOURCC_MJPG, ESP_FOURCC_RGB16},
#if CONFIG_IDF_TARGET_ESP32S3
        {ESP_FOURCC_YUV420P, ESP_FOURCC_H264, ESP_FOURCC_YUV420P},
#endif  /* CONFIG_IDF_TARGET_ESP32S3 */
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
    };
    for (int i = 0; i < ELEMS(convert_pair); i++) {
        dec_probe.compressed_fourcc = convert_pair[i][1];
        dec_probe.saw_compressed_report_from_dec = false;
        // Gen pattern
        allocate_src_pattern(convert_pair[i][0], false);
        video_el_inst.out_res = video_el_inst.src_res;
        video_el_inst.out_codec = convert_pair[i][2];

        esp_gmf_info_video_t info = {
            .format_id = convert_pair[i][0],
            .width = TEST_PATTERN_WIDTH,
            .height = TEST_PATTERN_HEIGHT,
            .fps = 10,
        };
        // esp_gmf_video_enc_set_dst_codec(res.enc_hd, convert_pair[i][1]);
        esp_gmf_video_param_set_dst_codec(res.enc_hd, convert_pair[i][1]);
        esp_gmf_pipeline_report_info(res.pipe, ESP_GMF_INFO_VIDEO, &info, sizeof(info));
        // Set decoder information
        // esp_gmf_video_dec_set_dst_format(res.dec_hd, convert_pair[i][2]);
        esp_gmf_video_param_set_dst_format(res.dec_hd, convert_pair[i][2]);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res.pipe));
        vTaskDelay(1000 / portTICK_RATE_MS);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res.pipe));
        TEST_ASSERT_FALSE_MESSAGE(dec_probe.saw_compressed_report_from_dec,
                                  "vid_dec must not REPORT_INFO compressed stream FourCC at open when decoding");
        show_result_pattern();
        free_video_el_inst();
        esp_gmf_pipeline_reset(res.pipe);
        esp_gmf_pipeline_loading_jobs(res.pipe);
    }
    // Clear up resources
    release_convert_pipeline(&res);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
}

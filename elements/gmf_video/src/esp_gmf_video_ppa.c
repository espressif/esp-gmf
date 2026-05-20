/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_gmf_err.h"
#include "esp_gmf_node.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_video_ppa.h"
#include "esp_gmf_video_methods_def.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_element.h"
#include "esp_gmf_video_element.h"
#include "esp_gmf_info.h"
#include "gmf_video_common.h"
#include "esp_heap_caps.h"
#include "esp_fourcc.h"
#include "esp_cache.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_idf_version.h"
#include "esp_gmf_oal_mutex.h"
#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31
#include "driver/ppa.h"
#include "esp_private/dma2d.h"
#include "hal/dma2d_types.h"
#include "hal/color_types.h"
#include "soc/dma2d_channel.h"
#include "esp_imgfx_color_convert.h"
#endif  /* CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31 */

static const char *TAG = "VCVT_EL";

#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31

/**
 * @brief  2D-DMA EOF callback
 */
typedef bool (*dma2d_m2m_trans_eof_callback_t)(void *user_data);

/**
 * @brief  2D-DMA M2M transport configuration
 */
typedef struct {
    intptr_t                        tx_desc_base_addr;   /*!< 2D-DMA TX descriptor address */
    intptr_t                        rx_desc_base_addr;   /*!< 2D-DMA RX descriptor address */
    dma2d_m2m_trans_eof_callback_t  trans_eof_cb;        /*!< Transfer EOF callback */
    void                           *user_data;           /*!< User registered data to be passed into `trans_eof_cb` callback */
    dma2d_transfer_ability_t        transfer_ability;    /*!< Transfer ability */
    dma2d_strategy_config_t        *tx_strategy_config;  /*!< Pointer to a collection of 2D-DMA TX strategy configuration */
    dma2d_strategy_config_t        *rx_strategy_config;  /*!< Pointer to a collection of 2D-DMA RX strategy configuration */
    dma2d_csc_config_t             *tx_csc_config;       /*!< Pointer to a collection of 2D-DMA TX color space conversion configuration */
    dma2d_csc_config_t             *rx_csc_config;       /*!< Pointer to a collection of 2D-DMA RX color space conversion configuration */
} dma2d_m2m_trans_config_t;

/**
 * @brief  2D-DMA M2M transaction definition
 */
typedef struct {
    dma2d_m2m_trans_config_t  m2m_trans_desc;                  /*!< M2M transport description configuration */
    dma2d_trans_config_t      dma_chan_desc;                   /*!< 2D-DMA transport configuration */
    uint8_t                   dma_trans_placeholder_head[64];  /*!< DMA transport description place holder */
} dma2d_m2m_transaction_t;

/**
 * @brief  2D-DMA information definition
 */
typedef struct {
    dma2d_descriptor_t      *rx_desc;  /*!< Pointer to 2D-DMA RX description */
    dma2d_descriptor_t      *tx_desc;  /*!< Pointer to 2D-DMA TX description */
    dma2d_pool_handle_t      handle;   /*!< 2D-DMA pool handle */
    SemaphoreHandle_t        sema;     /*!< Semaphore for wait transfer done */
    dma2d_m2m_transaction_t  trans;    /*!< 2D-DMA M2M transaction */
    dma2d_csc_config_t       tx_cvt;   /*!< 2D-DMA TX color space conversion configuration */
} dma2d_info_t;
#endif  /* CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31 */

/**
 * @brief  Video PPA (Pixel Processing Accelerator) definition
 */
typedef struct {
    esp_gmf_video_element_t  parent;          /*!< Video element parent */
    uint32_t                 dst_format;      /*!< Color converter destination format */
    uint16_t                 dst_width;       /*!< Scale destination width */
    uint16_t                 dst_height;      /*!< Scale destination height */
    uint16_t                 rotate_degree;   /*!< Rotation angle setting */
    esp_gmf_video_rgn_t      crop_rgn;        /*!< Cropped region setting */
    uint32_t                 out_frame_size;  /*!< Output frame size of PPA */
    bool                     bypass;          /*!< Whether PPA is bypassed or not */
#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31
    ppa_client_handle_t               ppa_handle;         /*!< PPA client handle */
    ppa_srm_oper_config_t             ppa_config;         /*!< PPA SRM operation configuration */
    bool                              supported;          /*!< Whether setting supported or not */
    bool                              use_ppa;            /*!< Whether use PPA or 2D-DMA */
    dma2d_info_t                      dma2d_info;         /*!< 2D-DMA information */
    bool                              soft_in;            /*!< Software color convert before PPA (src → RGB565) */
    bool                              soft_out;           /*!< Software color convert after PPA (RGB565 → dst) */
    uint8_t                          *soft_tmp;           /*!< Scratch for RGB565 intermediate(s) */
    uint32_t                          soft_tmp_len;       /*!< Bytes allocated at soft_tmp */
    uint32_t                          soft_rgb_dst_offs;  /*!< Offset of dst-sized RGB565 region inside soft_tmp (both-path) */
    esp_imgfx_color_convert_handle_t  sw_imgfx_in;        /*!< imgfx: stream format → PPA input bridge @ src resolution */
    esp_imgfx_color_convert_handle_t  sw_imgfx_out;       /*!< imgfx: PPA output bridge @ dst resolution → dst format */
    uint32_t                          sw_ppa_in_fmt;      /*!< PPA input pixel format after optional pre-imgfx */
    uint32_t                          sw_ppa_out_fmt;     /*!< PPA output pixel format before optional post-imgfx */
    bool                              sw_both_reuse_out;  /*!< both-path: first imgfx into acquired out (same res + same byte size as dst) */
    bool                              sw_imgfx_only;      /*!< Same geometry: single imgfx src→dst, no PPA/2D-DMA */
#endif  /* CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31 */
} gmf_video_ppa_t;

#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31
static SemaphoreHandle_t s_ppa_srm_mtx;
static portMUX_TYPE s_ppa_srm_init_mux = portMUX_INITIALIZER_UNLOCKED;
static int dm2d_convert(gmf_video_ppa_t *vid_cvt, esp_gmf_payload_t *in_load, esp_gmf_payload_t *out_load);

void ppa_srm_lock(void)
{
    if (s_ppa_srm_mtx == NULL) {
        portENTER_CRITICAL(&s_ppa_srm_init_mux);
        if (s_ppa_srm_mtx == NULL) {
            s_ppa_srm_mtx = xSemaphoreCreateMutex();
        }
        portEXIT_CRITICAL(&s_ppa_srm_init_mux);
    }
    if (s_ppa_srm_mtx) {
        xSemaphoreTake(s_ppa_srm_mtx, portMAX_DELAY);
    }
}

void ppa_srm_unlock(void)
{
    if (s_ppa_srm_mtx) {
        xSemaphoreGive(s_ppa_srm_mtx);
    }
}

static uint32_t pixel_buffer_size(uint32_t codec, uint32_t width, uint32_t height)
{
    switch (codec) {
        case ESP_FOURCC_RGB24:
        case ESP_FOURCC_BGR24:
            return width * height * 3;
        case ESP_FOURCC_RGB16:
        case ESP_FOURCC_RGB16_BE:
            return width * height * 2;
        case ESP_FOURCC_YUV420P:
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

static uint32_t get_frame_size(gmf_video_ppa_t *vid_cvt, uint32_t codec)
{
    return pixel_buffer_size(codec, vid_cvt->dst_width, vid_cvt->dst_height);
}

static ppa_srm_color_mode_t get_ppa_clr_mode(uint32_t codec)
{
    switch (codec) {
        case ESP_FOURCC_RGB24:
        case ESP_FOURCC_BGR24:
            return PPA_SRM_COLOR_MODE_RGB888;
        case ESP_FOURCC_RGB16_BE:
        case ESP_FOURCC_RGB16:
            return PPA_SRM_COLOR_MODE_RGB565;
        case ESP_FOURCC_YUV420P:
        case ESP_FOURCC_OUYY_EVYY:
            return PPA_SRM_COLOR_MODE_YUV420;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
        case ESP_FOURCC_YUYV:
            return PPA_SRM_COLOR_MODE_YUV422_YUYV;
        case ESP_FOURCC_YVYU:
            return PPA_SRM_COLOR_MODE_YUV422_YUYV;
        case ESP_FOURCC_UYVY:
            return PPA_SRM_COLOR_MODE_YUV422_UYVY;
        case ESP_FOURCC_VYUY:
            return PPA_SRM_COLOR_MODE_YUV422_VYUY;
#endif  /* ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0) */
        default:
            return (ppa_srm_color_mode_t)0;
    }
}

static bool check_2ddma_supported(gmf_video_ppa_t *vid_cvt)
{
    dma2d_info_t *dma2d = &vid_cvt->dma2d_info;
    esp_gmf_info_video_t *src_info = &vid_cvt->parent.src_info;
    if (src_info->format_id == ESP_FOURCC_RGB16_BE && vid_cvt->dst_format == ESP_FOURCC_BGR24) {
        dma2d->tx_cvt.pre_scramble = 1;
    } else if ((src_info->format_id == ESP_FOURCC_RGB16 && vid_cvt->dst_format == ESP_FOURCC_BGR24) || (src_info->format_id == ESP_FOURCC_BGR24 && vid_cvt->dst_format == ESP_FOURCC_RGB16)) {
        dma2d->tx_cvt.pre_scramble = 0;
    } else if ((src_info->format_id == ESP_FOURCC_RGB24 && vid_cvt->dst_format == ESP_FOURCC_RGB16)) {
        dma2d->tx_cvt.pre_scramble = 5;
    } else {
        return false;
    }
    return true;
}

static inline bool ppa_is_src_supported(uint32_t codec)
{
    switch (codec) {
        case ESP_FOURCC_RGB24:
        case ESP_FOURCC_BGR24:
        case ESP_FOURCC_RGB16:
        case ESP_FOURCC_RGB16_BE:
        case ESP_FOURCC_OUYY_EVYY:
        case ESP_FOURCC_YUYV:
        case ESP_FOURCC_YVYU:
        case ESP_FOURCC_UYVY:
        case ESP_FOURCC_VYUY:
        case ESP_FOURCC_GREY:
            return true;
        default:
            break;
    }
    return false;
}

static inline bool ppa_is_dst_support(uint32_t codec)
{
    switch (codec) {
        case ESP_FOURCC_RGB24:
        case ESP_FOURCC_BGR24:
        case ESP_FOURCC_RGB16:
        case ESP_FOURCC_RGB16_BE:
        case ESP_FOURCC_OUYY_EVYY:
        case ESP_FOURCC_UYVY:
        case ESP_FOURCC_GREY:
            return true;
        default:
            break;
    }
    return false;
}

static bool ppa_native_pair_ok(uint32_t src_fmt, uint32_t dst_fmt)
{
    if (src_fmt == dst_fmt) {
        return true;
    }
    return ppa_is_src_supported(src_fmt) && ppa_is_dst_support(dst_fmt);
}

static bool hw_native_accepts_src_dst(gmf_video_ppa_t *vid_cvt)
{
    return check_2ddma_supported(vid_cvt) || ppa_native_pair_ok(vid_cvt->parent.src_info.format_id, vid_cvt->dst_format);
}

static bool imgfx_color_convert_supported(uint32_t in_fmt, uint32_t out_fmt)
{
    if (in_fmt == out_fmt) {
        return true;
    }
    const bool in_rgb565 = (in_fmt == ESP_FOURCC_RGB16 || in_fmt == ESP_FOURCC_RGB16_BE || in_fmt == ESP_FOURCC_BGR16 || in_fmt == ESP_FOURCC_BGR16_BE);
    const bool in_rgb888 = (in_fmt == ESP_FOURCC_RGB24 || in_fmt == ESP_FOURCC_BGR24);
    if (in_rgb565 || in_rgb888) {
        switch (out_fmt) {
            case ESP_FOURCC_RGB16:
            case ESP_FOURCC_RGB16_BE:
            case ESP_FOURCC_BGR16:
            case ESP_FOURCC_BGR16_BE:
            case ESP_FOURCC_RGB24:
            case ESP_FOURCC_BGR24:
            case ESP_FOURCC_YUYV:
            case ESP_FOURCC_UYVY:
            case ESP_FOURCC_YVYU:
            case ESP_FOURCC_VYUY:
            case ESP_FOURCC_YUV420P:
            case ESP_FOURCC_OUYY_EVYY:
                return true;
            default:
                return false;
        }
    }
    if (in_fmt == ESP_FOURCC_YUYV || in_fmt == ESP_FOURCC_UYVY ||
        in_fmt == ESP_FOURCC_YVYU || in_fmt == ESP_FOURCC_VYUY) {
        switch (out_fmt) {
            case ESP_FOURCC_RGB16:
            case ESP_FOURCC_RGB16_BE:
            case ESP_FOURCC_BGR16:
            case ESP_FOURCC_BGR16_BE:
            case ESP_FOURCC_RGB24:
            case ESP_FOURCC_BGR24:
            case ESP_FOURCC_YUV420P:
            case ESP_FOURCC_OUYY_EVYY:
                return true;
            default:
                return false;
        }
    }
    if (in_fmt == ESP_FOURCC_YUV420P || in_fmt == ESP_FOURCC_OUYY_EVYY) {
        switch (out_fmt) {
            case ESP_FOURCC_RGB16:
            case ESP_FOURCC_RGB16_BE:
            case ESP_FOURCC_RGB24:
            case ESP_FOURCC_BGR24:
            case ESP_FOURCC_OUYY_EVYY:
                return true;
            default:
                return false;
        }
    }
    return false;
}

static uint32_t pick_ppa_in_bridge_fmt(uint32_t src_fmt, uint16_t w, uint16_t h)
{
    uint32_t need = pixel_buffer_size(src_fmt, w, h);
    if (need == 0) {
        return ESP_FOURCC_RGB16;
    }
    static const uint32_t cand[] = {
        ESP_FOURCC_OUYY_EVYY,
        ESP_FOURCC_UYVY,
        ESP_FOURCC_YUYV,
        ESP_FOURCC_YVYU,
        ESP_FOURCC_VYUY,
        ESP_FOURCC_RGB16,
        ESP_FOURCC_RGB16_BE,
        ESP_FOURCC_RGB24,
        ESP_FOURCC_BGR24,
    };
    for (size_t i = 0; i < sizeof(cand) / sizeof(cand[0]); i++) {
        if (!ppa_is_src_supported(cand[i])) {
            continue;
        }
        if (pixel_buffer_size(cand[i], w, h) != need) {
            continue;
        }
        if (!imgfx_color_convert_supported(src_fmt, cand[i])) {
            continue;
        }
        return cand[i];
    }
    return ESP_FOURCC_RGB16;
}

static uint32_t pick_ppa_out_bridge_fmt(uint32_t dst_fmt, uint16_t w, uint16_t h)
{
    uint32_t need = pixel_buffer_size(dst_fmt, w, h);
    if (need == 0) {
        return ESP_FOURCC_RGB16;
    }
    const uint32_t cand[] = {
        ESP_FOURCC_RGB16,
        ESP_FOURCC_RGB16_BE,
        ESP_FOURCC_UYVY,
        ESP_FOURCC_RGB24,
        ESP_FOURCC_BGR24,
        ESP_FOURCC_OUYY_EVYY,
    };
    for (size_t i = 0; i < sizeof(cand) / sizeof(cand[0]); i++) {
        if (!ppa_is_dst_support(cand[i])) {
            continue;
        }
        if (pixel_buffer_size(cand[i], w, h) != need) {
            continue;
        }
        if (!imgfx_color_convert_supported(cand[i], dst_fmt)) {
            continue;
        }
        return cand[i];
    }
    for (size_t i = 0; i < sizeof(cand) / sizeof(cand[0]); i++) {
        if (!ppa_is_dst_support(cand[i])) {
            continue;
        }
        if (imgfx_color_convert_supported(cand[i], dst_fmt)) {
            return cand[i];
        }
    }
    return ESP_FOURCC_RGB16;
}

static bool check_ppa_supported_formats(gmf_video_ppa_t *vid_cvt, uint32_t src_fmt, uint32_t dst_fmt)
{
    if (src_fmt == dst_fmt) {
        return true;
    }
    if (ppa_is_src_supported(src_fmt) == false ||
        ppa_is_dst_support(dst_fmt) == false) {
        return false;
    }
    if ((src_fmt == ESP_FOURCC_RGB16_BE &&
         ((dst_fmt == ESP_FOURCC_RGB16) || (dst_fmt == ESP_FOURCC_UYVY)))
        ||
        (src_fmt == ESP_FOURCC_RGB16 && dst_fmt == ESP_FOURCC_RGB16_BE)) {
        vid_cvt->ppa_config.byte_swap = 1;
        return true;
    }
    if ((src_fmt == ESP_FOURCC_RGB24 && dst_fmt == ESP_FOURCC_RGB16) ||
        (src_fmt == ESP_FOURCC_RGB16 && dst_fmt == ESP_FOURCC_RGB24) ||
        (src_fmt == ESP_FOURCC_RGB16 && dst_fmt == ESP_FOURCC_OUYY_EVYY) ||
        (src_fmt == ESP_FOURCC_RGB24 && dst_fmt == ESP_FOURCC_BGR24) ||
        (src_fmt == ESP_FOURCC_BGR24 && dst_fmt == ESP_FOURCC_RGB24)) {
        vid_cvt->ppa_config.rgb_swap = 1;
        return true;
    }
    return true;
}

static bool check_rotation_angle(uint16_t degree, ppa_srm_rotation_angle_t *angle)
{
    switch (degree) {
        case 0:
            *angle = PPA_SRM_ROTATION_ANGLE_0;
            break;
        case 90:
            *angle = PPA_SRM_ROTATION_ANGLE_90;
            break;
        case 180:
            *angle = PPA_SRM_ROTATION_ANGLE_180;
            break;
        case 270:
            *angle = PPA_SRM_ROTATION_ANGLE_270;
            break;
        default:
            return false;
    }
    return true;
}

static int open_ppa(gmf_video_ppa_t *vid_cvt)
{
    esp_gmf_info_video_t *src_info = &vid_cvt->parent.src_info;
    ppa_client_config_t ppa_client_config = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
    };
    ppa_register_client(&ppa_client_config, &vid_cvt->ppa_handle);
    ESP_GMF_MEM_CHECK(TAG, vid_cvt->ppa_handle, return ESP_GMF_ERR_NOT_ENOUGH);
    memset(&vid_cvt->ppa_config, 0, sizeof(ppa_srm_oper_config_t));
    uint32_t in_block_w = src_info->width;
    uint32_t in_block_h = src_info->height;
    float scale_x, scale_y;
    if (vid_cvt->crop_rgn.width) {
        in_block_w = vid_cvt->crop_rgn.width;
        in_block_h = vid_cvt->crop_rgn.height;
        vid_cvt->ppa_config.in.block_offset_x = vid_cvt->crop_rgn.x;
        vid_cvt->ppa_config.in.block_offset_y = vid_cvt->crop_rgn.y;
    }
    if (vid_cvt->rotate_degree == 0 || vid_cvt->rotate_degree == 180) {
        scale_x = 1.0 * vid_cvt->dst_width / in_block_w;
        scale_y = 1.0 * vid_cvt->dst_height / in_block_h;
    } else {
        scale_x = 1.0 * vid_cvt->dst_height / in_block_w;
        scale_y = 1.0 * vid_cvt->dst_width / in_block_h;
    }
    uint32_t ppa_src_fmt = vid_cvt->soft_in ? vid_cvt->sw_ppa_in_fmt : src_info->format_id;
    uint32_t ppa_dst_fmt = vid_cvt->soft_out ? vid_cvt->sw_ppa_out_fmt : vid_cvt->dst_format;
    vid_cvt->ppa_config.in.pic_w = src_info->width;
    vid_cvt->ppa_config.in.pic_h = src_info->height;
    vid_cvt->ppa_config.in.block_w = in_block_w;
    vid_cvt->ppa_config.in.block_h = in_block_h;
    vid_cvt->ppa_config.in.srm_cm = get_ppa_clr_mode(ppa_src_fmt);

    vid_cvt->ppa_config.out.pic_w = vid_cvt->dst_width;
    vid_cvt->ppa_config.out.pic_h = vid_cvt->dst_height;
    vid_cvt->ppa_config.out.srm_cm = get_ppa_clr_mode(ppa_dst_fmt);

    vid_cvt->ppa_config.rgb_swap = 0;
    vid_cvt->ppa_config.byte_swap = 0;
    check_ppa_supported_formats(vid_cvt, ppa_src_fmt, ppa_dst_fmt);
    vid_cvt->ppa_config.mode = PPA_TRANS_MODE_BLOCKING;

    vid_cvt->ppa_config.scale_x = scale_x;
    vid_cvt->ppa_config.scale_y = scale_y;
    if (check_rotation_angle(vid_cvt->rotate_degree, &vid_cvt->ppa_config.rotation_angle) == false) {
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    return ESP_GMF_ERR_OK;
}

static int close_ppa(gmf_video_ppa_t *vid_cvt)
{
    if (vid_cvt->ppa_handle) {
        ppa_unregister_client(vid_cvt->ppa_handle);
        vid_cvt->ppa_handle = NULL;
    }
    return 0;
}

static int ppa_convert(gmf_video_ppa_t *vid_cvt, esp_gmf_payload_t *in_load, esp_gmf_payload_t *out_load)
{
    if (vid_cvt->supported == false) {
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    ESP_GMF_NULL_CHECK(TAG, vid_cvt->ppa_handle, return ESP_GMF_ERR_NOT_SUPPORT);
    vid_cvt->ppa_config.in.buffer = in_load->buf;
    vid_cvt->ppa_config.out.buffer = out_load->buf;
    vid_cvt->ppa_config.out.buffer_size = out_load->buf_length;
    ppa_srm_lock();
    int err = ppa_do_scale_rotate_mirror(vid_cvt->ppa_handle, &vid_cvt->ppa_config);
    ppa_srm_unlock();
    return err;
}

static void sw_buf_flush_for_hw_read(const void *buf, size_t len)
{
    if (buf && len) {
        esp_cache_msync((void *)buf, len, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    }
}

static void sw_buf_invalidate_after_hw_write(void *buf, size_t len)
{
    if (buf && len) {
        esp_cache_msync(buf, len, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    }
}

static esp_gmf_job_err_t alloc_sw_scratch(gmf_video_ppa_t *v)
{
    esp_gmf_info_video_t *src = &v->parent.src_info;
    uint32_t sz_in_bridge = pixel_buffer_size(v->sw_ppa_in_fmt, src->width, src->height);
    uint32_t sz_out_bridge = pixel_buffer_size(v->sw_ppa_out_fmt, v->dst_width, v->dst_height);
    if (sz_in_bridge == 0 || sz_out_bridge == 0) {
        return ESP_GMF_JOB_ERR_FAIL;
    }
    v->soft_rgb_dst_offs = 0;
    if (v->soft_in && v->soft_out) {
        if (v->sw_both_reuse_out) {
            v->soft_tmp_len = sz_out_bridge;
        } else {
            v->soft_tmp_len = sz_in_bridge + sz_out_bridge;
            v->soft_rgb_dst_offs = sz_in_bridge;
        }
    } else if (v->soft_in) {
        v->soft_tmp_len = sz_in_bridge;
    } else {
        v->soft_tmp_len = sz_out_bridge;
    }
    uint8_t align = esp_gmf_oal_get_spiram_cache_align();
    v->soft_tmp = (uint8_t *)heap_caps_aligned_calloc(align, 1, v->soft_tmp_len, MALLOC_CAP_SPIRAM);
    ESP_GMF_MEM_CHECK(TAG, v->soft_tmp, {
        v->soft_tmp_len = 0;
        return ESP_GMF_JOB_ERR_FAIL;
    });
    return ESP_GMF_JOB_ERR_OK;
}

static void free_sw_scratch(gmf_video_ppa_t *v)
{
    if (v->soft_tmp) {
        heap_caps_free(v->soft_tmp);
        v->soft_tmp = NULL;
    }
    v->soft_tmp_len = 0;
    v->soft_rgb_dst_offs = 0;
}

static void close_sw_color_convert(gmf_video_ppa_t *v)
{
    if (v->sw_imgfx_in) {
        esp_imgfx_color_convert_close(v->sw_imgfx_in);
        v->sw_imgfx_in = NULL;
    }
    if (v->sw_imgfx_out) {
        esp_imgfx_color_convert_close(v->sw_imgfx_out);
        v->sw_imgfx_out = NULL;
    }
}

static esp_gmf_job_err_t open_sw_color_convert(gmf_video_ppa_t *v)
{
    esp_gmf_info_video_t *src = &v->parent.src_info;
    esp_imgfx_color_convert_cfg_t cfg = {
        .in_res = {.width = src->width, .height = src->height},
        .color_space_std = ESP_IMGFX_COLOR_SPACE_STD_BT601,
    };
    if (v->soft_in) {
        cfg.in_pixel_fmt = src->format_id;
        cfg.out_pixel_fmt = v->sw_ppa_in_fmt;
        if (esp_imgfx_color_convert_open(&cfg, &v->sw_imgfx_in) != ESP_IMGFX_ERR_OK) {
            ESP_LOGE(TAG, "Software color convert (pre-PPA) open failed");
            return ESP_GMF_JOB_ERR_FAIL;
        }
    }
    if (v->soft_out) {
        cfg.in_res.width = v->dst_width;
        cfg.in_res.height = v->dst_height;
        cfg.in_pixel_fmt = v->sw_ppa_out_fmt;
        cfg.out_pixel_fmt = v->dst_format;
        if (esp_imgfx_color_convert_open(&cfg, &v->sw_imgfx_out) != ESP_IMGFX_ERR_OK) {
            ESP_LOGE(TAG, "Software color convert (post-PPA) open failed");
            return ESP_GMF_JOB_ERR_FAIL;
        }
    }
    return ESP_GMF_JOB_ERR_OK;
}

static inline int vid_ppa_imgfx_sw(esp_imgfx_color_convert_handle_t h, void *in_ptr, size_t in_len, void *out_ptr, size_t out_len)
{
    esp_imgfx_data_t im_in = {.data = in_ptr, .data_len = in_len};
    esp_imgfx_data_t im_out = {.data = out_ptr, .data_len = out_len};
    return esp_imgfx_color_convert_process(h, &im_in, &im_out) == ESP_IMGFX_ERR_OK ? 0 : ESP_GMF_ERR_FAIL;
}

static int video_ppa_run_convert(gmf_video_ppa_t *v, esp_gmf_payload_t *in_load, esp_gmf_payload_t *out_load)
{
    if (v->supported == false) {
        return ESP_GMF_ERR_NOT_SUPPORT;
    }

    /* Software-only: single imgfx, no PPA / 2D-DMA. */
    if (v->sw_imgfx_only) {
        return vid_ppa_imgfx_sw(v->sw_imgfx_in, in_load->buf, in_load->valid_size, out_load->buf, out_load->buf_length);
    }

    const bool pre = v->soft_in;
    const bool post = v->soft_out;

    /* Hardware color only (2D-DMA or PPA). */
    if (!pre && !post) {
        return v->use_ppa ? ppa_convert(v, in_load, out_load) : dm2d_convert(v, in_load, out_load);
    }

    esp_gmf_info_video_t *src = &v->parent.src_info;
    const uint32_t sz_in_br = pixel_buffer_size(v->sw_ppa_in_fmt, src->width, src->height);
    const uint32_t sz_out_br = pixel_buffer_size(v->sw_ppa_out_fmt, v->dst_width, v->dst_height);
    ESP_GMF_NULL_CHECK(TAG, v->soft_tmp, return ESP_GMF_ERR_NOT_SUPPORT);

    int err;
    esp_gmf_payload_t mid_in;
    esp_gmf_payload_t mid_out;

    /* Pre-imgfx -> PPA -> post-imgfx (reuse acquired out as bridge input). */
    if (pre && post && v->sw_both_reuse_out) {
        if (out_load->buf_length < sz_in_br) {
            return ESP_GMF_ERR_FAIL;
        }
        err = vid_ppa_imgfx_sw(v->sw_imgfx_in, in_load->buf, in_load->valid_size, out_load->buf, sz_in_br);
        if (err != 0) {
            return err;
        }
        sw_buf_flush_for_hw_read(out_load->buf, sz_in_br);
        mid_in = (esp_gmf_payload_t) {
            .buf = out_load->buf,
            .buf_length = out_load->buf_length,
            .valid_size = sz_in_br,
        };
        mid_out = (esp_gmf_payload_t) {
            .buf = v->soft_tmp,
            .buf_length = sz_out_br,
            .valid_size = sz_out_br,
        };
        err = ppa_convert(v, &mid_in, &mid_out);
        if (err != 0) {
            return err;
        }
        return vid_ppa_imgfx_sw(v->sw_imgfx_out, v->soft_tmp, sz_out_br, out_load->buf, out_load->buf_length);
    }

    /* Pre-imgfx -> PPA -> out. */
    if (pre && !post) {
        err = vid_ppa_imgfx_sw(v->sw_imgfx_in, in_load->buf, in_load->valid_size, v->soft_tmp, sz_in_br);
        if (err != 0) {
            return err;
        }
        sw_buf_flush_for_hw_read(v->soft_tmp, sz_in_br);
        mid_in = (esp_gmf_payload_t) {
            .buf = v->soft_tmp,
            .buf_length = sz_in_br,
            .valid_size = sz_in_br,
        };
        return ppa_convert(v, &mid_in, out_load);
    }

    /* PPA -> scratch -> post-imgfx. */
    if (!pre && post) {
        mid_out = (esp_gmf_payload_t) {
            .buf = v->soft_tmp, .buf_length = sz_out_br, .valid_size = sz_out_br,
        };
        err = ppa_convert(v, in_load, &mid_out);
        if (err != 0) {
            return err;
        }
        sw_buf_invalidate_after_hw_write(v->soft_tmp, sz_out_br);
        return vid_ppa_imgfx_sw(v->sw_imgfx_out, v->soft_tmp, sz_out_br, out_load->buf, out_load->buf_length);
    }

    /* Pre-imgfx -> PPA (two scratch regions) -> post-imgfx. */
    err = vid_ppa_imgfx_sw(v->sw_imgfx_in, in_load->buf, in_load->valid_size, v->soft_tmp, sz_in_br);
    if (err != 0) {
        return err;
    }
    uint8_t *ppa_mid_out = v->soft_tmp + v->soft_rgb_dst_offs;
    mid_in = (esp_gmf_payload_t) {
        .buf = v->soft_tmp,
        .buf_length = sz_in_br,
        .valid_size = sz_in_br,
    };
    mid_out = (esp_gmf_payload_t) {
        .buf = ppa_mid_out,
        .buf_length = sz_out_br,
        .valid_size = sz_out_br,
    };
    err = ppa_convert(v, &mid_in, &mid_out);
    if (err != 0) {
        return err;
    }
    return vid_ppa_imgfx_sw(v->sw_imgfx_out, ppa_mid_out, sz_out_br, out_load->buf, out_load->buf_length);
}

static bool vid_ppa_geometry_needs_hw(const gmf_video_ppa_t *vid_cvt)
{
    const esp_gmf_info_video_t *src_info = &vid_cvt->parent.src_info;
    if (src_info->width != vid_cvt->dst_width || src_info->height != vid_cvt->dst_height) {
        return true;
    }
    if (vid_cvt->rotate_degree != 0) {
        return true;
    }
    if (vid_cvt->crop_rgn.width > 0) {
        return true;
    }
    return false;
}

static bool dma2d_m2m_transaction_done_cb(dma2d_channel_handle_t dma2d_chan, dma2d_event_data_t *event_data, void *user_data)
{
    bool need_yield = false;
    dma2d_m2m_transaction_t *trans_config = (dma2d_m2m_transaction_t *)user_data;
    dma2d_m2m_trans_config_t *m2m_trans_desc = &trans_config->m2m_trans_desc;
    if (m2m_trans_desc->trans_eof_cb) {
        need_yield |= m2m_trans_desc->trans_eof_cb(m2m_trans_desc->user_data);
    }
    return need_yield;
}

static bool dma2d_m2m_transaction_on_picked(uint32_t channel_num, const dma2d_trans_channel_info_t *dma2d_chans, void *user_config)
{
    dma2d_m2m_transaction_t *trans_config = (dma2d_m2m_transaction_t *)user_config;
    dma2d_m2m_trans_config_t *m2m_trans_desc = &trans_config->m2m_trans_desc;
    // Get the required 2D-DMA channel handles
    uint32_t dma_tx_chan_idx = 0;
    uint32_t dma_rx_chan_idx = 1;
    if (dma2d_chans[0].dir == DMA2D_CHANNEL_DIRECTION_RX) {
        dma_tx_chan_idx = 1;
        dma_rx_chan_idx = 0;
    }

    dma2d_channel_handle_t dma_tx_chan = dma2d_chans[dma_tx_chan_idx].chan;
    dma2d_channel_handle_t dma_rx_chan = dma2d_chans[dma_rx_chan_idx].chan;
    dma2d_trigger_t trig_periph = {
        .periph = DMA2D_TRIG_PERIPH_M2M,
        .periph_sel_id = SOC_DMA2D_TRIG_PERIPH_M2M_TX,
    };
    dma2d_connect(dma_tx_chan, &trig_periph);
    trig_periph.periph_sel_id = SOC_DMA2D_TRIG_PERIPH_M2M_RX;
    dma2d_connect(dma_rx_chan, &trig_periph);

    dma2d_set_transfer_ability(dma_tx_chan, &m2m_trans_desc->transfer_ability);
    dma2d_set_transfer_ability(dma_rx_chan, &m2m_trans_desc->transfer_ability);

    if (m2m_trans_desc->tx_strategy_config) {
        dma2d_apply_strategy(dma_tx_chan, m2m_trans_desc->tx_strategy_config);
    }
    if (m2m_trans_desc->rx_strategy_config) {
        dma2d_apply_strategy(dma_rx_chan, m2m_trans_desc->rx_strategy_config);
    }

    if (m2m_trans_desc->tx_csc_config) {
        dma2d_configure_color_space_conversion(dma_tx_chan, m2m_trans_desc->tx_csc_config);
    }
    if (m2m_trans_desc->rx_csc_config) {
        dma2d_configure_color_space_conversion(dma_rx_chan, m2m_trans_desc->rx_csc_config);
    }
    dma2d_rx_event_callbacks_t dma_cbs = {
        .on_recv_eof = dma2d_m2m_transaction_done_cb,
    };
    dma2d_register_rx_event_callbacks(dma_rx_chan, &dma_cbs, (void *)trans_config);
    dma2d_set_desc_addr(dma_tx_chan, m2m_trans_desc->tx_desc_base_addr);
    dma2d_set_desc_addr(dma_rx_chan, m2m_trans_desc->rx_desc_base_addr);
    dma2d_start(dma_tx_chan);
    dma2d_start(dma_rx_chan);
    return false;
}

static bool IRAM_ATTR dma2d_m2m_suc_eof_event_cb(void *user_data)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    SemaphoreHandle_t sem = (SemaphoreHandle_t)user_data;
    xSemaphoreGiveFromISR(sem, &xHigherPriorityTaskWoken);
    return (xHigherPriorityTaskWoken == pdTRUE);
}

static void dma2d_link_dscr_init(dma2d_descriptor_t *dma2d, uint32_t *next, void *buf_ptr, uint32_t ha, uint32_t va,
                                 uint32_t hb, uint32_t vb, uint32_t eof, uint32_t en_2d, uint32_t pbyte, uint32_t mod,
                                 uint32_t bias_x, uint32_t bias_y)
{
    dma2d->owner = DMA2D_DESCRIPTOR_BUFFER_OWNER_DMA;
    dma2d->suc_eof = eof;
    dma2d->dma2d_en = en_2d;
    dma2d->err_eof = 0;
    dma2d->hb_length = hb;
    dma2d->vb_size = vb;
    dma2d->pbyte = pbyte;
    dma2d->ha_length = ha;
    dma2d->va_size = va;
    dma2d->mode = mod;
    dma2d->y = bias_y;
    dma2d->x = bias_x;
    dma2d->buffer = buf_ptr;
    dma2d->next = (dma2d_descriptor_t *)next;
}

static int open_dma2d(gmf_video_ppa_t *vid_cvt)
{
    esp_gmf_info_video_t *src_info = &vid_cvt->parent.src_info;
    dma2d_pool_config_t pool_config = {
        .pool_id = 0,
    };
    dma2d_info_t *dma2d = &vid_cvt->dma2d_info;
    int ret = dma2d_acquire_pool(&pool_config, &dma2d->handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fail to allocate for DMA2D");
        return ret;
    }
    uint8_t align = esp_gmf_oal_get_spiram_cache_align();
    dma2d->tx_desc = (dma2d_descriptor_t *)heap_caps_aligned_calloc(align, 1, 64, MALLOC_CAP_SPIRAM);
    dma2d->rx_desc = (dma2d_descriptor_t *)heap_caps_aligned_calloc(align, 1, 64, MALLOC_CAP_SPIRAM);
    dma2d->sema = xSemaphoreCreateCounting(1, 0);
    if (dma2d->sema == NULL || dma2d->rx_desc == NULL || dma2d->tx_desc == NULL) {
        ESP_GMF_MEM_CHECK(TAG, NULL, return ESP_GMF_ERR_MEMORY_LACK);
    }
    dma2d->trans.dma_chan_desc.tx_channel_num = 1;
    dma2d->trans.dma_chan_desc.rx_channel_num = 1;
    dma2d->trans.dma_chan_desc.channel_flags = DMA2D_CHANNEL_FUNCTION_FLAG_SIBLING;
    dma2d->trans.dma_chan_desc.specified_tx_channel_mask = 0;
    dma2d->trans.dma_chan_desc.specified_rx_channel_mask = 0;
    dma2d->trans.dma_chan_desc.user_config = (void *)&dma2d->trans;
    dma2d->trans.dma_chan_desc.on_job_picked = dma2d_m2m_transaction_on_picked;
    if ((src_info->format_id == ESP_FOURCC_RGB24 || src_info->format_id == ESP_FOURCC_BGR24) &&
        (vid_cvt->dst_format == ESP_FOURCC_RGB16 || vid_cvt->dst_format == ESP_FOURCC_RGB16_BE)) {
        dma2d->tx_cvt.tx_csc_option = DMA2D_CSC_TX_RGB888_TO_RGB565;
        dma2d->tx_cvt.pre_scramble = DMA2D_SCRAMBLE_ORDER_BYTE0_1_2;
        dma2d->trans.m2m_trans_desc.tx_csc_config = &dma2d->tx_cvt;
    } else if ((src_info->format_id == ESP_FOURCC_RGB16_BE || src_info->format_id == ESP_FOURCC_RGB16) &&
               (vid_cvt->dst_format == ESP_FOURCC_RGB24 || vid_cvt->dst_format == ESP_FOURCC_BGR24)) {
        dma2d->tx_cvt.tx_csc_option = DMA2D_CSC_TX_RGB565_TO_RGB888;
        dma2d->tx_cvt.pre_scramble = DMA2D_SCRAMBLE_ORDER_BYTE2_1_0;
        dma2d->trans.m2m_trans_desc.tx_csc_config = &dma2d->tx_cvt;
    }
    check_2ddma_supported(vid_cvt);
    if (dma2d->trans.m2m_trans_desc.tx_csc_config) {
        dma2d->trans.dma_chan_desc.channel_flags |= DMA2D_CHANNEL_FUNCTION_FLAG_TX_CSC;
    }
    if (dma2d->trans.m2m_trans_desc.rx_csc_config) {
        dma2d->trans.dma_chan_desc.channel_flags |= DMA2D_CHANNEL_FUNCTION_FLAG_RX_CSC;
    }
    // TODO support other color conversion
    dma2d->trans.m2m_trans_desc.tx_desc_base_addr = (intptr_t)dma2d->tx_desc;
    dma2d->trans.m2m_trans_desc.rx_desc_base_addr = (intptr_t)dma2d->rx_desc;

    dma2d->trans.m2m_trans_desc.trans_eof_cb = dma2d_m2m_suc_eof_event_cb;
    dma2d->trans.m2m_trans_desc.user_data = (void *)dma2d->sema;
    dma2d_transfer_ability_t *trans_ability = &dma2d->trans.m2m_trans_desc.transfer_ability;
    trans_ability->data_burst_length = 128;
    trans_ability->desc_burst_en = true;
    trans_ability->mb_size = DMA2D_MACRO_BLOCK_SIZE_NONE;
    int src_size = get_frame_size(vid_cvt, src_info->format_id);
    int dst_size = get_frame_size(vid_cvt, vid_cvt->dst_format);

    dma2d_link_dscr_init(dma2d->tx_desc, NULL, NULL,
                         src_size >> 14, src_size >> 14,
                         src_size & 0x3FFF, src_size & 0x3FFF,
                         1, 0, DMA2D_DESCRIPTOR_PBYTE_1B0_PER_PIXEL,
                         DMA2D_DESCRIPTOR_BLOCK_RW_MODE_SINGLE, 0, 0);
    dma2d_link_dscr_init(dma2d->rx_desc, NULL, NULL,
                         0, dst_size >> 14,
                         0, dst_size & 0x3FFF,
                         0, 0, DMA2D_DESCRIPTOR_PBYTE_1B0_PER_PIXEL,
                         DMA2D_DESCRIPTOR_BLOCK_RW_MODE_SINGLE, 0, 0);
    return ESP_GMF_ERR_OK;
}

static void flush_src_dst_data(gmf_video_ppa_t *vid_cvt, esp_gmf_payload_t *in_load, esp_gmf_payload_t *out_load)
{
    esp_cache_msync((void *)in_load->buf, in_load->buf_length, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    esp_cache_msync((void *)out_load->buf, out_load->buf_length, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

static void invalid_dst_data(gmf_video_ppa_t *vid_cvt, esp_gmf_payload_t *out_load)
{
    esp_cache_msync(out_load->buf, out_load->buf_length, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
}

static int dm2d_convert(gmf_video_ppa_t *vid_cvt, esp_gmf_payload_t *in_load, esp_gmf_payload_t *out_load)
{
    if (vid_cvt->supported == false) {
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    dma2d_info_t *dma2d = &vid_cvt->dma2d_info;
    flush_src_dst_data(vid_cvt, in_load, out_load);
    // Set buffer for TX, RX desc
    dma2d->tx_desc->buffer = (void *)in_load->buf;
    dma2d->rx_desc->buffer = (void *)out_load->buf;
    esp_cache_msync(dma2d->tx_desc, 64, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    esp_cache_msync(dma2d->rx_desc, 64, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    int ret = dma2d_enqueue(dma2d->handle, &dma2d->trans.dma_chan_desc,
                            (dma2d_trans_t *)dma2d->trans.dma_trans_placeholder_head);
    if (ret != ESP_OK) {
        return ret;
    }
    // Wait for DMA transfer to complete
    xSemaphoreTake(dma2d->sema, portMAX_DELAY);
    invalid_dst_data(vid_cvt, out_load);
    return 0;
}

static void close_dma2d(gmf_video_ppa_t *vid_cvt)
{
    dma2d_info_t *dma2d = &vid_cvt->dma2d_info;
    if (dma2d->handle) {
        dma2d_release_pool(dma2d->handle);
        dma2d->handle = NULL;
    }
    if (dma2d->sema) {
        vSemaphoreDelete(dma2d->sema);
        dma2d->sema = NULL;
    }
    if (dma2d->tx_desc) {
        heap_caps_free(dma2d->tx_desc);
        dma2d->tx_desc = NULL;
    }
    if (dma2d->rx_desc) {
        heap_caps_free(dma2d->rx_desc);
        dma2d->rx_desc = NULL;
    }
}
#endif  /* CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31 */

static esp_gmf_job_err_t gmf_video_ppa_open(esp_gmf_element_handle_t self, void *para)
{
    gmf_video_ppa_t *vid_cvt = (gmf_video_ppa_t *)self;
    esp_gmf_job_err_t ret = ESP_GMF_JOB_ERR_OK;
    esp_gmf_oal_mutex_lock(((esp_gmf_video_element_t *)self)->lock);
    esp_gmf_info_video_t *src_info = &vid_cvt->parent.src_info;
    esp_gmf_info_video_t vid_info = vid_cvt->parent.src_info;
    // Set Default info
    if (vid_cvt->dst_width == 0) {
        vid_cvt->dst_width = src_info->width;
        if (vid_cvt->rotate_degree != 0 && vid_cvt->rotate_degree != 180) {
            vid_cvt->dst_width = src_info->height;
        }
    }
    if (vid_cvt->dst_height == 0) {
        vid_cvt->dst_height = src_info->height;
        if (vid_cvt->rotate_degree != 0 && vid_cvt->rotate_degree != 180) {
            vid_cvt->dst_height = src_info->width;
        }
    }
    if (vid_cvt->dst_format == 0) {
        vid_cvt->dst_format = src_info->format_id;
    }
    if (vid_cvt->crop_rgn.width) {
        if ((vid_cvt->crop_rgn.x + vid_cvt->crop_rgn.width > src_info->width) || (vid_cvt->crop_rgn.y + vid_cvt->crop_rgn.height > src_info->height)) {
            ESP_LOGE(TAG, "Crop over limited w: %d + %d > %d h: %d + %d > %d",
                     vid_cvt->crop_rgn.x, vid_cvt->crop_rgn.width, src_info->width,
                     vid_cvt->crop_rgn.y, vid_cvt->crop_rgn.height, src_info->height);
            ret = ESP_GMF_JOB_ERR_FAIL;
            goto __video_ppa_open_exit;
        }
    }
    vid_cvt->bypass = false;
    if (vid_cvt->dst_width == src_info->width && vid_cvt->dst_height == src_info->height &&
        vid_cvt->dst_format == src_info->format_id && vid_cvt->rotate_degree == 0) {
        vid_cvt->bypass = true;
    }
    if (vid_cvt->bypass == false) {
#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31
        bool dma2d_ok = check_2ddma_supported(vid_cvt);
        bool hw_native = hw_native_accepts_src_dst(vid_cvt);
        bool geom = vid_ppa_geometry_needs_hw(vid_cvt);
        vid_cvt->sw_imgfx_only = false;
        vid_cvt->soft_in = false;
        vid_cvt->soft_out = false;
        vid_cvt->sw_both_reuse_out = false;
        if (!hw_native && !dma2d_ok && !geom &&
            imgfx_color_convert_supported(src_info->format_id, vid_cvt->dst_format)) {
            esp_imgfx_color_convert_cfg_t icfg = {
                .in_res = {.width = src_info->width, .height = src_info->height},
                .in_pixel_fmt = src_info->format_id,
                .out_pixel_fmt = vid_cvt->dst_format,
                .color_space_std = ESP_IMGFX_COLOR_SPACE_STD_BT601,
            };
            if (esp_imgfx_color_convert_open(&icfg, &vid_cvt->sw_imgfx_in) == ESP_IMGFX_ERR_OK) {
                vid_cvt->sw_imgfx_only = true;
                vid_cvt->supported = true;
                vid_cvt->use_ppa = false;
                vid_cvt->out_frame_size = get_frame_size(vid_cvt, vid_cvt->dst_format);
                int cache_line_size = esp_gmf_oal_get_spiram_cache_align();
                ;
                ESP_GMF_ELEMENT_GET(vid_cvt)->out_attr.data_size = GMF_VIDEO_ALIGN_UP(vid_cvt->out_frame_size, cache_line_size);
                vid_cvt->sw_ppa_in_fmt = src_info->format_id;
                vid_cvt->sw_ppa_out_fmt = vid_cvt->dst_format;
                goto __vid_ppa_log_open_info;
            }
        }
        if (!hw_native) {
            if (!ppa_is_src_supported(src_info->format_id)) {
                vid_cvt->soft_in = true;
            }
            if (!ppa_is_dst_support(vid_cvt->dst_format)) {
                vid_cvt->soft_out = true;
            }
        }
        if (vid_cvt->soft_in || vid_cvt->soft_out) {
            vid_cvt->sw_ppa_in_fmt = pick_ppa_in_bridge_fmt(src_info->format_id, src_info->width, src_info->height);
            vid_cvt->sw_ppa_out_fmt = pick_ppa_out_bridge_fmt(vid_cvt->dst_format, vid_cvt->dst_width, vid_cvt->dst_height);
            if (vid_cvt->soft_in && vid_cvt->soft_out &&
                src_info->width == vid_cvt->dst_width && src_info->height == vid_cvt->dst_height &&
                pixel_buffer_size(vid_cvt->sw_ppa_in_fmt, src_info->width, src_info->height) ==
                    pixel_buffer_size(vid_cvt->dst_format, vid_cvt->dst_width, vid_cvt->dst_height)) {
                vid_cvt->sw_both_reuse_out = true;
            }
        } else {
            vid_cvt->sw_ppa_in_fmt = src_info->format_id;
            vid_cvt->sw_ppa_out_fmt = vid_cvt->dst_format;
        }
        uint32_t ppa_src_try = vid_cvt->soft_in ? vid_cvt->sw_ppa_in_fmt : src_info->format_id;
        uint32_t ppa_dst_try = vid_cvt->soft_out ? vid_cvt->sw_ppa_out_fmt : vid_cvt->dst_format;
        bool ppa_eff_ok = ppa_native_pair_ok(ppa_src_try, ppa_dst_try);
        vid_cvt->supported = hw_native || ppa_eff_ok;
        if (vid_cvt->supported == false) {
            ESP_LOGE(TAG, "Not support convert from %s to %s",
                     esp_gmf_video_get_format_string(src_info->format_id), esp_gmf_video_get_format_string(vid_cvt->dst_format));
            ret = ESP_GMF_ERR_NOT_SUPPORT;
            goto __video_ppa_open_exit;
        }
        // TODO decide to use DMA2D or PPA
        vid_cvt->out_frame_size = get_frame_size(vid_cvt, vid_cvt->dst_format);
        // Allocate memory for output frame
        int cache_line_size = esp_gmf_oal_get_spiram_cache_align();
        ;
        ESP_GMF_ELEMENT_GET(vid_cvt)->out_attr.data_size = GMF_VIDEO_ALIGN_UP(vid_cvt->out_frame_size, cache_line_size);
        if (dma2d_ok) {
            vid_cvt->use_ppa = false;
        } else {
            vid_cvt->use_ppa = geom || vid_cvt->soft_in || vid_cvt->soft_out || ppa_native_pair_ok(src_info->format_id, vid_cvt->dst_format);
        }
        if (vid_cvt->use_ppa) {
            ret = open_ppa(vid_cvt);
        } else {
            ret = open_dma2d(vid_cvt);
        }
        if (ret != ESP_GMF_JOB_ERR_OK) {
            goto __video_ppa_open_exit;
        }
        if (vid_cvt->soft_in || vid_cvt->soft_out) {
            esp_gmf_job_err_t sret = alloc_sw_scratch(vid_cvt);
            if (sret == ESP_GMF_JOB_ERR_OK) {
                sret = open_sw_color_convert(vid_cvt);
            }
            if (sret != ESP_GMF_JOB_ERR_OK) {
                close_sw_color_convert(vid_cvt);
                free_sw_scratch(vid_cvt);
                if (vid_cvt->use_ppa) {
                    close_ppa(vid_cvt);
                } else {
                    close_dma2d(vid_cvt);
                }
                ret = sret;
                goto __video_ppa_open_exit;
            }
        }
    __vid_ppa_log_open_info:
        vid_info.format_id = vid_cvt->dst_format;
        vid_info.width = vid_cvt->dst_width;
        vid_info.height = vid_cvt->dst_height;
        ESP_LOGI(TAG, "Convert in %s %dx%d to %s %dx%d ppa:%d soft_in:%d soft_out:%d reuse_out:%d hw_native:%d imgfx_only:%d geom:%d bridge %s->%s",
                 esp_gmf_video_get_format_string(src_info->format_id), (int)src_info->width, (int)src_info->height,
                 esp_gmf_video_get_format_string(vid_info.format_id), (int)vid_info.width, (int)vid_info.height,
                 vid_cvt->use_ppa, vid_cvt->soft_in, vid_cvt->soft_out, vid_cvt->sw_both_reuse_out, hw_native,
                 vid_cvt->sw_imgfx_only, geom,
                 esp_gmf_video_get_format_string(vid_cvt->sw_ppa_in_fmt),
                 esp_gmf_video_get_format_string(vid_cvt->sw_ppa_out_fmt));
#else
        ESP_LOGE(TAG, "Not support video convert hardware not supported");
        ret = ESP_GMF_JOB_ERR_FAIL;
        goto __video_ppa_open_exit;
#endif  /* CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31 */
    }
__video_ppa_open_exit:
    esp_gmf_oal_mutex_unlock(((esp_gmf_video_element_t *)self)->lock);
    if (ret == ESP_GMF_JOB_ERR_OK) {
        esp_gmf_element_notify_vid_info(self, &vid_info);
    }
    return ret;
}

static esp_gmf_job_err_t gmf_video_ppa_process(esp_gmf_element_handle_t self, void *para)
{
    gmf_video_ppa_t *vid_cvt = (gmf_video_ppa_t *)self;
    int ret = 0;
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    ret = esp_gmf_port_acquire_in(in_port, &in_load, ESP_GMF_ELEMENT_GET(self)->in_attr.data_size, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_IN_CHECK(TAG, ret, ret, return ret);
    uint32_t wanted_size = 0;
    if (vid_cvt->bypass) {
        out_load = in_load;
        wanted_size = in_load->valid_size;
    } else {
        out_load = NULL;
        wanted_size = ESP_GMF_ELEMENT_GET(vid_cvt)->out_attr.data_size;
    }
    ret = esp_gmf_port_acquire_out(out_port, &out_load, wanted_size, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, ret, ret, esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY); return ret);
    if (in_load->valid_size > 0 && vid_cvt->bypass == false) {
#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31
        if (vid_cvt->sw_imgfx_only || vid_cvt->use_ppa) {
            ret = video_ppa_run_convert(vid_cvt, in_load, out_load);
        } else {
            ret = dm2d_convert(vid_cvt, in_load, out_load);
        }
        if (ret == 0) {
            out_load->valid_size = vid_cvt->out_frame_size;
            out_load->pts = in_load->pts;
        }
#else
        ret = ESP_GMF_JOB_ERR_FAIL;
#endif  /* CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31 */
    }
    esp_gmf_port_release_out(out_port, out_load, ESP_GMF_MAX_DELAY);
    esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY);
    if (out_load->is_done) {
        ret = ESP_GMF_JOB_ERR_DONE;
    }
    return ret;
}

static esp_gmf_job_err_t gmf_video_ppa_close(esp_gmf_element_handle_t self, void *para)
{
#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31
    gmf_video_ppa_t *vid_cvt = (gmf_video_ppa_t *)self;
    close_sw_color_convert(vid_cvt);
    free_sw_scratch(vid_cvt);
    if (vid_cvt->use_ppa) {
        close_ppa(vid_cvt);
    } else if (!vid_cvt->sw_imgfx_only) {
        close_dma2d(vid_cvt);
    }
#endif  /* CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31 */
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_err_t gmf_video_ppa_destroy(esp_gmf_element_handle_t self)
{
    gmf_video_ppa_t *vid_cvt = (gmf_video_ppa_t *)self;
    esp_gmf_video_el_deinit(self);
    if (vid_cvt != NULL) {
        esp_gmf_oal_free(vid_cvt);
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t set_dst_format(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                    uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    gmf_video_ppa_t *vid_cvt = (gmf_video_ppa_t *)handle;
    vid_cvt->dst_format = *(uint32_t *)buf;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t set_dst_resolution(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                        uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    gmf_video_ppa_t *vid_cvt = (gmf_video_ppa_t *)handle;
    uint16_t *v = (uint16_t *)buf;
    vid_cvt->dst_width = *(v++);
    vid_cvt->dst_height = *v;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t set_rotation(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                  uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    gmf_video_ppa_t *vid_cvt = (gmf_video_ppa_t *)handle;
    uint16_t *v = (uint16_t *)buf;
    vid_cvt->rotate_degree = *v;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t set_crop(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                              uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    gmf_video_ppa_t *vid_cvt = (gmf_video_ppa_t *)handle;
    uint16_t *v = (uint16_t *)buf;
    vid_cvt->crop_rgn.x = *(v++);
    vid_cvt->crop_rgn.y = *(v++);
    vid_cvt->crop_rgn.width = *(v++);
    vid_cvt->crop_rgn.height = *(v++);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t gmf_video_ppa_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_video_ppa_init(cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_err_t gmf_video_ppa_load_methods(esp_gmf_element_handle_t handle)
{
    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_method_t *methods = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    do {
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(CLR_CVT, SET_DST_FMT, FMT), ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), 0);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        ret = esp_gmf_method_append(&methods, VMETHOD(CLR_CVT, SET_DST_FMT), set_dst_format, set_args);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        set_args = NULL;
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(SCALER, SET_DST_RES, WIDTH), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 0);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(SCALER, SET_DST_RES, HEIGHT), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), sizeof(uint16_t));
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_method_append(&methods, VMETHOD(SCALER, SET_DST_RES), set_dst_resolution, set_args);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        set_args = NULL;
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(ROTATOR, SET_ANGLE, DEGREE), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 0);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_method_append(&methods, VMETHOD(ROTATOR, SET_ANGLE), set_rotation, set_args);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        set_args = NULL;
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(CROP, SET_CROP_RGN, X), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 0);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(CROP, SET_CROP_RGN, Y), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 2);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(CROP, SET_CROP_RGN, WIDTH), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 4);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(CROP, SET_CROP_RGN, HEIGHT), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 6);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_method_append(&methods, VMETHOD(CROP, SET_CROP_RGN), set_crop, set_args);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ((esp_gmf_element_t *)handle)->method = methods;
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

static esp_gmf_err_t gmf_video_ppa_load_caps(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t cap = {0};
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    do {
        cap.cap_eightcc = ESP_GMF_CAPS_VIDEO_COLOR_CONVERT;
        cap.attr_fun = NULL;
        ret = esp_gmf_cap_append(&caps, &cap);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        cap.cap_eightcc = ESP_GMF_CAPS_VIDEO_SCALE;
        ret = esp_gmf_cap_append(&caps, &cap);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        cap.cap_eightcc = ESP_GMF_CAPS_VIDEO_CROP;
        ret = esp_gmf_cap_append(&caps, &cap);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        cap.cap_eightcc = ESP_GMF_CAPS_VIDEO_ROTATE;
        ret = esp_gmf_cap_append(&caps, &cap);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        ((esp_gmf_element_t *)handle)->caps = caps;
        return ret;
    } while (0);
    if (caps) {
        esp_gmf_cap_destroy(caps);
    }
    return ret;
}

esp_gmf_err_t esp_gmf_video_ppa_init(void *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_MEM_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    gmf_video_ppa_t *video_cvt = esp_gmf_oal_calloc(1, sizeof(gmf_video_ppa_t));
    ESP_GMF_MEM_CHECK(TAG, video_cvt, return ESP_GMF_ERR_MEMORY_LACK);
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)video_cvt;
    obj->new_obj = gmf_video_ppa_new;
    obj->del_obj = gmf_video_ppa_destroy;
    esp_gmf_err_t ret = esp_gmf_obj_set_tag(obj, "vid_ppa");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto _cvt_init_fail, "Failed set OBJ tag");

    uint8_t align = esp_gmf_oal_get_spiram_cache_align();
    esp_gmf_element_cfg_t el_cfg = {
        .dependency = true,
    };
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, align, align,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, align, align,
                                      ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ret = esp_gmf_video_el_init(obj, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto _cvt_init_fail, "Failed to init video convert");

    // Bind API
    gmf_video_ppa_t *rate_cvt = (gmf_video_ppa_t *)obj;
    rate_cvt->parent.base.ops.open = gmf_video_ppa_open;
    rate_cvt->parent.base.ops.process = gmf_video_ppa_process;
    rate_cvt->parent.base.ops.close = gmf_video_ppa_close;
    rate_cvt->parent.base.ops.event_receiver = esp_gmf_video_handle_events;
    rate_cvt->parent.base.ops.load_methods = gmf_video_ppa_load_methods;
    rate_cvt->parent.base.ops.load_caps = gmf_video_ppa_load_caps;

    *handle = (esp_gmf_element_handle_t)rate_cvt;
    return ESP_GMF_ERR_OK;

_cvt_init_fail:
    esp_gmf_obj_delete(obj);
    return ret;
}

esp_gmf_err_t esp_gmf_video_ppa_set_dst_format(esp_gmf_element_handle_t handle, uint32_t codec)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(((esp_gmf_video_element_t *)handle)->lock);
    const esp_gmf_method_t *method_head = NULL;
    const esp_gmf_method_t *method = NULL;
    esp_gmf_element_get_method((esp_gmf_element_handle_t)handle, &method_head);
    esp_gmf_method_found(method_head, VMETHOD(CLR_CVT, SET_DST_FMT), &method);
    ESP_GMF_NULL_CHECK(TAG, method_head, {ret = ESP_GMF_ERR_NOT_SUPPORT; goto __video_ppa_set_fmt_exit;});
    ESP_GMF_NULL_CHECK(TAG, method, {ret = ESP_GMF_ERR_NOT_SUPPORT; goto __video_ppa_set_fmt_exit;});
    uint8_t buf[4] = {0};
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(CLR_CVT, SET_DST_FMT, FMT), buf, (uint8_t *)&codec, sizeof(uint32_t));
    ret = esp_gmf_element_exe_method((esp_gmf_element_handle_t)handle, VMETHOD(CLR_CVT, SET_DST_FMT), buf, sizeof(buf));
__video_ppa_set_fmt_exit:
    esp_gmf_oal_mutex_unlock(((esp_gmf_video_element_t *)handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_video_ppa_set_cropped_rgn(esp_gmf_element_handle_t handle, esp_gmf_video_rgn_t *rgn)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, rgn, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(((esp_gmf_video_element_t *)handle)->lock);
    const esp_gmf_method_t *method_head = NULL;
    const esp_gmf_method_t *method = NULL;
    esp_gmf_element_get_method((esp_gmf_element_handle_t)handle, &method_head);
    esp_gmf_method_found(method_head, VMETHOD(CROP, SET_CROP_RGN), &method);
    ESP_GMF_NULL_CHECK(TAG, method_head, {ret = ESP_GMF_ERR_NOT_SUPPORT; goto __video_ppa_set_crop_exit;});
    ESP_GMF_NULL_CHECK(TAG, method, {ret = ESP_GMF_ERR_NOT_SUPPORT; goto __video_ppa_set_crop_exit;});
    uint8_t buf[8];
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(CROP, SET_CROP_RGN, X), buf, (uint8_t *)&rgn->x, sizeof(uint16_t));
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(CROP, SET_CROP_RGN, Y), buf, (uint8_t *)&rgn->y, sizeof(uint16_t));
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(CROP, SET_CROP_RGN, WIDTH), buf, (uint8_t *)&rgn->width, sizeof(uint16_t));
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(CROP, SET_CROP_RGN, HEIGHT), buf, (uint8_t *)&rgn->height, sizeof(uint16_t));
    ret = esp_gmf_element_exe_method((esp_gmf_element_handle_t)handle, VMETHOD(CROP, SET_CROP_RGN), buf, sizeof(buf));
__video_ppa_set_crop_exit:
    esp_gmf_oal_mutex_unlock(((esp_gmf_video_element_t *)handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_video_ppa_set_rotation(esp_gmf_element_handle_t handle, uint16_t degree)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(((esp_gmf_video_element_t *)handle)->lock);
    const esp_gmf_method_t *method_head = NULL;
    const esp_gmf_method_t *method = NULL;
    esp_gmf_element_get_method((esp_gmf_element_handle_t)handle, &method_head);
    esp_gmf_method_found(method_head, VMETHOD(ROTATOR, SET_ANGLE), &method);
    ESP_GMF_NULL_CHECK(TAG, method_head, {ret = ESP_GMF_ERR_NOT_SUPPORT; goto __video_ppa_set_rot_exit;});
    ESP_GMF_NULL_CHECK(TAG, method, {ret = ESP_GMF_ERR_NOT_SUPPORT; goto __video_ppa_set_rot_exit;});
    uint8_t buf[2] = {0};
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(ROTATOR, SET_ANGLE, DEGREE), buf, (uint8_t *)&degree, sizeof(uint16_t));
    ret = esp_gmf_element_exe_method((esp_gmf_element_handle_t)handle, VMETHOD(ROTATOR, SET_ANGLE), buf, sizeof(buf));
__video_ppa_set_rot_exit:
    esp_gmf_oal_mutex_unlock(((esp_gmf_video_element_t *)handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_video_ppa_set_dst_resolution(esp_gmf_element_handle_t handle, esp_gmf_video_resolution_t *res)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, res, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(((esp_gmf_video_element_t *)handle)->lock);
    const esp_gmf_method_t *method_head = NULL;
    const esp_gmf_method_t *method = NULL;
    esp_gmf_element_get_method((esp_gmf_element_handle_t)handle, &method_head);
    esp_gmf_method_found(method_head, VMETHOD(SCALER, SET_DST_RES), &method);
    ESP_GMF_NULL_CHECK(TAG, method_head, {ret = ESP_GMF_ERR_NOT_SUPPORT; goto __video_ppa_set_res_exit;});
    ESP_GMF_NULL_CHECK(TAG, method, {ret = ESP_GMF_ERR_NOT_SUPPORT; goto __video_ppa_set_res_exit;});
    uint8_t buf[4] = {0};
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(SCALER, SET_DST_RES, WIDTH), buf,
                           (uint8_t *)&res->width, sizeof(uint16_t));
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(SCALER, SET_DST_RES, HEIGHT),
                           buf, (uint8_t *)&res->height, sizeof(uint16_t));
    ret = esp_gmf_element_exe_method((esp_gmf_element_handle_t)handle, VMETHOD(SCALER, SET_DST_RES), buf, sizeof(buf));
__video_ppa_set_res_exit:
    esp_gmf_oal_mutex_unlock(((esp_gmf_video_element_t *)handle)->lock);
    return ret;
}

/**
 * @brief  This API is for debug only
 */
int gmf_video_ppa_test(uint32_t from_codec, int32_t to_codec, uint32_t width, uint32_t height, uint8_t *src, uint8_t *dst, int v)
{
    esp_gmf_obj_handle_t cvt = NULL;
    gmf_video_ppa_new(NULL, &cvt);
    gmf_video_ppa_t *vid_cvt = (gmf_video_ppa_t *)cvt;
    esp_gmf_info_video_t vid_info = {
        .format_id = from_codec,
        .width = width,
        .height = height,
    };
    vid_cvt->parent.src_info = vid_info;
    vid_cvt->dst_format = to_codec;
    vid_cvt->dst_width = width;
    vid_cvt->dst_height = height;
    gmf_video_ppa_open(cvt, NULL);
    int ret = 0;
#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31
    esp_gmf_payload_t in_load = {
        .buf = src,
        .buf_length = pixel_buffer_size(from_codec, width, height),
        .valid_size = pixel_buffer_size(from_codec, width, height),
    };
    esp_gmf_payload_t out_load = {
        .buf = dst,
        .buf_length = pixel_buffer_size(to_codec, vid_cvt->dst_width, vid_cvt->dst_height),
    };
    if (v != -1) {
        vid_cvt->ppa_config.rgb_swap = v & 0x1;
        vid_cvt->ppa_config.byte_swap = v & 0x2;
        dma2d_info_t *dma2d = &vid_cvt->dma2d_info;
        dma2d->tx_cvt.pre_scramble = v;
    }
    ESP_LOGI(TAG, "RGB swap:%d byteswap:%d scramble:%d", vid_cvt->ppa_config.rgb_swap, vid_cvt->ppa_config.byte_swap, v);
    ret = video_ppa_run_convert(vid_cvt, &in_load, &out_load);
#endif  /* CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31 */
    gmf_video_ppa_close(cvt, NULL);
    gmf_video_ppa_destroy(cvt);
    return ret;
}

/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_gmf_info.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_err.h"
#include "esp_gmf_video_element.h"
#include "esp_video_enc.h"
#include "esp_video_enc_reg.h"
#include "esp_video_codec_utils.h"
#include "esp_gmf_video_enc.h"
#include "gmf_video_common.h"
#include "esp_fourcc.h"
#include "esp_gmf_video_methods_def.h"
#include "esp_gmf_caps_def.h"

#define MAX_ENC_TYPE_SUPPORT         4
// TODO this is estimated value so that each frame not over the size
#define MJPEG_ENC_MIN_COMPRESS_RATIO 10
#define H264_ENC_MIN_COMPRESS_RATIO  2

#define COPY_ARG(dst, src, offset, size, total_len) \
    if (offset + size <= total_len) {               \
        memcpy(dst, src + offset, size);            \
        offset += size;                             \
    }

#define ADD_ARG_DESC(desc, type, size, offset)                           \
    ret = esp_gmf_args_desc_append(&set_args, desc, type, size, offset); \
    GMF_VIDEO_BREAK_ON_FAIL(ret);                                        \
    offset += size;

static const char *TAG = "VENC_EL";

/**
 * @brief  Video encoder extra setting mask
 */
typedef enum {
    VENC_EXTRA_SET_MASK_NONE    = 0,
    VENC_EXTRA_SET_MASK_BITRATE = (1 << 0),
    VENC_EXTRA_SET_MASK_QP      = (1 << 1),
    VENC_EXTRA_SET_MASK_GOP     = (1 << 2),
    VENC_EXTRA_SET_MASK_ALL     = (0xFF),
} venc_extra_set_mask_t;

/**
 * @brief  Video encoder extra setting
 */
typedef struct {
    uint32_t               bitrate;
    uint32_t               min_qp;
    uint32_t               max_qp;
    uint32_t               gop;
    venc_extra_set_mask_t  mask;
} venc_extra_set_t;

/**
 * @brief  Video encoder definition
 */
typedef struct {
    esp_gmf_video_element_t  parent;       /*!< Video element parent */
    esp_video_codec_type_t   dst_codec;    /*!< Video encoder destination codec */
    bool                     venc_bypass;  /*!< Whether video encoder is bypassed or not */
    uint32_t                 codec_cc;     /*!< FourCC used to find encoder if set */
    esp_video_enc_handle_t   enc_handle;   /*!< Video encoder handle */
    venc_extra_set_t         extra_set;    /*!< Video encoder extra setting */
} venc_t;

static int venc_get_input_codecs(venc_t *venc, uint32_t dst_codec, const uint32_t **codecs, uint8_t *num)
{
    esp_video_enc_caps_t caps = {};
    esp_video_codec_query_t query = {
        .codec_type = (esp_video_codec_type_t)dst_codec,
        .codec_cc = venc->codec_cc,
    };
    int ret = esp_video_enc_query_caps(&query, &caps);
    if (ret != ESP_VC_ERR_OK) {
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    *codecs = (const uint32_t *)caps.in_fmts;
    *num = caps.in_fmt_num;
    return ESP_GMF_ERR_OK;
}

static bool venc_is_codec_supported(venc_t *venc, uint32_t src_codec, uint32_t dst_codec)
{
    esp_video_enc_caps_t caps = {};
    esp_video_codec_query_t query = {
        .codec_type = (esp_video_codec_type_t)dst_codec,
        .codec_cc = venc->codec_cc,
    };
    int ret = esp_video_enc_query_caps(&query, &caps);
    if (ret != ESP_VC_ERR_OK) {
        return false;
    }
    for (uint8_t i = 0; i < caps.in_fmt_num; i++) {
        if (src_codec == (uint32_t)caps.in_fmts[i]) {
            return true;
        }
    }
    return false;
}

static int venc_get_frame_size(venc_t *venc, int *in_frame_size, int *out_frame_size)
{
    esp_gmf_info_video_t *src_info = &venc->parent.src_info;
    esp_video_codec_resolution_t res = {
        .width = src_info->width,
        .height = src_info->height,
    };
    *in_frame_size = (int)esp_video_codec_get_image_size(
        (esp_video_codec_pixel_fmt_t)src_info->format_id, &res);
    if (venc->dst_codec == ESP_FOURCC_MJPG) {
        *out_frame_size = *in_frame_size / MJPEG_ENC_MIN_COMPRESS_RATIO;
    } else if (venc->dst_codec == ESP_FOURCC_H264) {
        uint8_t align = esp_gmf_oal_get_spiram_cache_align();
        *out_frame_size = *in_frame_size / H264_ENC_MIN_COMPRESS_RATIO;
        *out_frame_size = GMF_VIDEO_ALIGN_UP(*out_frame_size, align);
    } else {
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t venc_el_apply_settings(venc_t *venc, venc_extra_set_mask_t mask)
{
    venc_extra_set_t *extra_set = &venc->extra_set;
    if (venc->enc_handle == NULL ||
        extra_set->mask == VENC_EXTRA_SET_MASK_NONE) {
        return ESP_GMF_ERR_OK;
    }
    esp_vc_err_t ret = ESP_VC_ERR_OK;
    if (extra_set->mask & (mask & VENC_EXTRA_SET_MASK_BITRATE)) {
        ret |= esp_video_enc_set_bitrate(venc->enc_handle, extra_set->bitrate);
    }
    if (extra_set->mask & (mask & VENC_EXTRA_SET_MASK_QP)) {
        ret |= esp_video_enc_set_qp(venc->enc_handle, extra_set->min_qp, extra_set->max_qp);
    }
    if (extra_set->mask & (mask & VENC_EXTRA_SET_MASK_GOP)) {
        ret |= esp_video_enc_set_gop(venc->enc_handle, extra_set->gop);
    }
    return ret == ESP_VC_ERR_OK ? ESP_GMF_ERR_OK : ESP_GMF_ERR_NOT_SUPPORT;
}

static esp_gmf_job_err_t venc_el_open(esp_gmf_video_element_handle_t self, void *para)
{
    venc_t *venc = (venc_t *)self;
    esp_gmf_video_enc_cfg_t *cfg = (esp_gmf_video_enc_cfg_t *)OBJ_GET_CFG(self);
    esp_gmf_info_video_t *src_info = &venc->parent.src_info;
    venc->venc_bypass = (src_info->format_id == venc->dst_codec);
    if (venc->venc_bypass == false) {
        esp_video_enc_cfg_t enc_cfg = {
            .codec_type = venc->dst_codec,
            .resolution = {
                .width = src_info->width,
                .height = src_info->height,
            },
            .in_fmt = (esp_video_codec_pixel_fmt_t)src_info->format_id,
            .fps = src_info->fps,
        };
        if (cfg) {
            enc_cfg.codec_cc = cfg->codec_cc;
            venc->codec_cc = cfg->codec_cc;
        }
        esp_vc_err_t ret = esp_video_enc_open(&enc_cfg, &venc->enc_handle);
        if (ret != ESP_VC_ERR_OK) {
            ESP_LOGE(TAG, "Fail to open encoder ret %d", ret);
            return ESP_GMF_ERR_NOT_SUPPORT;
        }
        int in_frame_size = 0;
        int out_frame_size = 0;
        venc_get_frame_size(venc, &in_frame_size, &out_frame_size);
        ESP_GMF_ELEMENT_GET(venc)->in_attr.data_size = in_frame_size;
        ESP_GMF_ELEMENT_GET(venc)->out_attr.data_size = out_frame_size;
        venc_el_apply_settings(venc, VENC_EXTRA_SET_MASK_ALL);
    }
    // Report info to next element
    esp_gmf_info_video_t out_info = venc->parent.src_info;
    out_info.format_id = venc->dst_codec;
    esp_gmf_element_notify_vid_info(self, &out_info);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t venc_el_process(esp_gmf_video_element_handle_t self, void *para)
{
    esp_gmf_element_handle_t hd = (esp_gmf_element_handle_t)self;
    venc_t *venc = (venc_t *)self;
    esp_gmf_port_t *in = ESP_GMF_ELEMENT_GET(hd)->in;
    esp_gmf_port_t *out = ESP_GMF_ELEMENT_GET(hd)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;

    int ret = esp_gmf_port_acquire_in(in, &in_load, ESP_GMF_ELEMENT_GET(venc)->in_attr.data_size, ESP_GMF_MAX_DELAY);
    if (ret < 0) {
        ESP_LOGE(TAG, "Acquire size:%d on in port, ret:%d", ESP_GMF_ELEMENT_GET(venc)->in_attr.data_size, ret);
        return (ret == ESP_GMF_IO_ABORT) ? ESP_GMF_JOB_ERR_OK : ESP_GMF_JOB_ERR_FAIL;
    }
    if (in_load->is_done && in_load->valid_size == 0) {
        esp_gmf_port_release_in(in, in_load, 0);
        return ESP_GMF_JOB_ERR_DONE;
    }
    if (venc->venc_bypass) {
        out_load = in_load;
    }
    int out_frame_size = ESP_GMF_ELEMENT_GET(venc)->out_attr.data_size;
    ret = esp_gmf_port_acquire_out(out, &out_load, out_frame_size, -1);
    if (ret < 0) {
        esp_gmf_port_release_in(in, in_load, 0);
        ESP_LOGE(TAG, "Acquire size:%d on out port, ret:%d", out_frame_size, ret);
        return ret;
    }
    do {
        if (venc->venc_bypass) {
            break;
        }
        esp_video_enc_in_frame_t in_frame = {
            .pts = in_load->pts,
            .data = in_load->buf,
            .size = in_load->valid_size,
        };
        esp_video_enc_out_frame_t out_frame = {
            .data = out_load->buf,
            .size = out_frame_size,
        };
        ret = esp_video_enc_process(venc->enc_handle, &in_frame, &out_frame);
        ESP_LOGD(TAG, "I-b:%p,I-sz:%d, O-b:%p,O-sz:%d, in_sz:%d", in_load->buf, in_load->valid_size,
                 out_load->buf, out_load->valid_size, out_frame_size);
        // Handle buffer not enough case
        if (ret == ESP_VC_ERR_BUF_NOT_ENOUGH) {
            ret = 0;
            break;
        }
        if (ret != ESP_VC_ERR_OK) {
            ESP_LOGE(TAG, "Video encoder encode frame error, ret:%d", ret);
            ret = ESP_GMF_JOB_ERR_FAIL;
            break;
        }
        out_load->pts = in_load->pts;
        out_load->valid_size = out_frame.encoded_size;
        ret = out_load->valid_size;
    } while (0);
    esp_gmf_port_release_in(in, in_load, 0);
    esp_gmf_port_release_out(out, out_load, 0);
    if (in_load->is_done) {
        ret = ESP_GMF_JOB_ERR_DONE;
    }
    return ret;
}

static esp_gmf_job_err_t venc_el_close(esp_gmf_video_element_handle_t self, void *para)
{
    venc_t *venc = (venc_t *)self;
    if (venc->enc_handle) {
        esp_video_enc_close(venc->enc_handle);
        venc->enc_handle = NULL;
    }
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_err_t venc_el_destroy(esp_gmf_video_element_handle_t self)
{
    venc_t *venc = (venc_t *)self;
    if (venc->enc_handle) {
        esp_video_enc_close(venc->enc_handle);
        venc->enc_handle = NULL;
    }
    esp_gmf_video_el_deinit(self);
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_oal_free(self);
    return ESP_OK;
}

static esp_gmf_err_t venc_el_load_caps(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t cap = {0};
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    do {
        cap.cap_eightcc = ESP_GMF_CAPS_VIDEO_ENCODER;
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

static esp_gmf_err_t venc_el_new(void *cfg, esp_gmf_obj_handle_t *obj)
{
    return esp_gmf_video_enc_init((esp_gmf_video_enc_cfg_t *)cfg, (esp_gmf_element_handle_t*)obj);
}

static esp_gmf_err_t set_bitrate(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                 uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    venc_t *venc = (venc_t *)handle;
    int offset = 0;
    COPY_ARG(&venc->extra_set.bitrate, buf, offset, sizeof(uint32_t), buf_len);
    venc->extra_set.mask |= VENC_EXTRA_SET_MASK_BITRATE;
    return venc_el_apply_settings(venc, VENC_EXTRA_SET_MASK_BITRATE);
}

static esp_gmf_err_t get_in_formats(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                    uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    venc_t *venc = (venc_t *)handle;
    int offset = 0;
    uint32_t dst_codec = 0;
    const uint32_t **in_fmts = NULL;
    uint8_t *in_fmts_num = NULL;
    COPY_ARG(&dst_codec, buf, offset, sizeof(uint32_t), buf_len);
    COPY_ARG(&in_fmts, buf, offset, sizeof(void *), buf_len);
    COPY_ARG(&in_fmts_num, buf, offset, sizeof(void *), buf_len);
    return venc_get_input_codecs(venc, dst_codec, in_fmts, in_fmts_num);
}

static esp_gmf_err_t set_format(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    venc_t *venc = (venc_t *)handle;
    esp_gmf_info_video_t video_info = {};
    uint32_t dst_codec = 0;
    int offset = 0;
    COPY_ARG(&video_info.format_id, buf, offset, sizeof(uint32_t), buf_len);
    COPY_ARG(&video_info.width, buf, offset, sizeof(uint16_t), buf_len);
    COPY_ARG(&video_info.height, buf, offset, sizeof(uint16_t), buf_len);
    COPY_ARG(&video_info.fps, buf, offset, sizeof(uint8_t), buf_len);
    COPY_ARG(&video_info.bitrate, buf, offset, sizeof(uint32_t), buf_len);
    COPY_ARG(&dst_codec, buf, offset, sizeof(uint32_t), buf_len);
    uint32_t in_fmt = video_info.format_id;
    if (venc_is_codec_supported(venc, in_fmt, dst_codec) == false) {
        ESP_LOGE(TAG, "Not support encode from %s to %s",
                 esp_gmf_video_get_format_string(video_info.format_id),
                 esp_gmf_video_get_format_string(dst_codec));
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    venc->parent.src_info = video_info;
    venc->dst_codec = (esp_video_codec_type_t)(dst_codec);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t set_dst_codec(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                   uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    venc_t *venc = (venc_t *)handle;
    venc->dst_codec = *(uint32_t *) buf;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t get_frame_size(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                    uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    venc_t *venc = (venc_t *)handle;
    uint32_t *frame_size = NULL;
    int offset = 0;
    COPY_ARG(&frame_size, buf, offset, sizeof(void *), buf_len);
    if (frame_size == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    *frame_size = 0;
    if (venc->parent.src_info.format_id == 0 || venc->dst_codec == ESP_VIDEO_CODEC_TYPE_NONE) {
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    if (venc->venc_bypass) {
        return ESP_GMF_ERR_OK;
    }
    int in_frame_size = 0, out_frame_size = 0;
    venc_get_frame_size(venc, &in_frame_size, &out_frame_size);
    *frame_size = out_frame_size;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t venc_el_load_methods(esp_gmf_element_handle_t handle)
{
    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_method_t *methods = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    int offset = 0;
    do {
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(ENCODER, SET_BITRATE, BITRATE), ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), 0);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_method_append(&methods, VMETHOD(ENCODER, SET_BITRATE), set_bitrate, set_args);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        set_args = NULL;
        offset = 0;
        ADD_ARG_DESC(VMETHOD_ARG(ENCODER, SET_DST_CODEC, CODEC),  ESP_GMF_ARGS_TYPE_UINT32, sizeof(void *), offset);
        ret = esp_gmf_method_append(&methods, VMETHOD(ENCODER, SET_DST_CODEC), set_dst_codec, set_args);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        set_args = NULL;
        offset = 0;
        ADD_ARG_DESC(VMETHOD_ARG(ENCODER, GET_SRC_FMTS, DST_CODEC),  ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), offset);
        ADD_ARG_DESC(VMETHOD_ARG(ENCODER, GET_SRC_FMTS, SRC_FMTS_PTR),  ESP_GMF_ARGS_TYPE_UINT32, sizeof(void *), offset);
        ADD_ARG_DESC(VMETHOD_ARG(ENCODER, GET_SRC_FMTS, SRC_FMTS_NUM_PTR),  ESP_GMF_ARGS_TYPE_UINT32, sizeof(void *), offset);
        ret = esp_gmf_method_append(&methods, VMETHOD(ENCODER, GET_SRC_FMTS), get_in_formats, set_args);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        set_args = NULL;
        offset = 0;
        ADD_ARG_DESC(VMETHOD_ARG(ENCODER, PRESET, SRC_FMT),  ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), offset);
        ADD_ARG_DESC(VMETHOD_ARG(ENCODER, PRESET, SRC_WIDTH), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), offset);
        ADD_ARG_DESC(VMETHOD_ARG(ENCODER, PRESET, SRC_HEIGHT), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), offset);
        ADD_ARG_DESC(VMETHOD_ARG(ENCODER, PRESET, SRC_FPS), ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), offset);
        ADD_ARG_DESC(VMETHOD_ARG(ENCODER, PRESET, SRC_BITRATE), ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), offset);
        ADD_ARG_DESC(VMETHOD_ARG(ENCODER, PRESET, DST_CODEC), ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), offset);
        ret = esp_gmf_method_append(&methods, VMETHOD(ENCODER, PRESET), set_format, set_args);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        set_args = NULL;
        offset = 0;
        ADD_ARG_DESC(VMETHOD_ARG(ENCODER, GET_FRAME_SIZE, DST_FRM_SIZE_PTR),  ESP_GMF_ARGS_TYPE_UINT32, sizeof(void *), offset);
        ret = esp_gmf_method_append(&methods, VMETHOD(ENCODER, GET_FRAME_SIZE), get_frame_size, set_args);
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

esp_gmf_err_t esp_gmf_video_enc_get_out_size(esp_gmf_element_handle_t self, uint32_t *frame_size)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, frame_size, return ESP_GMF_ERR_INVALID_ARG);
    venc_t *venc = (venc_t *)self;
    *frame_size = 0;
    if (venc->parent.src_info.format_id == 0 || venc->dst_codec == ESP_VIDEO_CODEC_TYPE_NONE) {
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    if (venc->venc_bypass) {
        return ESP_GMF_ERR_OK;
    }
    int in_frame_size = 0, out_frame_size = 0;
    venc_get_frame_size(venc, &in_frame_size, &out_frame_size);
    *frame_size = out_frame_size;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_video_enc_set_bitrate(esp_gmf_element_handle_t self, uint32_t bitrate)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    venc_t *venc = (venc_t *)self;
    venc->extra_set.bitrate = bitrate;
    venc->extra_set.mask |= VENC_EXTRA_SET_MASK_BITRATE;
    return venc_el_apply_settings(venc, VENC_EXTRA_SET_MASK_BITRATE);
}

esp_gmf_err_t esp_gmf_video_enc_set_gop(esp_gmf_element_handle_t self, uint32_t gop)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    venc_t *venc = (venc_t *)self;
    venc->extra_set.gop = gop;
    venc->extra_set.mask |= VENC_EXTRA_SET_MASK_GOP;
    return venc_el_apply_settings(venc, VENC_EXTRA_SET_MASK_GOP);
}

esp_gmf_err_t esp_gmf_video_enc_set_qp(esp_gmf_element_handle_t self, uint32_t min_qp, uint32_t max_qp)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    venc_t *venc = (venc_t *)self;
    venc->extra_set.min_qp = min_qp;
    venc->extra_set.max_qp = max_qp;
    venc->extra_set.mask |= VENC_EXTRA_SET_MASK_QP;
    venc_el_apply_settings(venc, VENC_EXTRA_SET_MASK_QP);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_video_enc_set_dst_codec(esp_gmf_element_handle_t handle, uint32_t dst_codec)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    venc_t *venc = (venc_t *)handle;
    if (venc->enc_handle) {
        ESP_LOGE(TAG, "Not support set encoder handle during run");
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    venc->dst_codec = (esp_video_codec_type_t)dst_codec;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_video_enc_preset(esp_gmf_element_handle_t self, esp_gmf_info_video_t *video_info, uint32_t dst_codec)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    venc_t *venc = (venc_t *)self;
    if (venc_is_codec_supported(venc, video_info->format_id, dst_codec) == false) {
        ESP_LOGE(TAG, "Not support encode from %s to %s",
                 esp_gmf_video_get_format_string(video_info->format_id), esp_gmf_video_get_format_string(dst_codec));
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    venc->parent.src_info = *video_info;
    venc->dst_codec = (esp_video_codec_type_t)(dst_codec);
    return 0;
}

esp_gmf_err_t esp_gmf_video_enc_get_src_formats(esp_gmf_element_handle_t self,
                                                uint32_t dst_codec,
                                                const uint32_t **input_codecs,
                                                uint8_t *input_codec_num)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    venc_t *venc = (venc_t *)self;
    return venc_get_input_codecs(venc, dst_codec, input_codecs, input_codec_num);
}

esp_gmf_err_t esp_gmf_video_enc_init(esp_gmf_video_enc_cfg_t *cfg, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_MEM_CHECK(TAG, handle, return ESP_ERR_INVALID_ARG);
    venc_t *venc = esp_gmf_oal_calloc(1, sizeof(venc_t));
    ESP_GMF_MEM_CHECK(TAG, venc, return ESP_ERR_NO_MEM);
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)venc;
    obj->new_obj = venc_el_new;
    obj->del_obj = venc_el_destroy;

    int ret = esp_gmf_obj_set_tag(obj, "vid_enc");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto VENC_FAIL, "Failed set OBJ tag");

    uint8_t align = esp_gmf_oal_get_spiram_cache_align();
    esp_gmf_element_cfg_t el_cfg = {
        .dependency = true,
    };
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, align, align,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, align, align,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ret = esp_gmf_video_el_init(venc, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto VENC_FAIL, "Failed init video encoder el");
    if (cfg) {
        esp_gmf_video_enc_cfg_t *enc_cfg = (esp_gmf_video_enc_cfg_t *) calloc(1, sizeof(esp_gmf_video_enc_cfg_t));
        ESP_GMF_MEM_CHECK(TAG, enc_cfg, { goto VENC_FAIL; });
        memcpy(enc_cfg, cfg, sizeof(esp_gmf_video_enc_cfg_t));
        esp_gmf_obj_set_config(obj, enc_cfg, sizeof(esp_gmf_video_enc_cfg_t));
    }
    ESP_GMF_ELEMENT_GET(venc)->ops.open = venc_el_open;
    ESP_GMF_ELEMENT_GET(venc)->ops.process = venc_el_process;
    ESP_GMF_ELEMENT_GET(venc)->ops.close = venc_el_close;
    ESP_GMF_ELEMENT_GET(venc)->ops.event_receiver = esp_gmf_video_handle_events;
    ESP_GMF_ELEMENT_GET(venc)->ops.load_caps = venc_el_load_caps;
    ESP_GMF_ELEMENT_GET(venc)->ops.load_methods = venc_el_load_methods;

    ESP_LOGI(TAG, "Create %s-%p", OBJ_GET_TAG(obj), obj);
    *handle = (esp_gmf_element_handle_t) obj;
    return ESP_OK;

VENC_FAIL:
    esp_gmf_obj_delete(obj);
    return ret;
}

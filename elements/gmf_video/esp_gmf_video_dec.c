/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_gmf_video_dec.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_video_element.h"
#include "esp_video_dec.h"
#include "esp_video_codec_utils.h"
#include "esp_gmf_video_element.h"
#include "gmf_video_common.h"
#include "esp_gmf_video_methods_def.h"
#include "esp_log.h"

#define TAG "VDEC_EL"

#define COPY_ARG(dst, src, offset, size, total_len) \
    if (offset + size <= total_len) {               \
        memcpy(&(dst), src + offset, size);         \
        offset += size;                             \
    }

#define ADD_ARG_DESC(desc, type, size, offset)                           \
    ret = esp_gmf_args_desc_append(&set_args, desc, type, size, offset); \
    GMF_VIDEO_BREAK_ON_FAIL(ret);                                        \
    offset += size;

/**
 * @brief  Video decoder definition
 */
typedef struct {
    esp_gmf_video_element_t  parent;         /*!< Video element parent */
    uint32_t                 out_format;      /*!< Video decoder output codec */
    uint32_t                 codec_cc;       /*!< Codec FourCC used to find decoder if user set it */
    bool                     vdec_bypass;    /*!< Whether decoder is bypassed or not */
    bool                     header_parsed;  /*!< Whether video header parsed or not */
    esp_video_dec_handle_t   dec_handle;     /*!< Video decoder handle */
} vdec_t;

static inline uint32_t get_prefer_codec(vdec_t *vdec)
{
    esp_gmf_video_dec_cfg_t *cfg = (esp_gmf_video_dec_cfg_t *)OBJ_GET_CFG(vdec);
    return cfg ? cfg->codec_cc : 0;
}

static int vdec_get_out_fmts(vdec_t *vdec, uint32_t src_codec, const uint32_t **fmts, uint8_t *num)
{
    esp_video_dec_caps_t caps = {};
    esp_video_codec_query_t query = {
        .codec_type = (esp_video_codec_type_t)src_codec,
        .codec_cc = get_prefer_codec(vdec),
    };
    esp_vc_err_t ret = esp_video_dec_query_caps(&query, &caps);
    if (ret != ESP_VC_ERR_OK) {
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    *fmts = (const uint32_t *)caps.out_fmts;
    *num = caps.out_fmt_num;
    return ESP_GMF_ERR_OK;
}

static bool vdec_is_codec_supported(vdec_t *vdec, uint32_t in_codec, uint32_t out_format)
{
    esp_video_dec_caps_t caps = {};
    esp_video_codec_query_t query = {
        .codec_type = (esp_video_codec_type_t)in_codec,
        .codec_cc = get_prefer_codec(vdec),
    };
    esp_vc_err_t ret = esp_video_dec_query_caps(&query, &caps);
    if (ret != ESP_VC_ERR_OK) {
        return false;
    }
    for (uint8_t i = 0; i < caps.out_fmt_num; i++) {
        if (out_format == (uint32_t)caps.out_fmts[i]) {
            return true;
        }
    }
    return false;
}

static esp_gmf_job_err_t vdec_el_open(esp_gmf_video_element_handle_t self, void *para)
{
    vdec_t *vdec = (vdec_t *)self;
    esp_gmf_info_video_t *src_info = &vdec->parent.src_info;
    vdec->vdec_bypass = (src_info->format_id == vdec->out_format);
    if (vdec->vdec_bypass) {
        // Report video info to next element directly
        esp_gmf_element_notify_vid_info(self, src_info);
        return ESP_GMF_JOB_ERR_OK;
    }
    // Open video decoder now
    esp_video_dec_cfg_t dec_cfg = {
        .codec_type = (esp_video_codec_type_t)src_info->format_id,
        .codec_cc = get_prefer_codec(vdec),
        .out_fmt = (esp_video_codec_pixel_fmt_t)vdec->out_format,
    };
    bool supported = vdec_is_codec_supported(vdec, src_info->format_id, vdec->out_format);
    if (supported == false) {
        ESP_LOGE(TAG, "Format not supported in:%s out:%s",
                 esp_gmf_video_get_format_string(src_info->format_id),
                 esp_gmf_video_get_format_string(vdec->out_format));
        return ESP_GMF_JOB_ERR_FAIL;
    }
    int ret = esp_video_dec_open(&dec_cfg, &vdec->dec_handle);
    if (ret != ESP_VC_ERR_OK) {
        return ESP_GMF_JOB_ERR_FAIL;
    }
    // Get alignment request
    uint8_t in_frame_align = 0;
    uint8_t out_frame_align = 0;
    esp_video_dec_get_frame_align(vdec->dec_handle, &in_frame_align, &out_frame_align);
    ESP_GMF_ELEMENT_GET(vdec)->in_attr.port.buf_addr_aligned = in_frame_align;
    ESP_GMF_ELEMENT_GET(vdec)->out_attr.port.buf_addr_aligned = out_frame_align;
    return ESP_GMF_JOB_ERR_OK;
}

static int vdec_bypass(vdec_t *vdec, esp_gmf_port_t *in, esp_gmf_port_t *out)
{
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    esp_gmf_err_io_t ret = esp_gmf_port_acquire_in(in, &in_load, ESP_GMF_ELEMENT_GET(vdec)->in_attr.data_size, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_IN_CHECK(TAG, ret, ret, return ret);
    bool is_done = in_load->is_done;
    out_load = in_load;
    ret = esp_gmf_port_acquire_out(out, &out_load, out_load->valid_size, -1);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, ret, ret, esp_gmf_port_release_in(in, in_load, 0); return ret);
    esp_gmf_port_release_out(out, out_load, 0);
    esp_gmf_port_release_in(in, in_load, 0);
    if (is_done) {
        ret = ESP_GMF_JOB_ERR_DONE;
    }
    return ret;
}

static esp_gmf_job_err_t vdec_el_process(esp_gmf_video_element_handle_t self, void *para)
{
    esp_gmf_element_handle_t hd = (esp_gmf_element_handle_t)self;
    vdec_t *vdec = (vdec_t *)self;
    esp_gmf_port_t *in = ESP_GMF_ELEMENT_GET(hd)->in;
    esp_gmf_port_t *out = ESP_GMF_ELEMENT_GET(hd)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    if (vdec->vdec_bypass) {
        // This code is added to bypass raw input data only
        return vdec_bypass(vdec, in, out);
    }
    // TODO make sure input frame alignment meet request
    int ret = esp_gmf_port_acquire_in(in, &in_load, ESP_GMF_ELEMENT_GET(vdec)->in_attr.data_size, -1);
    ESP_GMF_PORT_ACQUIRE_IN_CHECK(TAG, ret, ret, goto __vid_proc_release;);

    // Suppose input data must be frame boundary
    esp_video_dec_in_frame_t in_frame = {
        .pts = in_load->pts,
        .data = in_load->buf,
        .size = in_load->valid_size,
    };
    esp_video_dec_out_frame_t decoded_frame = {};
    ret = ESP_GMF_JOB_ERR_FAIL;
    do {
        // Check whether done
        if (in_load->valid_size == 0) {
            ret = esp_gmf_port_acquire_out(out, &out_load, ESP_GMF_ELEMENT_GET(vdec)->out_attr.data_size, ESP_GMF_MAX_DELAY);
            ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, ret, ret, break;);
            out_load->is_done = in_load->is_done;
            out_load->valid_size = 0;
            out_load->pts = in_load->pts;
            break;
        }
        // Check alignment
        if ((intptr_t)in_load->buf & (ESP_GMF_ELEMENT_GET(vdec)->in_attr.port.buf_addr_aligned - 1)) {
            ESP_LOGE(TAG, "Input alignment not meet %d", ESP_GMF_ELEMENT_GET(vdec)->in_attr.port.buf_addr_aligned);
            ret = ESP_GMF_JOB_ERR_FAIL;
            break;
        }
        if (vdec->header_parsed == false) {
            // Workaround use small buffer, so that can get frame info
            uint32_t out_size = 32;
            uint8_t out_frame_align = ESP_GMF_ELEMENT_GET(vdec)->out_attr.port.buf_addr_aligned;
            uint8_t *out_data = esp_video_codec_align_alloc(out_frame_align, out_size, &out_size);
            if (out_data == NULL) {
                ESP_LOGE(TAG, "No enough memory for parse header");
                ret = ESP_GMF_JOB_ERR_FAIL;
                break;
            }
            decoded_frame.data = out_data;
            decoded_frame.size = out_size;
            ret = esp_video_dec_process(vdec->dec_handle, &in_frame, &decoded_frame);
            esp_video_codec_free(out_data);
            esp_video_codec_frame_info_t frame_info = {};
            ret = esp_video_dec_get_frame_info(vdec->dec_handle, &frame_info);
            if ((ret != ESP_VC_ERR_OK) || (frame_info.res.width == 0) || (frame_info.res.height) == 0) {
                ESP_LOGE(TAG, "Fail to get frame info%d", ret);
                ret = ESP_GMF_JOB_ERR_CONTINUE;
                break;
            }
            ESP_LOGI(TAG, "Dec frame size %dx%d", (int)frame_info.res.width, (int)frame_info.res.height);
            // Update output frame size
            uint32_t out_frame_size = esp_video_codec_get_image_size(vdec->out_format, &frame_info.res);
            out_frame_size = GMF_VIDEO_ALIGN_UP(out_frame_size, out_frame_align);
            ESP_GMF_ELEMENT_GET(vdec)->out_attr.data_size = out_frame_size;
            // Report info to next element when parse header finished
            esp_gmf_info_video_t out_info = {
                .format_id = vdec->out_format,
                .width = frame_info.res.width,
                .height = frame_info.res.height,
                .fps = frame_info.fps,
            };
            esp_gmf_element_notify_vid_info(self, &out_info);
            vdec->header_parsed = true;
        }
        // We do not allow decode change resolution in middle currently
        ret = esp_gmf_port_acquire_out(out, &out_load, ESP_GMF_ELEMENT_GET(vdec)->out_attr.data_size, ESP_GMF_MAX_DELAY);
        if (ret < 0) {
            ESP_LOGE(TAG, "Write data error, ret:%d, line:%d", ret, __LINE__);
            ret = (ret == ESP_GMF_IO_ABORT) ? ESP_GMF_JOB_ERR_OK : ESP_GMF_JOB_ERR_FAIL;
            break;
        }
        decoded_frame.data = out_load->buf;
        decoded_frame.size = ESP_GMF_ELEMENT_GET(vdec)->out_attr.data_size;
        ret = esp_video_dec_process(vdec->dec_handle, &in_frame, &decoded_frame);
        // Skip data if decode failed
        if (ret != ESP_VC_ERR_OK) {
            ESP_LOGE(TAG, "Fail to decode ret %d", ret);
            ret = ESP_GMF_JOB_ERR_CONTINUE;
            break;
        }
        out_load->valid_size = decoded_frame.decoded_size;
        out_load->pts = in_load->pts;
        ret = ESP_GMF_JOB_ERR_OK;
    } while (0);
 __vid_proc_release:
    if (out_load) {
        out_load->is_done = in_load->is_done;
        esp_gmf_port_release_out(out, out_load, 0);
    }
    if (in_load != NULL) {
        esp_gmf_port_release_in(in, in_load, 0);
    }
    if (in_load->is_done) {
        ret = ESP_GMF_JOB_ERR_DONE;
    }
    return ret;
}

static esp_gmf_job_err_t vdec_el_close(esp_gmf_video_element_handle_t self, void *para)
{
    vdec_t *vdec = (vdec_t *)self;
    if (vdec->dec_handle) {
        esp_video_dec_close(vdec->dec_handle);
        vdec->dec_handle = NULL;
    }
    vdec->header_parsed = false;
    ESP_LOGI(TAG, "Closed, %p", self);
    return ESP_OK;
}

static esp_gmf_err_t vdec_el_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_video_dec_init((esp_gmf_video_dec_cfg_t *)cfg, (esp_gmf_element_handle_t*)handle);
}

static esp_gmf_err_t vdec_el_destroy(esp_gmf_obj_handle_t self)
{
    esp_gmf_video_el_deinit(self);
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_oal_free(self);
    return ESP_OK;
}

static esp_gmf_err_t set_out_format(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                    uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    vdec_t *vdec = (vdec_t *)handle;
    vdec->out_format = *((uint32_t*) buf);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t set_src_codec(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                   uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    vdec_t *vdec = (vdec_t *)handle;
    vdec->parent.src_info.format_id = *((uint32_t*) buf);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t get_out_formats(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                     uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    vdec_t *vdec = (vdec_t *)handle;
    int offset = 0;
    uint32_t in_codec = 0;
    const uint32_t **out_fmts = NULL;
    uint8_t *out_fmt_num = NULL;
    COPY_ARG(in_codec, buf, offset, sizeof(uint32_t), buf_len);
    COPY_ARG(out_fmts, buf, offset, sizeof(void*), buf_len);
    COPY_ARG(out_fmt_num, buf, offset, sizeof(void*), buf_len);
    return vdec_get_out_fmts(vdec, in_codec, out_fmts, out_fmt_num);
}

static esp_gmf_err_t vdec_el_load_caps(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t cap = {0};
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    do {
        cap.cap_eightcc = ESP_GMF_CAPS_VIDEO_DECODER;
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

static esp_gmf_err_t vdec_load_methods(esp_gmf_element_handle_t handle)
{
    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_method_t *methods = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    int offset = 0;
    do {
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(CLR_CVT, SET_DST_FMT, FMT), ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), 0);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_method_append(&methods, VMETHOD(CLR_CVT, SET_DST_FMT), set_out_format, set_args);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        set_args = NULL;
        offset = 0;
        ADD_ARG_DESC(VMETHOD_ARG(DECODER, SET_SRC_CODEC, CODEC),  ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), offset);
        ret = esp_gmf_method_append(&methods, VMETHOD(DECODER, SET_SRC_CODEC), set_src_codec, set_args);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        set_args = NULL;
        offset = 0;
        ADD_ARG_DESC(VMETHOD_ARG(DECODER, GET_DST_FMTS, SRC_CODEC),  ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), offset);
        ADD_ARG_DESC(VMETHOD_ARG(DECODER, GET_DST_FMTS, DST_FMTS_PTR),  ESP_GMF_ARGS_TYPE_UINT32, sizeof(void *), offset);
        ADD_ARG_DESC(VMETHOD_ARG(DECODER, GET_DST_FMTS, DST_FMTS_NUM_PTR),  ESP_GMF_ARGS_TYPE_UINT32, sizeof(void *), offset);
        ret = esp_gmf_method_append(&methods, VMETHOD(DECODER, GET_DST_FMTS), get_out_formats, set_args);
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

esp_gmf_err_t esp_gmf_video_dec_init(esp_gmf_video_dec_cfg_t *cfg, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_MEM_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    vdec_t *vdec = esp_gmf_oal_calloc(1, sizeof(vdec_t));
    ESP_GMF_MEM_CHECK(TAG, vdec, return ESP_GMF_ERR_MEMORY_LACK);

    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)vdec;
    obj->new_obj = vdec_el_new;
    obj->del_obj = vdec_el_destroy;
    if (cfg) {
        esp_gmf_video_dec_cfg_t *dec_cfg = (esp_gmf_video_dec_cfg_t *) calloc(1, sizeof(esp_gmf_video_dec_cfg_t));
        ESP_GMF_MEM_CHECK(TAG, vdec, { goto VDEC_FAIL; });
        *dec_cfg = *cfg;
        esp_gmf_obj_set_config(obj, dec_cfg, sizeof(esp_gmf_video_dec_cfg_t));
    }

    esp_gmf_err_t ret = esp_gmf_obj_set_tag(obj, "vid_dec");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto VDEC_FAIL, "Failed set OBJ tag");

    uint8_t align = esp_gmf_oal_get_spiram_cache_align();
    esp_gmf_element_cfg_t el_cfg = {
        .dependency = true,
    };
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, align, align,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, align, align,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ret = esp_gmf_video_el_init(vdec, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto VDEC_FAIL, "Failed to init video decoder element");
    ESP_GMF_ELEMENT_GET(vdec)->ops.open = vdec_el_open;
    ESP_GMF_ELEMENT_GET(vdec)->ops.process = vdec_el_process;
    ESP_GMF_ELEMENT_GET(vdec)->ops.close = vdec_el_close;
    ESP_GMF_ELEMENT_GET(vdec)->ops.event_receiver = esp_gmf_video_handle_events;
    ESP_GMF_ELEMENT_GET(vdec)->ops.load_caps = vdec_el_load_caps;
    ESP_GMF_ELEMENT_GET(vdec)->ops.load_methods = vdec_load_methods;

    *handle = (esp_gmf_element_handle_t) obj;
    ESP_LOGD(TAG, "Create %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;

VDEC_FAIL:
    esp_gmf_obj_delete(obj);
    return ret;
}

esp_gmf_err_t esp_gmf_video_dec_set_dst_format(esp_gmf_element_handle_t handle, uint32_t dst_fmt)
{
    ESP_GMF_MEM_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    vdec_t *vdec = (vdec_t *)handle;
    vdec->out_format = dst_fmt;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_video_dec_get_dst_formats(esp_gmf_element_handle_t handle,
                                                uint32_t in_codec,
                                                const uint32_t **dst_fmts,
                                                uint8_t *dst_fmts_num)
{
    ESP_GMF_MEM_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    vdec_t *vdec = (vdec_t *)handle;
    return vdec_get_out_fmts(vdec, in_codec, dst_fmts, dst_fmts_num);
}

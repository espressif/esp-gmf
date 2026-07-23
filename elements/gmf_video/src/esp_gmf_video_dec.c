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
#include "gmf_video_common.h"
#include "esp_gmf_video_methods_def.h"
#include "esp_gmf_data_queue.h"
#include "esp_gmf_port.h"
#include "esp_log.h"
#include "esp_gmf_oal_mutex.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

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
 * @brief  Video decoder output buffer pixel alignment
 */
#define VIDEO_PIXELS_ALIGNMENT  (16)

#define ALIGN_UP(a, align)  (((a) + ((align) - 1)) & ~((align) - 1))

/**
 * @brief  Per-frame header stored in decode out pool slot
 */
typedef struct {
    uint64_t  pts;           /*!< Presentation timestamp */
    uint32_t  decoded_size;  /*!< Decoded pixel bytes */
    uint32_t  pixel_off;     /*!< Offset from slot start to aligned pixel buffer */
    uint8_t   is_done;       /*!< End of stream marker */
} vdec_pool_hdr_t;

/**
 * @brief  Video decoder definition
 */
typedef struct {
    esp_gmf_video_element_t  parent;           /*!< Video element parent */
    uint32_t                 out_format;       /*!< Video decoder output codec */
    uint32_t                 codec_cc;         /*!< Codec FourCC used to find decoder if user set it */
    bool                     vdec_bypass;      /*!< Whether decoder is bypassed or not */
    bool                     header_parsed;    /*!< Whether video header parsed or not */
    esp_video_dec_handle_t   dec_handle;       /*!< Video decoder handle */
    int                      out_pool_count;   /*!< Output pool frame count, 0 means disabled */
    esp_gmf_data_queue_t    *out_pool;         /*!< Decoded frame storage (FIFO return) */
    QueueHandle_t            frame_info_q;     /*!< Outstanding frame pointers for multi-acquire */
    esp_gmf_port_handle_t    pool_port;        /*!< Internal port for AA/RR emit */
    uint32_t                 pool_slot_size;   /*!< Bytes reserved per pool frame */
    uint32_t                 pool_frame_size;  /*!< Pixel buffer size in pool */
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

static void vdec_drain_frame_info_q(vdec_t *vdec)
{
    if (vdec->frame_info_q == NULL) {
        return;
    }
    vdec_pool_hdr_t *dummy = NULL;
    while (xQueueReceive(vdec->frame_info_q, &dummy, 0) == pdTRUE) {
    }
}

static void vdec_destroy_frame_info_q(vdec_t *vdec)
{
    if (vdec->frame_info_q) {
        vdec_drain_frame_info_q(vdec);
        vQueueDelete(vdec->frame_info_q);
        vdec->frame_info_q = NULL;
    }
}

static void vdec_destroy_out_pool_storage(vdec_t *vdec)
{
    if (vdec->out_pool) {
        esp_gmf_data_queue_wakeup(vdec->out_pool);
        esp_gmf_data_queue_destroy(vdec->out_pool);
        vdec->out_pool = NULL;
    }
    vdec->pool_slot_size = 0;
    vdec->pool_frame_size = 0;
}

static void vdec_destroy_out_pool(vdec_t *vdec)
{
    vdec_destroy_frame_info_q(vdec);
    vdec_destroy_out_pool_storage(vdec);
}

static esp_gmf_err_io_t vdec_pool_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    (void)wanted_size;
    vdec_t *vdec = (vdec_t *)handle;
    if (vdec->frame_info_q == NULL) {
        return ESP_GMF_IO_FAIL;
    }
    TickType_t ticks = (wait_ticks < 0 || (uint32_t)wait_ticks == ESP_GMF_MAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(wait_ticks);
    vdec_pool_hdr_t *hdr = NULL;
    if (xQueueReceive(vdec->frame_info_q, &hdr, ticks) != pdTRUE || hdr == NULL) {
        return (ticks == 0) ? ESP_GMF_IO_TIMEOUT : ESP_GMF_IO_FAIL;
    }
    load->buf = (uint8_t *)hdr + hdr->pixel_off;
    load->buf_length = vdec->pool_frame_size;
    load->valid_size = hdr->decoded_size;
    load->pts = hdr->pts;
    load->is_done = hdr->is_done;
    load->needs_free = 0;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t vdec_pool_release(void *handle, esp_gmf_payload_t *load, int wait_ticks)
{
    (void)load;
    (void)wait_ticks;
    vdec_t *vdec = (vdec_t *)handle;
    if (vdec->out_pool == NULL) {
        return ESP_GMF_IO_FAIL;
    }
    void *data = NULL;
    int size = 0;
    if (esp_gmf_data_queue_acquire_read(vdec->out_pool, &data, &size, 0) != 0 || data == NULL) {
        return ESP_GMF_IO_FAIL;
    }
    return esp_gmf_data_queue_release_read(vdec->out_pool) == 0 ? ESP_GMF_IO_OK : ESP_GMF_IO_FAIL;
}

static esp_gmf_err_t vdec_create_pool_port(vdec_t *vdec)
{
    if (vdec->pool_port) {
        return ESP_GMF_ERR_OK;
    }
    vdec->pool_port = NEW_ESP_GMF_PORT_IN_BLOCK(vdec_pool_acquire, vdec_pool_release, NULL, vdec, 0, ESP_GMF_MAX_DELAY);
    if (vdec->pool_port == NULL) {
        return ESP_GMF_ERR_MEMORY_LACK;
    }
    esp_gmf_port_set_reader(vdec->pool_port, vdec);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t vdec_create_out_pool(vdec_t *vdec, uint32_t frame_size, uint8_t out_align)
{
    if (vdec->out_pool) {
        return ESP_GMF_ERR_OK;
    }
    if (vdec->out_pool_count <= 0) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    if (out_align == 0) {
        out_align = 1;
    }
    /* Extra (out_align - 1) so pixel ptr can be aligned even if slot base is unaligned */
    uint32_t slot_size = sizeof(vdec_pool_hdr_t) + (out_align > 1 ? (out_align - 1) : 0) + frame_size;
    int q_size = (int)((slot_size + 64) * (uint32_t)vdec->out_pool_count);
    vdec->out_pool = esp_gmf_data_queue_create(q_size);
    if (vdec->out_pool == NULL) {
        return ESP_GMF_ERR_MEMORY_LACK;
    }
    vdec->pool_slot_size = slot_size;
    vdec->pool_frame_size = frame_size;
    ESP_LOGI(TAG, "Out pool storage created count:%d slot:%u frame:%u", vdec->out_pool_count,
             (unsigned)slot_size, (unsigned)frame_size);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t vdec_emit_from_pool(vdec_t *vdec, esp_gmf_port_t *out)
{
    esp_gmf_element_t *el = ESP_GMF_ELEMENT_GET(vdec);
    esp_gmf_port_t *bitstr_in = el->in;
    esp_gmf_payload_t *pool_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    esp_gmf_job_err_t ret = ESP_GMF_JOB_ERR_OK;

    /* Use pool_port as element in only for AA/RR so acquire_out attaches ref_port correctly */
    el->in = vdec->pool_port;
    int io_ret = esp_gmf_port_acquire_in(vdec->pool_port, &pool_load, vdec->pool_frame_size, ESP_GMF_MAX_DELAY);
    if (io_ret < 0 || pool_load == NULL) {
        el->in = bitstr_in;
        ESP_LOGE(TAG, "Acquire pool frame failed ret:%d", io_ret);
        return (io_ret == ESP_GMF_IO_ABORT) ? ESP_GMF_JOB_ERR_OK : ESP_GMF_JOB_ERR_FAIL;
    }
    out_load = pool_load;
    io_ret = esp_gmf_port_acquire_out(out, &out_load, out_load->valid_size ? out_load->valid_size : 1, ESP_GMF_MAX_DELAY);
    if (io_ret < 0) {
        esp_gmf_port_release_in(vdec->pool_port, pool_load, 0);
        el->in = bitstr_in;
        ESP_LOGE(TAG, "Acquire out for pool frame failed ret:%d", io_ret);
        return (io_ret == ESP_GMF_IO_ABORT) ? ESP_GMF_JOB_ERR_OK : ESP_GMF_JOB_ERR_FAIL;
    }
    bool is_done = pool_load->is_done;
    esp_gmf_port_release_out(out, out_load, 0);
    esp_gmf_port_release_in(vdec->pool_port, pool_load, 0);
    el->in = bitstr_in;
    if (is_done) {
        ret = ESP_GMF_JOB_ERR_DONE;
    }
    return ret;
}

static esp_gmf_job_err_t vdec_push_pool_frame(vdec_t *vdec, esp_video_dec_in_frame_t *in_frame, bool is_done, uint64_t pts)
{
    uint8_t out_align = ESP_GMF_ELEMENT_GET(vdec)->out_attr.port.buf_addr_aligned;
    if (out_align == 0) {
        out_align = 64;
    }
    void *slot = NULL;
    if (esp_gmf_data_queue_acquire_write(vdec->out_pool, &slot, (int)vdec->pool_slot_size,
                                         ESP_GMF_DATA_QUEUE_WAIT_FOREVER) != 0 || slot == NULL) {
        ESP_LOGE(TAG, "Acquire pool write failed");
        return ESP_GMF_JOB_ERR_FAIL;
    }
    vdec_pool_hdr_t *hdr = (vdec_pool_hdr_t *)slot;
    uint8_t *pixels = (uint8_t *)ALIGN_UP((uintptr_t)((uint8_t *)slot + sizeof(vdec_pool_hdr_t)), out_align);
    hdr->pixel_off = (uint32_t)(pixels - (uint8_t *)slot);
    hdr->pts = pts;
    hdr->is_done = is_done;
    hdr->decoded_size = 0;

    if (in_frame && in_frame->size) {
        esp_video_dec_out_frame_t decoded_frame = {
            .data = pixels,
            .size = vdec->pool_frame_size,
        };
        int vret = esp_video_dec_process(vdec->dec_handle, in_frame, &decoded_frame);
        if (vret != ESP_VC_ERR_OK) {
            ESP_LOGE(TAG, "Fail to decode ret %d", vret);
            // Allow decode to continue with next frame
            esp_gmf_data_queue_release_write(vdec->out_pool, 0);
            return ESP_GMF_JOB_ERR_CONTINUE;
        }
        hdr->decoded_size = decoded_frame.decoded_size;
    }
    if (esp_gmf_data_queue_release_write(vdec->out_pool, (int)vdec->pool_slot_size) != 0) {
        ESP_LOGE(TAG, "Release pool write failed");
        return ESP_GMF_JOB_ERR_FAIL;
    }
    /* Publish pointer so acquire can take another frame without releasing previous */
    if (xQueueSend(vdec->frame_info_q, &hdr, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Push frame info q failed");
        return ESP_GMF_JOB_ERR_FAIL;
    }
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t vdec_el_open(esp_gmf_video_element_handle_t self, void *para)
{
    vdec_t *vdec = (vdec_t *)self;
    esp_gmf_job_err_t ret = ESP_GMF_JOB_ERR_OK;
    esp_gmf_oal_mutex_lock(((esp_gmf_video_element_t *)self)->lock);
    esp_gmf_info_video_t *src_info = &vdec->parent.src_info;
    vdec->vdec_bypass = (src_info->format_id == vdec->out_format);
    vdec_drain_frame_info_q(vdec);
    if (vdec->vdec_bypass) {
        goto __vdec_open_exit;
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
        ret = ESP_GMF_JOB_ERR_FAIL;
        goto __vdec_open_exit;
    }
    int vret = esp_video_dec_open(&dec_cfg, &vdec->dec_handle);
    if (vret != ESP_VC_ERR_OK) {
        ret = ESP_GMF_JOB_ERR_FAIL;
        goto __vdec_open_exit;
    }
    // Get alignment request
    uint8_t in_frame_align = 0;
    uint8_t out_frame_align = 0;
    esp_video_dec_get_frame_align(vdec->dec_handle, &in_frame_align, &out_frame_align);
    ESP_GMF_ELEMENT_GET(vdec)->in_attr.port.buf_addr_aligned = in_frame_align;
    ESP_GMF_ELEMENT_GET(vdec)->out_attr.port.buf_addr_aligned = out_frame_align;
__vdec_open_exit:
    esp_gmf_oal_mutex_unlock(((esp_gmf_video_element_t *)self)->lock);
    if (vdec->vdec_bypass) {
        // Report video info to next element directly
        esp_gmf_element_notify_vid_info(self, src_info);
    }
    return ret;
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

static esp_gmf_job_err_t vdec_parse_header(vdec_t *vdec, esp_gmf_video_element_handle_t self,
                                           esp_video_dec_in_frame_t *in_frame)
{
    /* Tiny probe buffer: jpeg decoder may log that size < decoded frame; that is expected for header parse. */
    uint32_t out_size = 32;
    uint8_t out_frame_align = ESP_GMF_ELEMENT_GET(vdec)->out_attr.port.buf_addr_aligned;
    uint8_t *out_data = esp_video_codec_align_alloc(out_frame_align, out_size, &out_size);
    if (out_data == NULL) {
        ESP_LOGE(TAG, "No enough memory for parse header");
        return ESP_GMF_JOB_ERR_FAIL;
    }
    esp_video_dec_out_frame_t decoded_frame = {
        .data = out_data,
        .size = out_size,
    };
    (void)esp_video_dec_process(vdec->dec_handle, in_frame, &decoded_frame);
    esp_video_codec_free(out_data);
    esp_video_codec_frame_info_t frame_info = {};
    int ret = esp_video_dec_get_frame_info(vdec->dec_handle, &frame_info);
    if ((ret != ESP_VC_ERR_OK) || (frame_info.res.width == 0) || (frame_info.res.height) == 0) {
        ESP_LOGE(TAG, "Fail to get frame info%d", ret);
        return ESP_GMF_JOB_ERR_CONTINUE;
    }
    esp_video_codec_resolution_t res = {
        .width = ALIGN_UP(frame_info.res.width, VIDEO_PIXELS_ALIGNMENT),
        .height = ALIGN_UP(frame_info.res.height, VIDEO_PIXELS_ALIGNMENT),
    };
    ESP_LOGI(TAG, "Dec frame size %dx%d", (int)frame_info.res.width, (int)frame_info.res.height);
    uint32_t out_frame_size = esp_video_codec_get_image_size(vdec->out_format, &res);
    out_frame_size = GMF_VIDEO_ALIGN_UP(out_frame_size, out_frame_align);
    ESP_GMF_ELEMENT_GET(vdec)->out_attr.data_size = out_frame_size;
    esp_gmf_info_video_t out_info = {
        .format_id = vdec->out_format,
        .width = frame_info.res.width,
        .height = frame_info.res.height,
        .fps = frame_info.fps,
    };
    if (frame_info.fps == 0) {
        out_info.fps = vdec->parent.src_info.fps;
    }
    esp_gmf_element_notify_vid_info(self, &out_info);
    vdec->header_parsed = true;
    if (vdec->out_pool_count > 0) {
        esp_gmf_err_t pret = vdec_create_out_pool(vdec, out_frame_size, out_frame_align);
        if (pret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Fail to create out pool");
            return ESP_GMF_JOB_ERR_FAIL;
        }
    }
    return ESP_GMF_JOB_ERR_OK;
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
        return vdec_bypass(vdec, in, out);
    }
    int ret = esp_gmf_port_acquire_in(in, &in_load, ESP_GMF_ELEMENT_GET(vdec)->in_attr.data_size, -1);
    ESP_GMF_PORT_ACQUIRE_IN_CHECK(TAG, ret, ret, goto __vid_proc_release;);

    esp_video_dec_in_frame_t in_frame = {
        .pts = in_load->pts,
        .data = in_load->buf,
        .size = in_load->valid_size,
    };
    esp_video_dec_out_frame_t decoded_frame = {};
    ret = ESP_GMF_JOB_ERR_FAIL;
    do {
        if (in_load->valid_size == 0) {
            if (vdec->out_pool_count > 0 && vdec->out_pool) {
                ret = vdec_push_pool_frame(vdec, NULL, in_load->is_done, in_load->pts);
                if (ret == ESP_GMF_JOB_ERR_OK) {
                    esp_gmf_port_release_in(in, in_load, 0);
                    in_load = NULL;
                    ret = vdec_emit_from_pool(vdec, out);
                }
                break;
            }
            ret = esp_gmf_port_acquire_out(out, &out_load, ESP_GMF_ELEMENT_GET(vdec)->out_attr.data_size, ESP_GMF_MAX_DELAY);
            ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, ret, ret, break;);
            out_load->is_done = in_load->is_done;
            out_load->valid_size = 0;
            out_load->pts = in_load->pts;
            break;
        }
        if ((intptr_t)in_load->buf & (ESP_GMF_ELEMENT_GET(vdec)->in_attr.port.buf_addr_aligned - 1)) {
            ESP_LOGE(TAG, "Input alignment not meet %d", ESP_GMF_ELEMENT_GET(vdec)->in_attr.port.buf_addr_aligned);
            ret = ESP_GMF_JOB_ERR_FAIL;
            break;
        }
        if (vdec->header_parsed == false) {
            ret = vdec_parse_header(vdec, self, &in_frame);
            if (ret != ESP_GMF_JOB_ERR_OK) {
                break;
            }
        }
        if (vdec->out_pool_count > 0) {
            ret = vdec_push_pool_frame(vdec, &in_frame, in_load->is_done, in_load->pts);
            if (ret != ESP_GMF_JOB_ERR_OK) {
                break;
            }
            esp_gmf_port_release_in(in, in_load, 0);
            in_load = NULL;
            ret = vdec_emit_from_pool(vdec, out);
            break;
        }
        ret = esp_gmf_port_acquire_out(out, &out_load, ESP_GMF_ELEMENT_GET(vdec)->out_attr.data_size, ESP_GMF_MAX_DELAY);
        if (ret < 0) {
            ESP_LOGE(TAG, "Write data error, ret:%d, line:%d", ret, __LINE__);
            ret = (ret == ESP_GMF_IO_ABORT) ? ESP_GMF_JOB_ERR_OK : ESP_GMF_JOB_ERR_FAIL;
            break;
        }
        decoded_frame.data = out_load->buf;
        decoded_frame.size = ESP_GMF_ELEMENT_GET(vdec)->out_attr.data_size;
        ret = esp_video_dec_process(vdec->dec_handle, &in_frame, &decoded_frame);
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
        if (in_load) {
            out_load->is_done = in_load->is_done;
        }
        esp_gmf_port_release_out(out, out_load, 0);
    }
    if (in_load != NULL) {
        bool is_done = in_load->is_done;
        esp_gmf_port_release_in(in, in_load, 0);
        if (is_done && ret != ESP_GMF_JOB_ERR_DONE) {
            ret = ESP_GMF_JOB_ERR_DONE;
        }
    }
    return ret;
}

static esp_gmf_job_err_t vdec_el_close(esp_gmf_video_element_handle_t self, void *para)
{
    vdec_t *vdec = (vdec_t *)self;
    /* Wake blocked acquire_write if pool full; info q is only used inside process() */
    vdec_destroy_out_pool_storage(vdec);
    vdec_drain_frame_info_q(vdec);
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
    vdec_t *vdec = (vdec_t *)self;
    vdec_destroy_out_pool(vdec);
    if (vdec->pool_port) {
        esp_gmf_port_deinit(vdec->pool_port);
        vdec->pool_port = NULL;
    }
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

static esp_gmf_err_t set_out_pool(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                  uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    int frame_count = *((int *)buf);
    return esp_gmf_video_dec_set_out_pool(handle, frame_count);
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

        set_args = NULL;
        offset = 0;
        ADD_ARG_DESC(VMETHOD_ARG(DECODER, SET_OUT_POOL, FRAME_COUNT), ESP_GMF_ARGS_TYPE_INT32, sizeof(int), offset);
        ret = esp_gmf_method_append(&methods, VMETHOD(DECODER, SET_OUT_POOL), set_out_pool, set_args);
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
    esp_gmf_oal_mutex_lock(((esp_gmf_video_element_t *)handle)->lock);
    vdec->out_format = dst_fmt;
    esp_gmf_oal_mutex_unlock(((esp_gmf_video_element_t *)handle)->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_video_dec_get_dst_format(esp_gmf_element_handle_t handle, uint32_t *dst_fmt)
{
    ESP_GMF_MEM_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_MEM_CHECK(TAG, dst_fmt, return ESP_GMF_ERR_INVALID_ARG);
    vdec_t *vdec = (vdec_t *)handle;
    esp_gmf_oal_mutex_lock(((esp_gmf_video_element_t *)handle)->lock);
    *dst_fmt = vdec->out_format;
    esp_gmf_oal_mutex_unlock(((esp_gmf_video_element_t *)handle)->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_video_dec_get_dst_formats(esp_gmf_element_handle_t handle,
                                                uint32_t in_codec,
                                                const uint32_t **dst_fmts,
                                                uint8_t *dst_fmts_num)
{
    ESP_GMF_MEM_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    vdec_t *vdec = (vdec_t *)handle;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(((esp_gmf_video_element_t *)handle)->lock);
    ret = vdec_get_out_fmts(vdec, in_codec, dst_fmts, dst_fmts_num);
    esp_gmf_oal_mutex_unlock(((esp_gmf_video_element_t *)handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_video_dec_set_out_pool(esp_gmf_element_handle_t handle, int frame_count)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    if (frame_count < 0) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    vdec_t *vdec = (vdec_t *)handle;
    esp_gmf_event_state_t state = ESP_GMF_EVENT_STATE_NONE;
    esp_gmf_element_get_state(handle, &state);
    if (state == ESP_GMF_EVENT_STATE_RUNNING || state == ESP_GMF_EVENT_STATE_PAUSED) {
        return ESP_GMF_ERR_INVALID_STATE;
    }
    esp_gmf_oal_mutex_lock(((esp_gmf_video_element_t *)handle)->lock);
    if (vdec->out_pool) {
        esp_gmf_oal_mutex_unlock(((esp_gmf_video_element_t *)handle)->lock);
        return ESP_GMF_ERR_INVALID_STATE;
    }
    vdec->out_pool_count = frame_count;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    if (frame_count > 0) {
        /* Create info q at set time so acquire can block from pipeline_run */
        vdec_destroy_frame_info_q(vdec);
        vdec->frame_info_q = xQueueCreate((UBaseType_t)frame_count, sizeof(vdec_pool_hdr_t *));
        if (vdec->frame_info_q == NULL) {
            ret = ESP_GMF_ERR_MEMORY_LACK;
        } else {
            ret = vdec_create_pool_port(vdec);
            if (ret != ESP_GMF_ERR_OK) {
                vdec_destroy_frame_info_q(vdec);
            }
        }
    } else {
        vdec_destroy_frame_info_q(vdec);
        if (vdec->pool_port) {
            esp_gmf_port_deinit(vdec->pool_port);
            vdec->pool_port = NULL;
        }
    }
    esp_gmf_oal_mutex_unlock(((esp_gmf_video_element_t *)handle)->lock);
    return ret;
}

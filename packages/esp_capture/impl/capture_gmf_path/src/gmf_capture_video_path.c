/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_capture_path_mngr.h"
#include "capture_gmf_mngr.h"
#include "data_queue.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_video_enc.h"
#include "esp_gmf_video_overlay.h"
#include "esp_log.h"
#include "capture_pipeline_utils.h"
#include "capture_gmf_mngr.h"
#include "capture_os.h"
#include "esp_capture.h"
#include "esp_capture_sync.h"
#include "gmf_capture_path_mngr.h"
#include "capture_video_src_el.h"
#include "capture_share_copy_el.h"

#define TAG "GMF_CAPTURE_VPATH"

#define VIDEO_ENC_OUT_ALIGNMENT (128)
#define ALIGN_UP(num, align)    (((num) + ((align) - 1)) & ~((align) - 1))

typedef struct {
    gmf_capture_path_res_t     base;
    // TODO data queue create in video encoder elements?
    data_q_t                  *video_q;
    esp_capture_sync_handle_t  sync_handle;
    esp_gmf_element_handle_t   venc_el;
    esp_gmf_port_handle_t      sink_port;
    capture_sema_handle_t      raw_consume_sema;
    bool                       video_share_raw;
    esp_gmf_port_handle_t      overlay_port;
    esp_capture_overlay_if_t  *overlay;
    esp_gmf_element_handle_t   overlay_el;
    bool                       overlay_enable;
    bool                       run_once;
    uint32_t                   bitrate;
} video_path_res_t;

typedef struct {
    esp_capture_video_path_mngr_if_t  base;
    gmf_capture_path_mngr_t           mngr;
} gmf_video_path_t;

static esp_capture_err_t get_video_encoder(gmf_capture_path_mngr_t *mngr, uint8_t idx)
{
    video_path_res_t *res = (video_path_res_t *)gmf_capture_path_mngr_get_idx(mngr, idx);
    uint8_t path_mask = (1 << (res->base.path));
    for (int i = 0; i < mngr->pipeline_num; i++) {
        esp_capture_gmf_pipeline_t *pipeline = &mngr->pipeline[i];
        if ((pipeline->path_mask & path_mask) == 0) {
            continue;
        }
        if (capture_pipeline_is_sink(pipeline->pipeline)) {
            // TODO get by caps
            esp_gmf_element_handle_t venc_handle = capture_get_element_by_caps(pipeline->pipeline, ESP_GMF_CAPS_VIDEO_ENCODER);
            if (venc_handle) {
                res->venc_el = venc_handle;
                return ESP_CAPTURE_ERR_OK;
            }
        }
    }
    return ESP_CAPTURE_ERR_NOT_FOUND;
}

static esp_gmf_element_handle_t get_overlay_element(gmf_capture_path_mngr_t *mngr, uint8_t path)
{
    for (int i = 0; i < mngr->pipeline_num; i++) {
        esp_capture_gmf_pipeline_t *pipe = &mngr->pipeline[i];
        if ((pipe->path_mask & (1 << path)) == 0) {
            continue;
        }
        esp_gmf_element_handle_t overlay_el = NULL;
        esp_gmf_pipeline_get_el_by_name(pipe->pipeline, "vid_overlay", &overlay_el);
        if (overlay_el) {
            return overlay_el;
        }
    }
    return NULL;
}

static esp_capture_err_t set_video_source_sync_handle(gmf_capture_path_mngr_t *mngr, esp_capture_sync_handle_t sync_handle)
{
    for (int i = 0; i < mngr->pipeline_num; i++) {
        esp_capture_gmf_pipeline_t *pipeline = &mngr->pipeline[i];
        if (capture_pipeline_is_src(pipeline->pipeline, mngr->pipeline, mngr->pipeline_num)) {
            esp_gmf_element_handle_t vid_src = NULL;
            esp_gmf_pipeline_get_el_by_name(pipeline->pipeline, "vid_src", &vid_src);
            if (vid_src) {
                capture_video_src_el_set_sync_handle(vid_src, sync_handle);
            }
        }
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t gmf_video_path_set_run_once(gmf_capture_path_mngr_t *mngr, video_path_res_t *res)
{
    uint8_t path_mask = (1 << res->base.path);
    for (int i = 0; i < mngr->pipeline_num; i++) {
        esp_capture_gmf_pipeline_t *pipeline = &mngr->pipeline[i];
        if (capture_pipeline_is_src(pipeline->pipeline, mngr->pipeline, mngr->pipeline_num) && (pipeline->path_mask & path_mask) != 0) {
            if ((pipeline->path_mask & ~path_mask)) {
                // Also have other path
                esp_gmf_element_handle_t share_cp = NULL;
                esp_gmf_pipeline_get_el_by_name(pipeline->pipeline, "share_copier", &share_cp);
                if (share_cp) {
                    return capture_share_copy_el_set_single_fetch(share_cp, res->base.path, res->run_once);
                }
            } else {
                // Only one sink
                esp_gmf_element_handle_t vid_src = NULL;
                esp_gmf_pipeline_get_el_by_name(pipeline->pipeline, "vid_src", &vid_src);
                if (vid_src) {
                    return esp_gmf_video_src_set_single_fetch(vid_src, res->run_once);
                }
            }
        }
    }
    return ESP_CAPTURE_ERR_NOT_SUPPORTED;
}

static esp_capture_err_t video_path_apply_setting(gmf_capture_path_mngr_t *mngr, uint8_t idx)
{
    video_path_res_t *res = (video_path_res_t *)gmf_capture_path_mngr_get_idx(mngr, idx);
    if (res->overlay_port) {
        res->overlay_el = get_overlay_element(mngr, res->base.path);
        if (res->overlay_el == NULL) {
            ESP_LOGW(TAG, "No overlay element existed");
            return ESP_CAPTURE_ERR_NOT_SUPPORTED;
        }
        esp_gmf_video_overlay_set_overlay_port(res->overlay_el, res->overlay_port);
        if (res->overlay_enable) {
            esp_gmf_overlay_rgn_info_t overlay_rgn = {};
            res->overlay->get_overlay_region(res->overlay,
                                             (esp_capture_format_id_t *)&overlay_rgn.format_id,
                                             (esp_capture_rgn_t *)&overlay_rgn.dst_rgn);
            esp_gmf_video_overlay_set_rgn(res->overlay_el, &overlay_rgn);
        }
        esp_gmf_video_overlay_enable(res->overlay_el, res->overlay_enable);
    }
    if (res->venc_el && res->bitrate) {
        esp_gmf_video_enc_set_bitrate(res->venc_el, res->bitrate);
    }
    if (res->sync_handle) {
        set_video_source_sync_handle(mngr, res->sync_handle);
    }
    gmf_video_path_set_run_once(mngr, res);
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t video_path_prepare_all(gmf_capture_path_mngr_t *mngr)
{
    for (int i = 0; i < mngr->path_num; i++) {
        get_video_encoder(mngr, i);
        video_path_apply_setting(mngr, i);
    }
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t gmf_video_path_open(esp_capture_path_mngr_if_t *p, esp_capture_path_cfg_t *cfg)
{
    if (p == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    gmf_video_path_t *video_path = (gmf_video_path_t *)p;
    return gmf_capture_path_mngr_open(&video_path->mngr, ESP_CAPTURE_STREAM_TYPE_VIDEO, cfg, sizeof(video_path_res_t));
}

static esp_gmf_err_io_t video_sink_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    video_path_res_t *res = (video_path_res_t *)handle;
    // Detect encoder bypass case
    if (res->video_share_raw) {
        esp_capture_stream_frame_t vid_frame = {
            .stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO,
            .pts = (uint32_t)load->pts,
            .data = load->buf,
            .size = load->valid_size,
        };
        int ret = gmf_capture_path_mngr_frame_reached(&res->base, &vid_frame);
        return ret == ESP_CAPTURE_ERR_OK ? ESP_GMF_IO_OK : ESP_GMF_IO_FAIL;
    }
    data_q_t *q = res->video_q;
    int size = sizeof(esp_capture_stream_frame_t) + wanted_size + VIDEO_ENC_OUT_ALIGNMENT;
    esp_capture_stream_frame_t *vid_frame = (esp_capture_stream_frame_t *)data_q_get_buffer(q, size);
    if (vid_frame == NULL) {
        return ESP_GMF_IO_FAIL;
    }
    vid_frame->stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;
    vid_frame->data = ((void *)vid_frame) + sizeof(esp_capture_stream_frame_t);
    vid_frame->data = (uint8_t *)ALIGN_UP((uintptr_t)vid_frame->data, VIDEO_ENC_OUT_ALIGNMENT);
    load->buf = vid_frame->data;
    load->buf_length = size;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t video_sink_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    video_path_res_t *res = (video_path_res_t *)handle;
    if (res->video_share_raw) {
        if (res->base.parent->started == false || res->base.enable == false) {
            return ESP_GMF_IO_FAIL;
        }
        if (load->valid_size) {
            capture_sema_lock(res->raw_consume_sema, CAPTURE_MAX_LOCK_TIME);
        }
        return ESP_GMF_IO_OK;
    }
    data_q_t *q = res->video_q;
    void *data = data_q_get_write_data(q);
    if (data) {
        esp_capture_stream_frame_t *vid_frame = (esp_capture_stream_frame_t *)data;
        vid_frame->pts = (uint32_t)load->pts;
        vid_frame->size = load->valid_size;
        int ret = gmf_capture_path_mngr_frame_reached(&res->base, vid_frame);
        if (ret == ESP_CAPTURE_ERR_OK) {
            int size = (int)(intptr_t)(vid_frame->data - (uint8_t *)data) + vid_frame->size;
            data_q_send_buffer(q, size);
        } else {
            ESP_LOGI(TAG, "Drop for disable");
            data_q_send_buffer(q, 0);
        }
    }
    return ESP_GMF_IO_OK;
}

static esp_capture_err_t video_path_prepare(gmf_capture_path_res_t *mngr_res)
{
    video_path_res_t *res = (video_path_res_t *)mngr_res;
    uint32_t out_frame_size = 0;
    esp_gmf_video_enc_get_out_size(res->venc_el, &out_frame_size);
    if (out_frame_size == 0) {
        // Bypass mode
        res->video_share_raw = true;
    } else {
        res->video_q = data_q_init(out_frame_size * 3);
        if (res->video_q == NULL) {
            return ESP_CAPTURE_ERR_NO_MEM;
        }
    }
    if (res->sink_port == NULL) {
        res->sink_port = NEW_ESP_GMF_PORT_OUT_BLOCK(video_sink_acquire, video_sink_release, NULL, res, 0, ESP_GMF_MAX_DELAY);
        if (res->sink_port == NULL) {
            return ESP_CAPTURE_ERR_NO_MEM;
        }
        esp_gmf_element_register_out_port(res->venc_el, res->sink_port);
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t video_path_stop(gmf_capture_path_res_t *mngr_res)
{
    video_path_res_t *res = (video_path_res_t *)mngr_res;
    // Release to let user quit
    if (res->raw_consume_sema) {
        capture_sema_unlock(res->raw_consume_sema);
    }
    if (res->video_q) {
        data_q_consume_all(res->video_q);
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t video_path_release(gmf_capture_path_res_t *mngr_res)
{
    video_path_res_t *res = (video_path_res_t *)mngr_res;
    if (res->video_q) {
        data_q_deinit(res->video_q);
        res->video_q = NULL;
    }
    if (res->sink_port) {
        esp_gmf_element_unregister_out_port(res->venc_el, res->sink_port);
        res->sink_port = NULL;
    }
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t gmf_video_path_add_path(esp_capture_path_mngr_if_t *p, uint8_t path, esp_capture_stream_info_t *sink_cfg)
{
    if (p == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    gmf_video_path_t *video_path = (gmf_video_path_t *)p;
    int ret = gmf_capture_path_mngr_add_path(&video_path->mngr, path, sink_cfg);
    if (ret != ESP_CAPTURE_ERR_OK) {
        return ret;
    }
    video_path_res_t *res = (video_path_res_t *)gmf_capture_path_mngr_get_path(&video_path->mngr, path);
    if (res->raw_consume_sema == NULL) {
        capture_sema_create(&res->raw_consume_sema);
        if (res->raw_consume_sema == NULL) {
            return ESP_CAPTURE_ERR_NO_RESOURCES;
        }
    }
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t gmf_video_path_enable_path(esp_capture_path_mngr_if_t *p, uint8_t path, bool enable)
{
    gmf_video_path_t *video_path = (gmf_video_path_t *)p;
    return gmf_capture_path_mngr_enable_path(&video_path->mngr, path, enable,
                                             video_path_prepare,
                                             video_path_stop,
                                             video_path_release);
}

static esp_gmf_err_io_t overlay_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    esp_capture_overlay_if_t *overlay = (esp_capture_overlay_if_t *)handle;
    esp_capture_stream_frame_t frame = {};
    int ret = overlay->acquire_frame(overlay, &frame);
    if (ret == ESP_CAPTURE_ERR_OK) {
        // TODO workaround use PTS to store alpha?
        uint8_t alpha = 0;
        overlay->get_alpha(overlay, &alpha);
        load->pts = alpha;
        load->buf = frame.data;
        load->valid_size = frame.size;
    } else {
        ESP_LOGE(TAG, "Fail to acquire overlay ret %d", ret);
    }
    return ret >= 0 ? ESP_GMF_IO_OK : ESP_GMF_IO_FAIL;
}

static esp_gmf_err_io_t overlay_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    esp_capture_overlay_if_t *overlay = (esp_capture_overlay_if_t *)handle;
    esp_capture_stream_frame_t frame = {
        .data = load->buf,
        .size = load->valid_size,
    };
    overlay->release_frame(overlay, &frame);
    return ESP_GMF_IO_OK;
}

esp_capture_err_t gmf_video_path_add_overlay(esp_capture_video_path_mngr_if_t *p, uint8_t path, esp_capture_overlay_if_t *overlay)
{
    gmf_video_path_t *video_path = (gmf_video_path_t *)p;
    video_path_res_t *res = (video_path_res_t *)gmf_capture_path_mngr_get_path(&video_path->mngr, path);
    if (res == NULL) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    if (res->overlay) {
        ESP_LOGW(TAG, "Overlay already added");
        return ESP_CAPTURE_ERR_INVALID_STATE;
    }
    res->overlay = overlay;
    res->overlay_port = NEW_ESP_GMF_PORT_IN_BLOCK(overlay_acquire, overlay_release, NULL, overlay, 0, ESP_GMF_MAX_DELAY);
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t gmf_video_path_enable_overlay(esp_capture_video_path_mngr_if_t *p, uint8_t path, bool enable)
{
    gmf_video_path_t *video_path = (gmf_video_path_t *)p;
    video_path_res_t *res = (video_path_res_t *)gmf_capture_path_mngr_get_path(&video_path->mngr, path);
    if (res == NULL || res->overlay_port == NULL) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    res->overlay_enable = enable;
    if (res->overlay_el == NULL) {
        return ESP_CAPTURE_ERR_OK;
    }
    if (enable) {
        esp_gmf_overlay_rgn_info_t overlay_rgn = {};
        res->overlay->get_overlay_region(res->overlay,
                                         (esp_capture_format_id_t *)&overlay_rgn.format_id,
                                         (esp_capture_rgn_t *)&overlay_rgn.dst_rgn);
        esp_gmf_video_overlay_set_rgn(res->overlay_el, &overlay_rgn);
    }
    return esp_gmf_video_overlay_enable(res->overlay_el, enable);
}

esp_capture_err_t gmf_video_path_start(esp_capture_path_mngr_if_t *p)
{
    gmf_video_path_t *video_path = (gmf_video_path_t *)p;
    return gmf_capture_path_mngr_start(&video_path->mngr, video_path_prepare_all, video_path_prepare);
}

esp_capture_err_t gmf_video_path_set(esp_capture_path_mngr_if_t *p, uint8_t path,
                                     esp_capture_path_set_type_t type, void *cfg, int cfg_size)
{
    // Iterate pipelines to get
    gmf_video_path_t *video_path = (gmf_video_path_t *)p;
    if (p == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    int ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
    video_path_res_t *res = (video_path_res_t *)gmf_capture_path_mngr_get_path(&video_path->mngr, path);
    if (res == NULL && type != ESP_CAPTURE_PATH_SET_TYPE_REGISTER_ELEMENT) {
        return ret;
    }
    esp_capture_pipeline_builder_if_t *builder = video_path->mngr.pipeline_builder;
    if (type == ESP_CAPTURE_PATH_SET_TYPE_SYNC_HANDLE) {
        res->sync_handle = *(esp_capture_sync_handle_t *)cfg;
    } else if (type == ESP_CAPTURE_PATH_SET_TYPE_VIDEO_BITRATE) {
        res->bitrate = *(uint32_t *)cfg;
        ret = ESP_CAPTURE_ERR_OK;
        if (res->venc_el && res->bitrate) {
            ret = esp_gmf_video_enc_set_bitrate(res->venc_el, res->bitrate);
        }
    } else if (type == ESP_CAPTURE_PATH_SET_TYPE_REGISTER_ELEMENT) {
        esp_gmf_element_handle_t *el = (esp_gmf_element_handle_t *)cfg;
        if (builder->reg_element) {
            ret = builder->reg_element(builder, *el);
        }
    } else if (type == ESP_CAPTURE_PATH_SET_TYPE_BUILD_PIPELINE) {
        esp_capture_path_build_pipeline_cfg_t *path_cfg = (esp_capture_path_build_pipeline_cfg_t *)cfg;
        esp_capture_gmf_pipeline_cfg_t build_cfg = {
            .element_tags = path_cfg->element_tags,
            .element_num = path_cfg->element_num,
        };
        if (builder->build_pipeline) {
            ret = builder->build_pipeline(builder, path, &build_cfg);
        }
    } else if (type == ESP_CAPTURE_PATH_SET_TYPE_RUN_ONCE) {
        res->run_once = *(bool *)cfg;
        ret = gmf_video_path_set_run_once(&video_path->mngr, res);
    }
    return ret;
}

esp_capture_err_t gmf_video_path_get(esp_capture_path_mngr_if_t *p, uint8_t path, esp_capture_path_get_type_t type, void *cfg, int cfg_size)
{
    gmf_video_path_t *video_path = (gmf_video_path_t *)p;
    if (p == NULL || path >= video_path->mngr.path_num) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    esp_capture_err_t ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
    esp_capture_pipeline_builder_if_t *builder = video_path->mngr.pipeline_builder;
    if (type == ESP_CAPTURE_PATH_GET_ELEMENT) {
        esp_capture_path_element_get_info_t *info = (esp_capture_path_element_get_info_t *)cfg;
        if (builder->get_element) {
            ret = builder->get_element(builder, path, info->element_tag,
                                       (esp_gmf_element_handle_t *)&info->element_hd);
        }
    }
    return ret;
}

esp_capture_err_t gmf_video_path_return_frame(esp_capture_path_mngr_if_t *p, uint8_t path, esp_capture_stream_frame_t *frame)
{
    gmf_video_path_t *video_path = (gmf_video_path_t *)p;
    if (p == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    video_path_res_t *res = (video_path_res_t *)gmf_capture_path_mngr_get_path(&video_path->mngr, path);
    if (res == NULL) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    if (res->video_share_raw) {
        capture_sema_unlock(res->raw_consume_sema);
        return ESP_CAPTURE_ERR_OK;
    }
    int ret = ESP_CAPTURE_ERR_OK;
    if (data_q_have_data(res->video_q)) {
        esp_capture_stream_frame_t *read_frame = NULL;
        int read_size = 0;
        data_q_read_lock(res->video_q, (void **)&read_frame, &read_size);
        ESP_LOGD(TAG, "simple return video data:%x frame:%x\n", frame->data[0], read_frame->data[0]);
        ret = data_q_read_unlock(res->video_q);
    }
    return ret == 0 ? ESP_CAPTURE_ERR_OK : ESP_CAPTURE_ERR_NOT_FOUND;
}

esp_capture_err_t gmf_video_path_stop(esp_capture_path_mngr_if_t *p)
{
    gmf_video_path_t *video_path = (gmf_video_path_t *)p;
    return gmf_capture_path_mngr_stop(&video_path->mngr, video_path_stop, video_path_release);
}

esp_capture_err_t gmf_video_path_close(esp_capture_path_mngr_if_t *p)
{
    esp_capture_err_t ret = gmf_video_path_stop(p);
    gmf_video_path_t *video_path = (gmf_video_path_t *)p;
    for (int i = 0; i < video_path->mngr.path_num; i++) {
        video_path_res_t *res = (video_path_res_t *)gmf_capture_path_mngr_get_idx(&video_path->mngr, i);
        if (res->raw_consume_sema) {
            capture_sema_destroy(res->raw_consume_sema);
            res->raw_consume_sema = NULL;
        }
        if (res->overlay_port) {
            esp_gmf_port_deinit(res->overlay_port);
            res->overlay_port = NULL;
        }
    }
    gmf_capture_path_mngr_close(&video_path->mngr);
    return ret;
}

esp_capture_video_path_mngr_if_t *esp_capture_new_gmf_video_mngr(esp_capture_video_path_mngr_cfg_t *cfg)
{
    if (cfg == NULL || cfg->pipeline_builder == NULL) {
        return NULL;
    }
    gmf_video_path_t *vid_path = (gmf_video_path_t *)calloc(1, sizeof(gmf_video_path_t));
    if (vid_path == NULL) {
        return NULL;
    }
    esp_capture_path_mngr_if_t *base_path = &vid_path->base.base;
    base_path->open = gmf_video_path_open;
    base_path->add_path = gmf_video_path_add_path;
    base_path->enable_path = gmf_video_path_enable_path;
    base_path->start = gmf_video_path_start;
    base_path->set = gmf_video_path_set;
    base_path->get = gmf_video_path_get;
    base_path->return_frame = gmf_video_path_return_frame;
    base_path->stop = gmf_video_path_stop;
    base_path->close = gmf_video_path_close;
    vid_path->base.add_overlay = gmf_video_path_add_overlay;
    vid_path->base.enable_overlay = gmf_video_path_enable_overlay;
    vid_path->mngr.pipeline_builder = cfg->pipeline_builder;
    return &vid_path->base;
}

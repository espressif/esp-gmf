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
#include "esp_capture_types.h"
#include "esp_capture_video_src_if.h"
#include "esp_gmf_video_element.h"
#include "capture_video_src_el.h"
#include "esp_capture_sync.h"
#include "capture_perf_mon.h"
#include "data_queue.h"

#define CAPTURE_SYNC_TOLERANCE 100

static const char *TAG = "VID_SRC";

typedef struct {
    esp_gmf_video_element_t      parent;
    esp_capture_sync_handle_t    sync_handle;
    esp_capture_video_src_if_t  *video_src_if;
    esp_gmf_info_video_t         vid_info;      /*!< Video information */
    uint32_t                     video_frames;  /*!< Processed video frame number */
    uint8_t                      is_open       : 1;
    uint8_t                      can_drop      : 1;
    uint8_t                      fetch_once    : 1;
    uint8_t                      once_finished : 1;
    uint8_t                      frame_reached : 1;
} video_src_t;

static bool video_src_can_drop(uint32_t codec)
{
    return (codec != ESP_CAPTURE_FMT_ID_H264);
}

static esp_gmf_err_io_t video_src_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    video_src_t *video_src = (video_src_t *)handle;
    esp_capture_stream_frame_t frame = {
        .stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO,
    };
    int ret = video_src->video_src_if->acquire_frame(video_src->video_src_if, &frame);
    if (ret >= 0) {
        load->pts = frame.pts;
        load->buf = frame.data;
        load->buf_length = frame.size;
        load->valid_size = frame.size;
    }
    return ret >= 0 ? frame.size : -1;
}

static esp_gmf_err_io_t video_src_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    video_src_t *video_src = (video_src_t *)handle;
    esp_capture_stream_frame_t frame = {
        .stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO,
        .pts = load->pts,
        .data = load->buf,
        .size = load->valid_size,
    };
    return video_src->video_src_if->release_frame(video_src->video_src_if, &frame);
}

static esp_gmf_job_err_t video_src_el_open(esp_gmf_element_handle_t self, void *para)
{
    video_src_t *video_src = (video_src_t *)self;
    if (video_src->video_src_if == NULL) {
        capture_video_src_el_cfg_t *cfg = (capture_video_src_el_cfg_t *)OBJ_GET_CFG(self);
        if (cfg == NULL || cfg->vsrc_if == NULL) {
            return ESP_GMF_JOB_ERR_FAIL;
        }
        video_src->video_src_if = cfg->vsrc_if;
    }
    if (video_src->is_open) {
        CAPTURE_PERF_MON(0, "Video Src Start", {
            video_src->video_src_if->start(video_src->video_src_if);
        });
    }
    video_src->can_drop = video_src_can_drop(video_src->vid_info.format_id);
    esp_gmf_info_video_t vid_info = video_src->vid_info;
    esp_gmf_element_notify_vid_info(self, &vid_info);
    video_src->frame_reached = false;
    return ESP_GMF_JOB_ERR_OK;
}

static uint32_t video_src_calc_pts(video_src_t *video_src, uint32_t frames)
{
    if (video_src->vid_info.fps == 0) {
        return 0;
    }
    return (uint32_t)((uint64_t)frames * 1000 / video_src->vid_info.fps);
}

static esp_gmf_job_err_t video_src_el_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_element_handle_t hd = (esp_gmf_element_handle_t)self;
    video_src_t *video_src = (video_src_t *)self;
    esp_gmf_port_t *in = ESP_GMF_ELEMENT_GET(hd)->in;
    esp_gmf_port_t *out = ESP_GMF_ELEMENT_GET(hd)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    int ret = esp_gmf_port_acquire_in(in, &in_load, 1, -1);
    if (ret < 0 || in_load == NULL) {
        ESP_LOGE(TAG, "Acquire on in port, ret:%d", ret);
        return ret == ESP_GMF_IO_ABORT ? ESP_GMF_JOB_ERR_OK : ESP_GMF_JOB_ERR_FAIL;
    }
    if (video_src->frame_reached == false) {
        CAPTURE_PERF_MON(0, "Video Src Frame Reached", {});
        video_src->frame_reached = true;
    }
    // Do sync control
    uint32_t video_pts = video_src_calc_pts(video_src, video_src->video_frames);
    // Only do sync control if support drop
    if (video_src->sync_handle) {
        uint32_t cur_pts = 0;
        esp_capture_sync_get_current(video_src->sync_handle, &cur_pts);
        if (video_src->can_drop) {
            // Video ahead drop directly
            if (video_pts > cur_pts) {
                esp_gmf_port_release_in(in, in_load, 0);
                return ESP_GMF_JOB_ERR_CONTINUE;
            } else if (video_pts + CAPTURE_SYNC_TOLERANCE < cur_pts) {
                // Video too slow force to use current pts
                video_pts = cur_pts;
            }
        } else {
            // Force to use sync time
            video_pts = cur_pts;
        }
    }
    video_src->video_frames++;
    in_load->pts = video_pts;
    if (video_src->once_finished) {
        ret = ESP_GMF_JOB_ERR_CONTINUE;
    } else {
        out_load = in_load;
        ret = esp_gmf_port_acquire_out(out, &out_load, out_load->valid_size, ESP_GMF_MAX_DELAY);
        if (ret >= 0) {
            esp_gmf_port_release_out(out, out_load, 0);
        }
        // Set fetch finished in once mode
        if (video_src->fetch_once) {
            video_src->once_finished = true;
        }
    }
    esp_gmf_port_release_in(in, in_load, 0);
    return ret;
}

static esp_gmf_job_err_t video_src_el_close(esp_gmf_element_handle_t self, void *para)
{
    video_src_t *video_src = (video_src_t *)self;
    if (video_src->is_open) {
        video_src->video_src_if->stop(video_src->video_src_if);
        video_src->video_src_if->close(video_src->video_src_if);
        video_src->is_open = false;
    }
    video_src->fetch_once = false;
    video_src->once_finished = false;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t video_src_el_destroy(esp_gmf_obj_handle_t self)
{
    esp_gmf_video_el_deinit(self);
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_oal_free(self);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t video_src_cvt_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
{
    int ret = ESP_GMF_ERR_OK;
    esp_gmf_element_handle_t self = (esp_gmf_element_handle_t)ctx;
    if (evt->type != ESP_GMF_EVT_TYPE_REPORT_INFO || evt->sub != ESP_GMF_INFO_VIDEO) {
        return ret;
    }
    esp_gmf_event_state_t state = -1;
    esp_gmf_element_get_state(self, &state);
    esp_gmf_info_video_t *vid_info = evt->payload;
    video_src_t *video_src = (video_src_t *)self;
    video_src->vid_info = *vid_info;
    ESP_LOGI(TAG, "Info %dx%d %dfps",
             (int)vid_info->width, (int)vid_info->height, (int)vid_info->fps);
    if (state == ESP_GMF_EVENT_STATE_NONE) {
        esp_gmf_element_set_state(self, ESP_GMF_EVENT_STATE_INITIALIZED);
    }
    return ret;
}

static esp_err_t video_src_el_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return capture_video_src_el_init((capture_video_src_el_cfg_t *)cfg, (esp_gmf_obj_handle_t *)handle);
}

esp_gmf_err_t capture_video_src_el_init(capture_video_src_el_cfg_t *cfg, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_ERR_INVALID_ARG);
    video_src_t *video_src = esp_gmf_oal_calloc(1, sizeof(video_src_t));

    ESP_GMF_MEM_CHECK(TAG, video_src, return ESP_ERR_NO_MEM);
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)video_src;
    int ret = ESP_GMF_ERR_MEMORY_LACK;
    if (cfg) {
        capture_video_src_el_cfg_t *src_cfg = esp_gmf_oal_calloc(1, sizeof(capture_video_src_el_cfg_t));
        ESP_GMF_MEM_CHECK(TAG, src_cfg, { goto VSRC_FAIL;});
        *src_cfg = *cfg;
        esp_gmf_obj_set_config(obj, cfg, sizeof(capture_video_src_el_cfg_t));
    }
    ret = esp_gmf_obj_set_tag(obj, "vid_src");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto VSRC_FAIL, "Failed set OBJ tag");
    obj->new_obj = video_src_el_new;
    obj->del_obj = video_src_el_destroy;
    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_video_el_init(video_src, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto VSRC_FAIL, "Failed to init video el");

    ESP_GMF_ELEMENT_GET(video_src)->ops.open = video_src_el_open;
    ESP_GMF_ELEMENT_GET(video_src)->ops.process = video_src_el_process;
    ESP_GMF_ELEMENT_GET(video_src)->ops.close = video_src_el_close;
    ESP_GMF_ELEMENT_GET(video_src)->ops.event_receiver = video_src_cvt_received_event_handler;
    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BLOCK(video_src_acquire, video_src_release, NULL, video_src, 0, ESP_GMF_MAX_DELAY);
    ESP_GMF_MEM_CHECK(TAG, in_port, {ret = ESP_GMF_ERR_MEMORY_LACK; goto VSRC_FAIL;});
    *handle = obj;
    esp_gmf_element_register_in_port(*handle, in_port);
    ESP_LOGD(TAG, "Create %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;

VSRC_FAIL:
    esp_gmf_obj_delete(obj);
    return ret;
}

esp_gmf_err_t capture_video_src_el_set_sync_handle(esp_gmf_element_handle_t self, esp_capture_sync_handle_t sync_handle)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, sync_handle, return ESP_GMF_ERR_INVALID_ARG);
    video_src_t *video_src = (video_src_t *)self;
    video_src->sync_handle = sync_handle;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t capture_video_src_el_set_src_if(esp_gmf_element_handle_t self, esp_capture_video_src_if_t *vsrc_if)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, vsrc_if, return ESP_GMF_ERR_INVALID_ARG);
    video_src_t *video_src = (video_src_t *)self;
    video_src->video_src_if = vsrc_if;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t capture_video_src_el_negotiate(esp_gmf_element_handle_t self, esp_capture_video_info_t *nego_info,
                                             esp_capture_video_info_t *out_info)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, nego_info, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, out_info, return ESP_GMF_ERR_INVALID_ARG);
    video_src_t *video_src = (video_src_t *)self;
    if (video_src->video_src_if) {
        if (video_src->is_open == false) {
            CAPTURE_PERF_MON(0, "Video Src Open", {
                video_src->video_src_if->open(video_src->video_src_if);
            });
            video_src->is_open = true;
        }
        return video_src->video_src_if->negotiate_caps(video_src->video_src_if, nego_info, out_info);
    }
    return ESP_GMF_ERR_NOT_SUPPORT;
}

esp_gmf_err_t esp_gmf_video_src_set_single_fetch(esp_gmf_element_handle_t self, bool enable)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    video_src_t *video_src = (video_src_t *)self;
    video_src->fetch_once = enable;
    // Always clear fetch once finished flags
    video_src->once_finished = false;
    return ESP_GMF_ERR_OK;
}

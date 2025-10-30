
/**
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include <stdlib.h>
#include "esp_capture.h"
#include "esp_log.h"
#include "esp_muxer.h"
#include "mp4_muxer.h"
#include "ts_muxer.h"
#include "msg_q.h"
#include "capture_os.h"
#include "data_queue.h"
#include "share_q.h"
#include "esp_capture_sync.h"
#include "msg_q.h"
#include "capture_muxer.h"
#include "capture_utils.h"
#include "capture_pipeline_builder.h"
#include "capture_gmf_mngr.h"
#include "capture_perf_mon.h"
#include "esp_capture_sink.h"
#include "esp_capture_advance.h"

#define TAG                  "ESP_CAPTURE"
#define CAPTURE_MAX_PATH_NUM (3)  /*!< Maximum of capture path supported */
#define CAPTURE_STREAM_Q_NUM (5)

typedef enum {
    CAPTURE_SHARED_BY_USER  = 0,
    CAPTURE_SHARED_BY_MUXER = 1
} capture_shared_type_t;

typedef struct capture_path_t capture_path_t;

struct capture_path_t {
    struct capture_t            *parent;
    uint8_t                      path_type;
    esp_capture_sink_cfg_t       sink_cfg;
    uint8_t                      enable        : 1;
    uint8_t                      sink_disabled : 1;
    uint8_t                      audio_reached : 1;
    uint8_t                      video_reached : 1;
    uint8_t                      muxer_reached : 1;
    // Streaming disabled are controlled by user
    uint8_t                      audio_stream_disabled : 1;
    uint8_t                      video_stream_disabled : 1;
    // Path disable is clear during start and set if meet error
    uint8_t                      audio_path_disabled : 1;
    uint8_t                      video_path_disabled : 1;
    // For audio path
    share_q_handle_t             audio_share_q;
    msg_q_handle_t               audio_q;
    // For video path
    esp_capture_overlay_if_t    *overlay;
    msg_q_handle_t               video_q;
    share_q_handle_t             video_share_q;
    // For muxer path
    capture_muxer_path_handle_t  muxer;
};

typedef struct capture_t {
    esp_capture_advance_cfg_t           cfg;
    esp_capture_cfg_t                   src_cfg;
    capture_path_t                     *path[CAPTURE_MAX_PATH_NUM];
    uint8_t                             path_num;
    esp_capture_sync_handle_t           sync_handle;
    bool                                started;
    capture_mutex_handle_t              api_lock;
    esp_capture_event_cb_t              event_cb;
    void                               *event_ctx;
    esp_capture_pipeline_builder_if_t  *audio_pipe_builder;
    esp_capture_pipeline_builder_if_t  *video_pipe_builder;
    bool                                expert_builder;
} capture_t;

static inline capture_path_t *capture_get_path_by_index(capture_t *capture, uint8_t index)
{
    for (int i = 0; i < capture->path_num; i++) {
        if (capture->path[i] && capture->path[i]->path_type == index) {
            return capture->path[i];
        }
    }
    return NULL;
}

static int capture_frame_avail(void *src, uint8_t sel, esp_capture_stream_frame_t *frame)
{
    capture_t *capture = (capture_t *)src;
    if (capture == NULL) {
        return ESP_CAPTURE_ERR_NOT_FOUND;
    }
    capture_path_t *path = capture_get_path_by_index(capture, sel);
    if (path == NULL) {
        return ESP_CAPTURE_ERR_NOT_FOUND;
    }
    // When path disable drop input data directly
    if (path->sink_disabled || path->parent->started == false) {
        // Can not release it here, callback to user to handle it
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    int ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
    switch (frame->stream_type) {
        default:
            break;
        case ESP_CAPTURE_STREAM_TYPE_VIDEO:
            if (path->video_share_q) {
                ret = share_q_add(path->video_share_q, frame);
            }
            if (path->video_reached == false) {
                CAPTURE_PERF_MON(path->path_type, "First Video Frame Reached", {});
                path->video_reached = true;
            }
            break;
        case ESP_CAPTURE_STREAM_TYPE_AUDIO:
            if (path->audio_share_q) {
                ret = share_q_add(path->audio_share_q, frame);
            }
            if (path->audio_reached == false) {
                CAPTURE_PERF_MON(path->path_type, "First Audio Frame Reached", {});
                path->audio_reached = true;
            }
            break;
    }
    return ret;
}

static int capture_path_event_reached(void *src, uint8_t sel, esp_capture_path_event_type_t event)
{
    capture_t *capture = (capture_t *)src;
    if (capture == NULL) {
        return ESP_CAPTURE_ERR_NOT_FOUND;
    }
    capture_path_t *path = capture_get_path_by_index(capture, sel);
    if (path == NULL) {
        return ESP_CAPTURE_ERR_NOT_FOUND;
    }
    switch (event) {
        default:
            break;
        case ESP_CAPTURE_PATH_EVENT_AUDIO_NOT_SUPPORT:
        case ESP_CAPTURE_PATH_EVENT_AUDIO_FINISHED:
        case ESP_CAPTURE_PATH_EVENT_AUDIO_ERROR: {
            path->audio_path_disabled = true;
            // TODO send fake data into share queue can let user quit
            // But will cause wrong share queue release not existed data
            if (path->audio_share_q) {
                esp_capture_stream_frame_t frame = {0};
                frame.stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO;
                share_q_add(path->audio_share_q, &frame);
            }
            break;
        }
        case ESP_CAPTURE_PATH_EVENT_VIDEO_NOT_SUPPORT:
        case ESP_CAPTURE_PATH_EVENT_VIDEO_FINISHED:
        case ESP_CAPTURE_PATH_EVENT_VIDEO_ERROR: {
            path->video_path_disabled = true;
            if (path->video_share_q) {
                esp_capture_stream_frame_t frame = {0};
                frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;
                share_q_add(path->video_share_q, &frame);
            }
            break;
        }
        case ESP_CAPTURE_PATH_EVENT_VIDEO_PIPELINE_BUILT:
        case ESP_CAPTURE_PATH_EVENT_AUDIO_PIPELINE_BUILT: {
            if (capture->event_cb) {
                esp_capture_event_t app_event = (event == ESP_CAPTURE_PATH_EVENT_VIDEO_PIPELINE_BUILT) ?
                                                 ESP_CAPTURE_EVENT_VIDEO_PIPELINE_BUILT :
                                                 ESP_CAPTURE_EVENT_AUDIO_PIPELINE_BUILT;
                capture->event_cb(app_event, capture->event_ctx);
            }
            break;
        }
    }
    return ESP_CAPTURE_ERR_OK;
}

static void *video_sink_get_q_data_ptr(void *item)
{
    esp_capture_stream_frame_t *frame = (esp_capture_stream_frame_t *)item;
    return frame ? frame->data : NULL;
}

static void *audio_sink_get_q_data_ptr(void *item)
{
    esp_capture_stream_frame_t *frame = (esp_capture_stream_frame_t *)item;
    return frame ? frame->data : NULL;
}

static int video_sink_release_frame(void *item, void *ctx)
{
    esp_capture_stream_frame_t *frame = (esp_capture_stream_frame_t *)item;
    capture_path_t *path = (capture_path_t *)ctx;
    esp_capture_path_mngr_if_t *capture_path = &path->parent->cfg.video_path->base;
    return capture_path->return_frame(capture_path, path->path_type, frame);
}

static int audio_sink_release_frame(void *item, void *ctx)
{
    esp_capture_stream_frame_t *frame = (esp_capture_stream_frame_t *)item;
    capture_path_t *path = (capture_path_t *)ctx;
    esp_capture_path_mngr_if_t *capture_path = &path->parent->cfg.audio_path->base;
    ESP_LOGD(TAG, "Begin to return audio frame");
    return capture_path->return_frame(capture_path, path->path_type, frame);
}

static int muxer_sink_release_frame(void *item, void *ctx)
{
    esp_capture_stream_frame_t *frame = (esp_capture_stream_frame_t *)item;
    capture_path_t *path = (capture_path_t *)ctx;
    return capture_path_release_share(path, frame);
}

const esp_capture_sink_cfg_t *capture_path_get_sink_cfg(capture_path_handle_t path)
{
    return &path->sink_cfg;
}

uint8_t capture_path_get_path_type(capture_path_handle_t path)
{
    return path->path_type;
}

esp_capture_err_t capture_path_release_share(capture_path_handle_t path, esp_capture_stream_frame_t *frame)
{
    if (frame->stream_type == ESP_CAPTURE_STREAM_TYPE_VIDEO) {
        return share_q_release(path->video_share_q, frame);
    } else if (frame->stream_type == ESP_CAPTURE_STREAM_TYPE_AUDIO) {
        return share_q_release(path->audio_share_q, frame);
    }
    return ESP_CAPTURE_ERR_NOT_SUPPORTED;
}

static esp_capture_err_t prepare_audio_share_queue(capture_path_t *path)
{
    // Create share queue to hold sink buffer
    msg_q_handle_t muxer_q = NULL;
    if (path->muxer) {
        muxer_q = capture_muxer_get_muxer_q(path->muxer);
    }
    if (path->audio_share_q == NULL) {
        uint8_t user_count = 0;
        if (muxer_q) {
            user_count++;
        }
        if (path->audio_q) {
            user_count++;
        }
        // When muxer enable it need share queue port 1, so at least create 2
        share_q_cfg_t cfg = {
            .user_count = muxer_q ? 2 : user_count,
            .q_count = 5,
            .item_size = sizeof(esp_capture_stream_frame_t),
            .get_frame_data = audio_sink_get_q_data_ptr,
            .release_frame = audio_sink_release_frame,
            .ctx = path,
            .use_external_q = true,
        };
        path->audio_share_q = share_q_create(&cfg);
        if (path->audio_share_q == NULL) {
            ESP_LOGE(TAG, "Failed to create share q for audio sink");
            return ESP_CAPTURE_ERR_NO_MEM;
        }
    }
    if (path->audio_q) {
        share_q_set_external(path->audio_share_q, CAPTURE_SHARED_BY_USER, path->audio_q);
        share_q_enable(path->audio_share_q, CAPTURE_SHARED_BY_USER, path->enable);
    }
    if (muxer_q) {
        bool muxer_enable = capture_muxer_stream_prepared(path->muxer, ESP_CAPTURE_STREAM_TYPE_AUDIO);
        share_q_set_external(path->audio_share_q, CAPTURE_SHARED_BY_MUXER, muxer_q);
        // Muxer queue is shared by audio and video have user release callback
        share_q_set_user_release(path->audio_share_q, CAPTURE_SHARED_BY_MUXER, muxer_sink_release_frame, path);
        share_q_enable(path->audio_share_q, CAPTURE_SHARED_BY_MUXER, muxer_enable && path->enable);
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t prepare_video_share_queue(capture_path_t *path)
{
    msg_q_handle_t muxer_q = NULL;
    if (path->muxer) {
        muxer_q = capture_muxer_get_muxer_q(path->muxer);
    }
    if (path->video_share_q == NULL) {
        uint8_t user_count = 0;
        if (muxer_q) {
            user_count++;
        }
        if (path->video_q) {
            user_count++;
        }
        // When muxer enable it need share queue port 1, so at least create 2
        share_q_cfg_t cfg = {
            .user_count = muxer_q ? 2 : user_count,
            .q_count = 5,
            .item_size = sizeof(esp_capture_stream_frame_t),
            .get_frame_data = video_sink_get_q_data_ptr,
            .release_frame = video_sink_release_frame,
            .ctx = path,
            .use_external_q = true,
        };
        path->video_share_q = share_q_create(&cfg);
        if (path->video_share_q == NULL) {
            ESP_LOGE(TAG, "Failed to create share q for video sink");
            return ESP_CAPTURE_ERR_NO_MEM;
        }
    }
    if (path->video_q) {
        share_q_set_external(path->video_share_q, CAPTURE_SHARED_BY_USER, path->video_q);
        share_q_enable(path->video_share_q, CAPTURE_SHARED_BY_USER, path->enable);
    }
    if (muxer_q) {
        bool muxer_enable = capture_muxer_stream_prepared(path->muxer, ESP_CAPTURE_STREAM_TYPE_VIDEO);
        share_q_set_external(path->video_share_q, CAPTURE_SHARED_BY_MUXER, muxer_q);
        // Muxer queue is shared by audio and video have user release callback
        share_q_set_user_release(path->video_share_q, CAPTURE_SHARED_BY_MUXER, muxer_sink_release_frame, path);
        share_q_enable(path->video_share_q, CAPTURE_SHARED_BY_MUXER, muxer_enable && path->enable);
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t prepare_audio_path(capture_path_t *path)
{
    if (path->audio_stream_disabled == false && path->audio_q == NULL) {
        path->audio_q = msg_q_create(CAPTURE_STREAM_Q_NUM, sizeof(esp_capture_stream_frame_t));
        if (path->audio_q == NULL) {
            ESP_LOGE(TAG, "Failed to create audio q");
            return ESP_CAPTURE_ERR_NO_MEM;
        }
    }
    return prepare_audio_share_queue(path);
}

static esp_capture_err_t prepare_video_path(capture_path_t *path)
{
    if (path->video_stream_disabled == false && path->video_q == NULL) {
        path->video_q = msg_q_create(CAPTURE_STREAM_Q_NUM, sizeof(esp_capture_stream_frame_t));
        if (path->video_q == NULL) {
            ESP_LOGE(TAG, "Failed to create audio q");
            return ESP_CAPTURE_ERR_NO_MEM;
        }
    }
    return prepare_video_share_queue(path);
}

static esp_capture_err_t prepare_path(capture_path_t *path)
{
    esp_capture_err_t ret = ESP_CAPTURE_ERR_OK;
    if (path->muxer) {
        // Although prepare muxer failed, still fetch audio and video
        ret = capture_muxer_prepare(path->muxer);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to prepare muxer path %d ret %d", path->path_type, ret);
        }
    }
    if (path->sink_cfg.audio_info.format_id) {
        ret = prepare_audio_path(path);
        CAPTURE_RETURN_ON_FAIL(ret);
    }
    if (path->sink_cfg.video_info.format_id) {
        ret = prepare_video_path(path);
        CAPTURE_RETURN_ON_FAIL(ret);
    }
    return ret;
}

static void enable_muxer_share_q(capture_path_t *path, esp_capture_stream_type_t stream_type, bool enable)
{
    share_q_handle_t share_q = NULL;
    if (stream_type == ESP_CAPTURE_STREAM_TYPE_VIDEO) {
        share_q = path->video_share_q;
    } else if (stream_type == ESP_CAPTURE_STREAM_TYPE_AUDIO) {
        share_q = path->audio_share_q;
    }
    if (share_q == NULL) {
        return;
    }
    if (enable) {
        bool prepared = capture_muxer_stream_prepared(path->muxer, stream_type);
        enable = enable && prepared;
    }
    share_q_enable(share_q, CAPTURE_SHARED_BY_MUXER, enable);
}

static void enable_muxer_input(capture_path_t *path, bool enable)
{
    enable_muxer_share_q(path, ESP_CAPTURE_STREAM_TYPE_VIDEO, enable);
    enable_muxer_share_q(path, ESP_CAPTURE_STREAM_TYPE_AUDIO, enable);
}

static esp_capture_err_t start_path(capture_path_t *path)
{
    capture_t *capture = path->parent;
    // Do not prepare resource when not started yet
    if (capture->started == false) {
        return ESP_CAPTURE_ERR_OK;
    }
    // Clear path error status
    path->audio_path_disabled = false;
    path->video_path_disabled = false;

    esp_capture_err_t ret = ESP_CAPTURE_ERR_OK;
    CAPTURE_PERF_MON(path->path_type, "Prepare Path", {
        ret = prepare_path(path);
        CAPTURE_RETURN_ON_FAIL(ret);
    });
    if (path->muxer) {
        CAPTURE_PERF_MON(path->path_type, "Start Muxer", {
            ret = capture_muxer_start(path->muxer);
            CAPTURE_RETURN_ON_FAIL(ret);
        });
        enable_muxer_input(path, true);
    }
    return ret;
}

static void release_path(capture_path_t *path)
{
    if (path->audio_q) {
        msg_q_destroy(path->audio_q);
        path->audio_q = NULL;
    }
    if (path->audio_share_q) {
        share_q_destroy(path->audio_share_q);
        path->audio_share_q = NULL;
    }
    if (path->video_q) {
        msg_q_destroy(path->video_q);
        path->video_q = NULL;
    }
    if (path->video_share_q) {
        share_q_destroy(path->video_share_q);
        path->video_share_q = NULL;
    }
}

static esp_capture_err_t stop_path(capture_path_t *path)
{
    if (path->video_share_q) {
        share_q_enable(path->video_share_q, CAPTURE_SHARED_BY_USER, false);
    }
    if (path->audio_share_q) {
        share_q_enable(path->audio_share_q, CAPTURE_SHARED_BY_USER, false);
    }
    return ESP_CAPTURE_ERR_OK;
}

static int build_audio_path(capture_t *capture, esp_capture_cfg_t *cfg)
{
#ifdef CONFIG_ESP_CAPTURE_ENABLE_AUDIO
    // Create pipeline builder firstly
    esp_capture_gmf_auto_audio_pipeline_cfg_t builder_cfg = {
        .aud_src = cfg->audio_src,
    };
    capture->audio_pipe_builder = esp_capture_create_auto_audio_pipeline(&builder_cfg);
    CAPTURE_CHECK_MEM_RET(capture->audio_pipe_builder, "audio pipeline builder", ESP_CAPTURE_ERR_NO_MEM);

    // Create audio path use pipeline builder
    esp_capture_audio_path_mngr_cfg_t path_cfg = {
        .pipeline_builder = capture->audio_pipe_builder,
    };
    capture->cfg.audio_path = esp_capture_new_gmf_audio_mngr(&path_cfg);
    CAPTURE_CHECK_MEM_RET(capture->cfg.audio_path, "GMF audio path", ESP_CAPTURE_ERR_NO_MEM);

    return ESP_CAPTURE_ERR_OK;
#else
    ESP_LOGE(TAG, "CONFIG_ESP_CAPTURE_ENABLE_AUDIO disabled");
    return ESP_CAPTURE_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_ESP_CAPTURE_ENABLE_AUDIO */
}

static esp_capture_err_t build_video_path(capture_t *capture, esp_capture_cfg_t *cfg)
{
#ifdef CONFIG_ESP_CAPTURE_ENABLE_VIDEO
    // Create pipeline builder firstly
    esp_capture_gmf_auto_video_pipeline_cfg_t builder_cfg = {
        .vid_src = cfg->video_src,
    };
    capture->video_pipe_builder = esp_capture_create_auto_video_pipeline(&builder_cfg);
    CAPTURE_CHECK_MEM_RET(capture->video_pipe_builder, "video pipeline builder", ESP_CAPTURE_ERR_NO_MEM);
    // Create audio path use pipeline builder
    esp_capture_video_path_mngr_cfg_t path_cfg = {
        .pipeline_builder = capture->video_pipe_builder,
    };
    capture->cfg.video_path = esp_capture_new_gmf_video_mngr(&path_cfg);
    CAPTURE_CHECK_MEM_RET(capture->cfg.video_path, "GMF video path", ESP_CAPTURE_ERR_NO_MEM);
    return ESP_CAPTURE_ERR_OK;
#else
    ESP_LOGE(TAG, "CONFIG_ESP_CAPTURE_ENABLE_VIDEO disabled");
    return ESP_CAPTURE_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_ESP_CAPTURE_ENABLE_VIDEO */
}

static esp_capture_path_mngr_if_t *capture_get_mngr_by_stream_type(capture_t *capture,
                                                                   esp_capture_stream_type_t stream_type)
{
    esp_capture_path_mngr_if_t *mngr = NULL;
    if (stream_type == ESP_CAPTURE_STREAM_TYPE_AUDIO) {
        mngr = &capture->cfg.audio_path->base;
    } else if (stream_type == ESP_CAPTURE_STREAM_TYPE_VIDEO) {
        mngr = &capture->cfg.video_path->base;
    }
    return mngr;
}

static bool capture_same_sink_cfg(esp_capture_sink_cfg_t *old, esp_capture_sink_cfg_t *sink_cfg)
{
    if (old->audio_info.format_id != sink_cfg->audio_info.format_id ||
        old->video_info.format_id != sink_cfg->video_info.format_id) {
        return false;
    }
    if (old->audio_info.format_id) {
        if (old->audio_info.sample_rate != sink_cfg->audio_info.sample_rate ||
            old->audio_info.channel != sink_cfg->audio_info.channel ||
            old->audio_info.bits_per_sample != sink_cfg->audio_info.bits_per_sample) {
            return false;
        }
    }
    if (old->video_info.format_id) {
        if (old->video_info.width != sink_cfg->video_info.width ||
            old->video_info.height != sink_cfg->video_info.height ||
            old->video_info.fps != sink_cfg->video_info.fps) {
            return false;
        }
    }
    return true;
}

esp_capture_err_t esp_capture_set_thread_scheduler(esp_capture_thread_scheduler_cb_t thread_scheduler)
{
    capture_thread_set_scheduler(thread_scheduler);
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t esp_capture_open(esp_capture_cfg_t *cfg, esp_capture_handle_t *h)
{
    if (cfg == NULL || h == NULL || (cfg->audio_src == NULL && cfg->video_src == NULL)) {
        ESP_LOGE(TAG, "Invalid argument cfg:%p capture:%p audio src:%p video_src:%p",
                 cfg, h, cfg ? cfg->audio_src : NULL, cfg ? cfg->video_src : NULL);
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    capture_t *capture = capture_calloc(1, sizeof(capture_t));
    if (capture == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for capture");
        return ESP_CAPTURE_ERR_NO_MEM;
    }
    do {
        capture_mutex_create(&capture->api_lock);
        if (capture->api_lock == NULL) {
            break;
        }
        if (cfg->sync_mode != ESP_CAPTURE_SYNC_MODE_NONE) {
            esp_capture_sync_create(cfg->sync_mode, &capture->sync_handle);
        }
        // Create audio pipeline builder firstly
        int ret = ESP_CAPTURE_ERR_OK;
        if (cfg->audio_src) {
            ret = build_audio_path(capture, cfg);
            if (ret != ESP_CAPTURE_ERR_OK) {
                break;
            }
        }
        if (cfg->video_src) {
            ret = build_video_path(capture, cfg);
            if (ret != ESP_CAPTURE_ERR_OK) {
                break;
            }
        }
        esp_capture_path_mngr_if_t *audio_path = &capture->cfg.audio_path->base;
        if (audio_path) {
            esp_capture_path_cfg_t path_cfg = {
                .src_ctx = capture,
                .frame_avail = capture_frame_avail,
                .event_cb = capture_path_event_reached,
            };
            if (audio_path->open(audio_path, &path_cfg) != ESP_CAPTURE_ERR_OK) {
                ESP_LOGE(TAG, "Failed to open audio path");
                break;
            }
        }
        esp_capture_path_mngr_if_t *video_path = &capture->cfg.video_path->base;
        if (video_path) {
            esp_capture_path_cfg_t path_cfg = {
                .src_ctx = capture,
                .frame_avail = capture_frame_avail,
                .event_cb = capture_path_event_reached,
            };
            if (video_path->open(video_path, &path_cfg) != ESP_CAPTURE_ERR_OK) {
                ESP_LOGE(TAG, "Failed to video capture path");
                break;
            }
        }
        capture->src_cfg = *cfg;
        *h = capture;
        return ESP_CAPTURE_ERR_OK;
    } while (0);
    esp_capture_close(capture);
    return ESP_CAPTURE_ERR_NO_RESOURCES;
}

esp_capture_err_t esp_capture_advance_open(esp_capture_advance_cfg_t *cfg, esp_capture_handle_t *h)
{
    if (cfg == NULL || h == NULL || (cfg->audio_path == NULL && cfg->video_path == NULL)) {
        ESP_LOGE(TAG, "Invalid argument cfg:%p capture:%p audio src:%p video_src:%p",
                 cfg, h, cfg ? cfg->audio_path : NULL, cfg ? cfg->video_path : NULL);
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    capture_t *capture = capture_calloc(1, sizeof(capture_t));
    if (capture == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for capture");
        return ESP_CAPTURE_ERR_NO_MEM;
    }
    do {
        capture_mutex_create(&capture->api_lock);
        if (capture->api_lock == NULL) {
            break;
        }
        if (cfg->sync_mode != ESP_CAPTURE_SYNC_MODE_NONE) {
            esp_capture_sync_create(cfg->sync_mode, &capture->sync_handle);
            if (capture->sync_handle == NULL) {
                ESP_LOGE(TAG, "Failed to create capture sync");
                break;
            }
        }
        capture->expert_builder = true;
        esp_capture_path_mngr_if_t *audio_path = &cfg->audio_path->base;
        if (audio_path) {
            esp_capture_path_cfg_t path_cfg = {
                .src_ctx = capture,
                .frame_avail = capture_frame_avail,
                .event_cb = capture_path_event_reached,
            };
            if (audio_path->open(audio_path, &path_cfg) != ESP_CAPTURE_ERR_OK) {
                ESP_LOGE(TAG, "Failed to open audio path");
                break;
            }
        }
        esp_capture_path_mngr_if_t *video_path = &cfg->video_path->base;
        if (video_path) {
            esp_capture_path_cfg_t path_cfg = {
                .src_ctx = capture,
                .frame_avail = capture_frame_avail,
                .event_cb = capture_path_event_reached,
            };
            if (video_path->open(video_path, &path_cfg) != ESP_CAPTURE_ERR_OK) {
                ESP_LOGE(TAG, "Failed to video capture path");
                break;
            }
        }
        capture->cfg = *cfg;
        *h = capture;
        return ESP_CAPTURE_ERR_OK;
    } while (0);
    esp_capture_close(capture);
    return ESP_CAPTURE_ERR_NO_RESOURCES;
}

esp_capture_err_t esp_capture_set_event_cb(esp_capture_handle_t h, esp_capture_event_cb_t cb, void *ctx)
{
    if (h == NULL || cb == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    capture_t *capture = (capture_t *)h;
    capture->event_cb = cb;
    capture->event_ctx = ctx;
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t esp_capture_register_element(esp_capture_handle_t h, esp_capture_stream_type_t stream_type,
                                               esp_gmf_element_handle_t element)
{
    if (h == NULL || element == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    capture_t *capture = (capture_t *)h;
    esp_capture_path_mngr_if_t *path_mngr = capture_get_mngr_by_stream_type(capture, stream_type);
    if (path_mngr == NULL) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    int ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
    if (path_mngr->set) {
        // Currently set to first path only
        ret = path_mngr->set(path_mngr, 0, ESP_CAPTURE_PATH_SET_TYPE_REGISTER_ELEMENT,
                             &element, sizeof(esp_gmf_element_handle_t));
    }
    return ret;
}

static esp_capture_err_t capture_add_path(capture_t *capture, capture_path_t *cur)
{
    esp_capture_err_t ret = ESP_CAPTURE_ERR_OK;
    // Clear sink codec when path not existed
    esp_capture_path_mngr_if_t *audio_path = &capture->cfg.audio_path->base;
    if (audio_path == NULL) {
        cur->sink_cfg.audio_info.format_id = ESP_CAPTURE_FMT_ID_NONE;
    }
    if (cur->sink_cfg.audio_info.format_id) {
        esp_capture_stream_info_t aud_info = {};
        aud_info.audio_info = cur->sink_cfg.audio_info;
        ret = audio_path->add_path(audio_path, cur->path_type, &aud_info);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to add audio path ret %d", ret);
            return ret;
        }
    }
    esp_capture_path_mngr_if_t *video_path = &capture->cfg.video_path->base;
    if (video_path == NULL) {
        cur->sink_cfg.video_info.format_id = ESP_CAPTURE_FMT_ID_NONE;
    }
    if (cur->sink_cfg.video_info.format_id) {
        esp_capture_stream_info_t vid_info = {};
        vid_info.video_info = cur->sink_cfg.video_info;
        ret = video_path->add_path(video_path, cur->path_type, &vid_info);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to add video path ret %d", ret);
            return ret;
        }
    }
    if (capture->sync_handle) {
        if (audio_path) {
            audio_path->set(audio_path, cur->path_type, ESP_CAPTURE_PATH_SET_TYPE_SYNC_HANDLE, &capture->sync_handle, sizeof(esp_capture_sync_handle_t *));
        }
        if (video_path) {
            video_path->set(video_path, cur->path_type, ESP_CAPTURE_PATH_SET_TYPE_SYNC_HANDLE, &capture->sync_handle, sizeof(esp_capture_sync_handle_t *));
        }
    }
    return ret;
}

static void capture_reset_sink(capture_path_t *path)
{
    path->enable = false;
    path->sink_disabled = false;
    path->audio_reached = false;
    path->video_reached = false;
    path->muxer_reached = false;
    path->audio_stream_disabled = false;
    path->video_stream_disabled = false;
    path->audio_path_disabled = false;
    path->video_path_disabled = false;
}

esp_capture_err_t esp_capture_sink_setup(esp_capture_handle_t h, uint8_t type, esp_capture_sink_cfg_t *sink_info,
                                         esp_capture_sink_handle_t *path)
{
    capture_t *capture = (capture_t *)h;
    if (capture == NULL || sink_info == NULL || path == NULL ||
        (sink_info->audio_info.format_id == ESP_CAPTURE_FMT_ID_NONE &&
         sink_info->video_info.format_id == ESP_CAPTURE_FMT_ID_NONE)) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    capture_mutex_lock(capture->api_lock, CAPTURE_MAX_LOCK_TIME);
    esp_capture_err_t ret = ESP_CAPTURE_ERR_OK;
    do {
        capture_path_t *cur = capture_get_path_by_index(capture, type);
        if (capture->started) {
            if (cur && capture_same_sink_cfg(&cur->sink_cfg, sink_info)) {
                // Allow get sink handle use same setup
                *path = (esp_capture_sink_handle_t)cur;
                ret = ESP_CAPTURE_ERR_OK;
                break;
            }
            ESP_LOGE(TAG, "Not support add path after started");
            CAPTURE_BREAK_SET_RETURN(ret, ESP_CAPTURE_ERR_INVALID_STATE);
        }
        if (capture->cfg.audio_path == NULL && capture->cfg.video_path == NULL) {
            ESP_LOGE(TAG, "Only support add sink for no path manager");
            CAPTURE_BREAK_SET_RETURN(ret, ESP_CAPTURE_ERR_NOT_SUPPORTED);
        }
        // Path already added
        if (cur) {
            if (cur->enable && capture->started) {
                ESP_LOGW(TAG, "Not allowed to change sink during running");
                CAPTURE_BREAK_SET_RETURN(ret, ESP_CAPTURE_ERR_INVALID_STATE);
            }
            cur->sink_cfg = *sink_info;
            *path = (esp_capture_sink_handle_t)cur;
            ret = capture_add_path(capture, cur);
            // Clear status
            capture_reset_sink(cur);
            break;
        }
        if (capture->path_num >= CAPTURE_MAX_PATH_NUM) {
            ESP_LOGE(TAG, "Only support max path %d", CAPTURE_MAX_PATH_NUM);
            CAPTURE_BREAK_SET_RETURN(ret, ESP_CAPTURE_ERR_NOT_ENOUGH);
        }
        capture->path[capture->path_num] = (capture_path_t *)capture_calloc(1, sizeof(capture_path_t));
        if (path == NULL) {
            CAPTURE_BREAK_SET_RETURN(ret, ESP_CAPTURE_ERR_NO_MEM);
        }
        cur = capture->path[capture->path_num];
        cur->path_type = type;
        cur->sink_cfg = *sink_info;
        cur->parent = capture;
        ret = capture_add_path(capture, cur);
        if (ret != ESP_CAPTURE_ERR_OK) {
            capture_free(cur);
            CAPTURE_BREAK_SET_RETURN(ret, ret);
        }
        capture->path_num++;
        *path = (esp_capture_sink_handle_t)cur;
        ret = ESP_CAPTURE_ERR_OK;
    } while (0);
    capture_mutex_unlock(capture->api_lock);
    return ret;
}

esp_capture_err_t esp_capture_sink_add_muxer(esp_capture_sink_handle_t h, esp_capture_muxer_cfg_t *muxer_cfg)
{
    capture_path_t *path = (capture_path_t *)h;
    if (path == NULL || muxer_cfg == NULL || path->parent == NULL ||
        muxer_cfg->base_config == NULL || muxer_cfg->cfg_size < sizeof(esp_muxer_config_t)) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    capture_t *capture = path->parent;
    int ret = ESP_CAPTURE_ERR_OK;
    capture_mutex_lock(capture->api_lock, CAPTURE_MAX_LOCK_TIME);
    do {
        if (capture->started) {
            ESP_LOGE(TAG, "Not support add muxer after started");
            CAPTURE_BREAK_SET_RETURN(ret, ESP_CAPTURE_ERR_INVALID_STATE);
        }
        if (path->muxer) {
            ESP_LOGE(TAG, "Muxer already added");
            CAPTURE_BREAK_SET_RETURN(ret, ESP_CAPTURE_ERR_INVALID_STATE);
        }
        ret = capture_muxer_open(path, muxer_cfg, &path->muxer);
    } while (0);
    capture_mutex_unlock(capture->api_lock);
    return ret;
}

esp_capture_err_t esp_capture_sink_add_overlay(esp_capture_sink_handle_t h, esp_capture_overlay_if_t *overlay)
{
    capture_path_t *path = (capture_path_t *)h;
    if (path == NULL || path->parent == NULL || overlay == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    capture_t *capture = path->parent;
    capture_mutex_lock(capture->api_lock, CAPTURE_MAX_LOCK_TIME);
    esp_capture_err_t ret = ESP_CAPTURE_ERR_OK;
    esp_capture_video_path_mngr_if_t *video_path = capture->cfg.video_path;
    if (video_path == NULL || video_path->add_overlay == NULL) {
        ESP_LOGE(TAG, "Capture path not added, not support overlay");
        ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
    } else {
        ret = video_path->add_overlay(video_path, path->path_type, overlay);
    }
    capture_mutex_unlock(capture->api_lock);
    return ret;
}

esp_capture_err_t esp_capture_sink_enable_muxer(esp_capture_sink_handle_t h, bool enable)
{
    capture_path_t *path = (capture_path_t *)h;
    if (path == NULL || path->parent == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    capture_mutex_lock(path->parent->api_lock, CAPTURE_MAX_LOCK_TIME);
    esp_capture_err_t ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
    if (path->muxer) {
        ret = capture_muxer_enable(path->muxer, enable);
    }
    capture_mutex_unlock(path->parent->api_lock);
    return ret;
}

esp_capture_err_t esp_capture_sink_enable_overlay(esp_capture_sink_handle_t h, bool enable)
{
    capture_path_t *path = (capture_path_t *)h;
    if (path == NULL || path->parent == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    capture_t *capture = path->parent;
    capture_mutex_lock(capture->api_lock, CAPTURE_MAX_LOCK_TIME);
    int ret = ESP_CAPTURE_ERR_OK;
    esp_capture_video_path_mngr_if_t *video_path = capture->cfg.video_path;
    if (video_path == NULL || video_path->enable_overlay == NULL) {
        ESP_LOGE(TAG, "Capture path not added, not support overlay");
        ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
    } else {
        ret = video_path->enable_overlay(video_path, path->path_type, enable);
    }
    capture_mutex_unlock(capture->api_lock);
    return ret;
}

esp_capture_err_t esp_capture_sink_build_pipeline(esp_capture_sink_handle_t h, esp_capture_stream_type_t stream_type,
                                                  const char **element_tags, uint8_t element_num)
{
    capture_path_t *path = (capture_path_t *)h;
    if (path == NULL || path->parent == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    capture_t *capture = path->parent;
    capture_mutex_lock(capture->api_lock, CAPTURE_MAX_LOCK_TIME);
    if (capture->started) {
        ESP_LOGE(TAG, "Not support build pipeline after started");
        capture_mutex_unlock(capture->api_lock);
        return ESP_CAPTURE_ERR_INVALID_STATE;
    }
    int ret = ESP_CAPTURE_ERR_OK;
    esp_capture_path_mngr_if_t *mngr = capture_get_mngr_by_stream_type(capture, stream_type);
    if (mngr) {
        esp_capture_path_build_pipeline_cfg_t pipe_cfg = {
            .element_tags = element_tags,
            .element_num = element_num,
        };
        ret = mngr->set(mngr, path->path_type, ESP_CAPTURE_PATH_SET_TYPE_BUILD_PIPELINE, &pipe_cfg, sizeof(pipe_cfg));
    } else {
        ESP_LOGE(TAG, "Capture path manager not found for stream %d", stream_type);
        ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    capture_mutex_unlock(capture->api_lock);
    return ret;
}

esp_capture_err_t esp_capture_sink_get_element_by_tag(esp_capture_sink_handle_t h,
                                                      esp_capture_stream_type_t stream_type,
                                                      const char *element_tag, esp_gmf_element_handle_t *element)
{
    capture_path_t *path = (capture_path_t *)h;
    if (path == NULL || path->parent == NULL || element == NULL || element_tag == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    capture_t *capture = path->parent;
    capture_mutex_lock(capture->api_lock, CAPTURE_MAX_LOCK_TIME);
    int ret = ESP_CAPTURE_ERR_OK;
    esp_capture_path_mngr_if_t *mngr = capture_get_mngr_by_stream_type(capture, stream_type);
    if (mngr) {
        esp_capture_path_element_get_info_t ele_get_cfg = {
            .element_tag = element_tag,
            .element_hd = NULL,
        };
        ret = mngr->get(mngr, path->path_type, ESP_CAPTURE_PATH_GET_ELEMENT, &ele_get_cfg, sizeof(ele_get_cfg));
        if (ret == ESP_CAPTURE_ERR_OK) {
            *element = (esp_gmf_element_handle_t)ele_get_cfg.element_hd;
        }
    } else {
        ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    capture_mutex_unlock(capture->api_lock);
    return ret;
}

static void flush_path_stream_output(capture_path_t *path)
{
    esp_capture_stream_frame_t frame = {0};
    if (path->video_share_q) {
        share_q_recv_all(path->video_share_q, &frame);
    }
    if (path->audio_share_q) {
        share_q_recv_all(path->audio_share_q, &frame);
    }
}

esp_capture_err_t esp_capture_sink_enable(esp_capture_sink_handle_t h, esp_capture_run_mode_t run_type)
{
    capture_path_t *path = (capture_path_t *)h;
    if (path == NULL || path->parent == NULL) {
        ESP_LOGE(TAG, "Fail to enable path for path:%p parent:%p", path, path ? path->parent : NULL);
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    capture_t *capture = path->parent;
    capture_mutex_lock(capture->api_lock, CAPTURE_MAX_LOCK_TIME);

    bool enable = run_type != ESP_CAPTURE_RUN_MODE_DISABLE;
    int ret = ESP_CAPTURE_ERR_OK;
    // Handle one shot mode for video
    esp_capture_path_mngr_if_t *video_path = &capture->cfg.video_path->base;
    if (video_path) {
        bool run_once = (run_type == ESP_CAPTURE_RUN_MODE_ONESHOT);
        ret = video_path->set(video_path, path->path_type, ESP_CAPTURE_PATH_SET_TYPE_RUN_ONCE, &run_once, sizeof(bool));
    }
    if (path->enable == enable) {
        capture_mutex_unlock(capture->api_lock);
        return ret;
    }

    if (enable) {
        path->enable = true;
        path->sink_disabled = false;
        // Prepare so that data pushed to path queue
        ret = start_path(path);
    } else {
        // Stop from last to first one src -> path -> muxer
        path->sink_disabled = true;
        if (path->muxer) {
            enable_muxer_input(path, false);
            capture_muxer_stop(path->muxer);
        }
        flush_path_stream_output(path);
    }
    esp_capture_path_mngr_if_t *audio_path = &capture->cfg.audio_path->base;
    if (audio_path) {
        ret = audio_path->enable_path(audio_path, path->path_type, enable);
    }
    if (video_path) {
        ret = video_path->enable_path(video_path, path->path_type, enable);
    }
    path->enable = enable;
    if (enable == false) {
        ret = stop_path(path);
    }
    capture_mutex_unlock(capture->api_lock);
    return ret;
}

esp_capture_err_t esp_capture_sink_disable_stream(esp_capture_sink_handle_t h, esp_capture_stream_type_t stream_type)
{
    capture_path_t *path = (capture_path_t *)h;
    if (path == NULL || path->parent == NULL || stream_type == ESP_CAPTURE_STREAM_TYPE_NONE) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    capture_t *capture = path->parent;
    esp_capture_err_t ret = ESP_CAPTURE_ERR_OK;
    capture_mutex_lock(capture->api_lock, CAPTURE_MAX_LOCK_TIME);
    do {
        if (capture->started) {
            ret = ESP_CAPTURE_ERR_INVALID_STATE;
            break;
        }
        if (stream_type == ESP_CAPTURE_STREAM_TYPE_MUXER) {
            if (path->muxer) {
                ret = capture_muxer_disable_streaming(path->muxer);
            } else {
                ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
            }
        } else if (stream_type == ESP_CAPTURE_STREAM_TYPE_AUDIO) {
            path->audio_stream_disabled = true;
        } else if (stream_type == ESP_CAPTURE_STREAM_TYPE_VIDEO) {
            path->video_stream_disabled = true;
        }
    } while (0);
    capture_mutex_unlock(capture->api_lock);
    return ret;
}

esp_capture_err_t esp_capture_start(esp_capture_handle_t h)
{
    CAPTURE_VERIFY_PTR_RET(h, "Wrong capture handle");
    capture_t *capture = (capture_t *)h;
    capture_mutex_lock(capture->api_lock, CAPTURE_MAX_LOCK_TIME);
    if (capture->started) {
        capture_mutex_unlock(capture->api_lock);
        ESP_LOGW(TAG, "Already started");
        return ESP_CAPTURE_ERR_OK;
    }
    int ret = ESP_CAPTURE_ERR_OK;
    capture->started = true;
    for (int i = 0; i < capture->path_num; i++) {
        capture_path_t *path = capture->path[i];
        if (path == NULL) {
            continue;
        }
        path->audio_reached = false;
        path->video_reached = false;
        CAPTURE_PERF_MON(i, "Start Path", {
            // Prepare queues and related resource
            ret = start_path(path);
        });
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to start capture path %d ret %d", i, ret);
            // When fail try to start next path
            continue;
        }
    }
    // We start video firstly for initialize video take long time than audio
    esp_capture_path_mngr_if_t *video_path = &capture->cfg.video_path->base;
    if (video_path) {
        CAPTURE_PERF_MON(0, "Start Video Path", {
            ret = video_path->start(video_path);
        });
        CAPTURE_LOG_ON_ERR(ret, "Fail to start video path ret %d", ret);
    }
    esp_capture_path_mngr_if_t *audio_path = &capture->cfg.audio_path->base;
    if (audio_path) {
        CAPTURE_PERF_MON(0, "Start Audio Path", {
            ret = audio_path->start(audio_path);
        });
        CAPTURE_LOG_ON_ERR(ret, "Fail to start audio path ret %d", ret);
    }
    if (capture->sync_handle) {
        esp_capture_sync_on(capture->sync_handle);
    }
    if (ret == ESP_CAPTURE_ERR_OK && capture->event_cb) {
        capture->event_cb(ESP_CAPTURE_EVENT_STARTED, capture->event_ctx);
    }
    CAPTURE_PERF_MON(0, "Start Finished", {});
    capture_mutex_unlock(capture->api_lock);
    return ret;
}

esp_capture_err_t esp_capture_sink_set_bitrate(esp_capture_sink_handle_t h, esp_capture_stream_type_t stream_type, uint32_t bitrate)
{
    capture_path_t *path = (capture_path_t *)h;
    if (path == NULL || path->parent == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    capture_t *capture = path->parent;
    capture_mutex_lock(capture->api_lock, CAPTURE_MAX_LOCK_TIME);
    esp_capture_path_set_type_t type = ESP_CAPTURE_PATH_SET_TYPE_NONE;
    int ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
    if (stream_type == ESP_CAPTURE_STREAM_TYPE_VIDEO) {
        type = ESP_CAPTURE_PATH_SET_TYPE_VIDEO_BITRATE;
        esp_capture_path_mngr_if_t *video_path = &capture->cfg.video_path->base;
        if (video_path) {
            ret = video_path->set(video_path, path->path_type, type, &bitrate, sizeof(uint32_t));
        }
    } else if (stream_type == ESP_CAPTURE_STREAM_TYPE_AUDIO) {
        type = ESP_CAPTURE_PATH_SET_TYPE_AUDIO_BITRATE;
        esp_capture_path_mngr_if_t *audio_path = &capture->cfg.audio_path->base;
        if (audio_path) {
            ret = audio_path->set(audio_path, path->path_type, type, &bitrate, sizeof(uint32_t));
        }
    }
    capture_mutex_unlock(capture->api_lock);
    return ret;
}

esp_capture_err_t esp_capture_sink_acquire_frame(esp_capture_sink_handle_t h, esp_capture_stream_frame_t *frame, bool no_wait)
{
    capture_path_t *path = (capture_path_t *)h;
    if (path == NULL || path->parent == NULL || frame == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    // TODO not add lock user need care the timing
    if (path->enable == false) {
        return ESP_CAPTURE_ERR_INVALID_STATE;
    }
    int ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
    switch (frame->stream_type) {
        case ESP_CAPTURE_STREAM_TYPE_VIDEO:
            if (path->video_path_disabled) {
                // Only receive all of share q port data
                while (msg_q_recv(path->video_q, frame, sizeof(esp_capture_stream_frame_t), true) == 0) {
                    share_q_release(path->video_share_q, frame);
                }
                return ESP_CAPTURE_ERR_NOT_FOUND;
            }
            // TODO check whether share q send frame only
            if (path->video_q) {
                ret = msg_q_recv(path->video_q, frame, sizeof(esp_capture_stream_frame_t), no_wait);
                ret = (ret == 0) ? ESP_CAPTURE_ERR_OK : ESP_CAPTURE_ERR_NOT_FOUND;
            }
            break;
        case ESP_CAPTURE_STREAM_TYPE_AUDIO:
            if (path->audio_path_disabled) {
                while (msg_q_recv(path->audio_q, frame, sizeof(esp_capture_stream_frame_t), true) == 0) {
                    share_q_release(path->audio_share_q, frame);
                }
                return ESP_CAPTURE_ERR_NOT_FOUND;
            }
            if (path->audio_q) {
                ret = msg_q_recv(path->audio_q, frame, sizeof(esp_capture_stream_frame_t), no_wait);
                ret = (ret == 0) ? ESP_CAPTURE_ERR_OK : ESP_CAPTURE_ERR_NOT_FOUND;
            }
            break;
        case ESP_CAPTURE_STREAM_TYPE_MUXER:
            if (path->muxer) {
                ret = capture_muxer_acquire_frame(path->muxer, frame, no_wait);
            }
            break;
        default:
            break;
    }
    return ret;
}

esp_capture_err_t esp_capture_sink_release_frame(esp_capture_sink_handle_t h, esp_capture_stream_frame_t *frame)
{
    capture_path_t *path = (capture_path_t *)h;
    if (path == NULL || path->parent == NULL || frame == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    // TODO not add lock user need care the timing
    if (path->enable == false) {
        return ESP_CAPTURE_ERR_INVALID_STATE;
    }
    int ret = ESP_CAPTURE_ERR_OK;
    switch (frame->stream_type) {
        case ESP_CAPTURE_STREAM_TYPE_VIDEO:
            if (path->video_share_q) {
                share_q_release(path->video_share_q, frame);
            }
            break;
        case ESP_CAPTURE_STREAM_TYPE_AUDIO:
            if (path->audio_share_q) {
                share_q_release(path->audio_share_q, frame);
            }
            break;
        case ESP_CAPTURE_STREAM_TYPE_MUXER:
            if (path->muxer) {
                ret = capture_muxer_release_frame(path->muxer, frame);
            }
            break;
        default:
            return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    return ret;
}

esp_capture_err_t esp_capture_stop(esp_capture_handle_t h)
{
    capture_t *capture = (capture_t *)h;
    if (capture == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    capture_mutex_lock(capture->api_lock, CAPTURE_MAX_LOCK_TIME);
    if (capture->started == false) {
        capture_mutex_unlock(capture->api_lock);
        return ESP_CAPTURE_ERR_INVALID_STATE;
    }
    if (capture->event_cb) {
        capture->event_cb(ESP_CAPTURE_EVENT_STOPPED, capture->event_ctx);
    }
    capture->started = false;

    CAPTURE_PERF_MON(0, "Stop Capture", {});

    // Stop muxer before path for muxer may still keep capture path data
    for (int i = 0; i < capture->path_num; i++) {
        capture_path_t *path = capture->path[i];
        if (path && path->muxer) {
            enable_muxer_input(path, false);
            CAPTURE_PERF_MON(i, "Stop Muxer", {
                capture_muxer_stop(path->muxer);
            });
        }
    }
    // Receive all output firstly to let capture path quit
    for (int i = 0; i < capture->path_num; i++) {
        capture_path_t *path = capture->path[i];
        // Disable path
        path->sink_disabled = true;
        CAPTURE_PERF_MON(i, "Flush output", {
            flush_path_stream_output(path);
        });
    }
    // Stop path
    esp_capture_path_mngr_if_t *audio_path = &capture->cfg.audio_path->base;
    if (audio_path) {
        CAPTURE_PERF_MON(0, "Stop Audio Path", {
            audio_path->stop(audio_path);
        });
    }
    esp_capture_path_mngr_if_t *video_path = &capture->cfg.video_path->base;
    if (video_path) {
        CAPTURE_PERF_MON(0, "Stop Video Path", {
            video_path->stop(video_path);
        });
    }

    // Send empty data to let user quit
    esp_capture_stream_frame_t frame = {0};
    for (int i = 0; i < capture->path_num; i++) {
        capture_path_t *path = capture->path[i];
        flush_path_stream_output(path);
        if (path->video_share_q) {
            frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;
            share_q_add(path->video_share_q, &frame);
        }
        if (path->audio_share_q) {
            frame.stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO;
            share_q_add(path->audio_share_q, &frame);
        }
    }

    for (int i = 0; i < capture->path_num; i++) {
        capture_path_t *path = capture->path[i];
        stop_path(path);
        release_path(path);
        path->sink_disabled = false;
    }
    if (capture->sync_handle) {
        esp_capture_sync_off(capture->sync_handle);
    }
    CAPTURE_PERF_MON(0, "Stop Capture End", {});
    capture_mutex_unlock(capture->api_lock);
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t esp_capture_close(esp_capture_handle_t h)
{
    CAPTURE_VERIFY_PTR_RET(h, "capture handle");
    capture_t *capture = (capture_t *)h;
    // Internal do stop firstly
    esp_capture_stop(h);

    for (int i = 0; i < capture->path_num; i++) {
        capture_path_t *path = capture->path[i];
        if (path == NULL) {
            continue;
        }
        if (path->muxer) {
            capture_muxer_close(path->muxer);
            path->muxer = NULL;
        }
        capture_free(path);
        capture->path[i] = NULL;
    }

    // Close path and destroy pipeline builder for auto mode
#ifdef CONFIG_ESP_CAPTURE_ENABLE_AUDIO
    if (capture->audio_pipe_builder) {
        esp_capture_destroy_pipeline(capture->audio_pipe_builder);
        capture->audio_pipe_builder = NULL;
    }
#endif  /* CONFIG_ESP_CAPTURE_ENABLE_AUDIO */
#ifdef CONFIG_ESP_CAPTURE_ENABLE_VIDEO
    if (capture->video_pipe_builder) {
        esp_capture_destroy_pipeline(capture->video_pipe_builder);
        capture->video_pipe_builder = NULL;
    }
#endif  /* CONFIG_ESP_CAPTURE_ENABLE_VIDEO */
    // Close path for expert mode
    esp_capture_path_mngr_if_t *audio_path = &capture->cfg.audio_path->base;
    if (audio_path) {
        audio_path->close(audio_path);
        if (capture->expert_builder == false) {
            capture_free(capture->cfg.audio_path);
        }
        capture->cfg.audio_path = NULL;
    }
    esp_capture_path_mngr_if_t *video_path = &capture->cfg.video_path->base;
    if (video_path) {
        video_path->close(video_path);
        if (capture->expert_builder == false) {
            capture_free(capture->cfg.video_path);
        }
        capture->cfg.video_path = NULL;
    }

    // Free resources
    if (capture->api_lock) {
        capture_mutex_destroy(capture->api_lock);
        capture->api_lock = NULL;
    }
    if (capture->sync_handle) {
        esp_capture_sync_destroy(capture->sync_handle);
        capture->sync_handle = NULL;
    }
    capture_free(capture);
    return ESP_CAPTURE_ERR_OK;
}

void esp_capture_enable_perf_monitor(bool enable)
{
#ifdef CONFIG_ESP_CAPTURE_ENABLE_PERF_MON
    capture_perf_monitor_enable(enable);
#endif  /* CONFIG_ESP_CAPTURE_ENABLE_PERF_MON */
}

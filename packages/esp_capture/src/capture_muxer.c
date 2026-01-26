
/**
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_capture.h"
#include "data_queue.h"
#include "esp_muxer.h"
#include "msg_q.h"
#include "mp4_muxer.h"
#include "ts_muxer.h"
#include "capture_muxer.h"
#include "capture_path.h"
#include "capture_os.h"
#include "capture_utils.h"
#include "capture_perf_mon.h"
#include "esp_log.h"

#define TAG "CAPTURE_MUXER"

#define SLICE_DURATION           (300000)
#define WRITE_CACHE_SIZE         (16 * 1024)
#define MUXER_DEFAULT_POOL_SIZE  (100 * 1024)
#define MIN_AUDIO_FRAME_DURATION (10)
#define MIN_VIDEO_FRAME_DURATION (30)
#define EVENT_GROUP_MUXER_EXITED (4)
#define MUXER_DEFAULT_Q_NUM      (10)
#define INVALID_MUXER_TYPE       (esp_muxer_type_t) - 1
#define DEFAULT_CACHE_FRAME_NUM  (3)
#define MAX(a, b)                ((a) > (b) ? (a) : (b))

// Here hacking to use stream type to indicate start/stop command
#define START_CMD_STREAM_TYPE (esp_capture_stream_type_t)0x10
#define STOP_CMD_STREAM_TYPE  (esp_capture_stream_type_t)0x11

typedef struct capture_muxer_path_t capture_muxer_path_t;

struct capture_muxer_path_t {
    // Configuration
    esp_capture_muxer_cfg_t     muxer_cfg;
    capture_path_handle_t       path;
    uint32_t                    muxer_cache_size;
    bool                        enable_streaming;
    // Status
    bool                        enabled;
    bool                        started;
    bool                        prepared;
    bool                        muxing;
    bool                        muxer_frame_reached;
    uint32_t                    muxer_cur_pts;
    int                         audio_stream_idx;
    int                         video_stream_idx;
    // Resources
    capture_event_grp_handle_t  event_grp;
    msg_q_handle_t              muxer_q;
    esp_muxer_handle_t          muxer;
    data_q_t                   *muxer_data_q;
};

static esp_muxer_audio_codec_t get_muxer_acodec(esp_capture_format_id_t codec_type)
{
    switch (codec_type) {
        case ESP_CAPTURE_FMT_ID_AAC:
            return ESP_MUXER_ADEC_AAC;
        case ESP_CAPTURE_FMT_ID_G711A:
            return ESP_MUXER_ADEC_G711_A;
        case ESP_CAPTURE_FMT_ID_G711U:
            return ESP_MUXER_ADEC_G711_U;
        case ESP_CAPTURE_FMT_ID_OPUS:
            return ESP_MUXER_ADEC_OPUS;
        case ESP_CAPTURE_FMT_ID_PCM:
            return ESP_MUXER_ADEC_PCM;
        default:
            return ESP_MUXER_ADEC_NONE;
    }
}

static esp_muxer_video_codec_t get_muxer_vcodec(esp_capture_format_id_t codec_type)
{
    switch (codec_type) {
        case ESP_CAPTURE_FMT_ID_H264:
            return ESP_MUXER_VDEC_H264;
        case ESP_CAPTURE_FMT_ID_MJPEG:
            return ESP_MUXER_VDEC_MJPEG;
        default:
            return ESP_MUXER_VDEC_NONE;
    }
}

static uint32_t calc_muxer_cache_size(const esp_capture_sink_cfg_t *sink_cfg)
{
    uint32_t cache_size = 0;
    if (sink_cfg->audio_info.format_id) {
        // For audio default use 8K cache
        cache_size += 20 * 1024;
    }
    switch (sink_cfg->video_info.format_id) {
        case ESP_CAPTURE_FMT_ID_H264: {
            uint32_t frame_size[] = {20, 40, 100};  // Mapped to 640, 1280, 1920
            int frame_idx = MAX(sink_cfg->video_info.width, sink_cfg->video_info.height) / 640;
            if (frame_idx >= sizeof(frame_size) / sizeof(frame_size[0])) {
                frame_idx = sizeof(frame_size) / sizeof(frame_size[0]) - 1;
            }
            cache_size += frame_size[frame_idx] * DEFAULT_CACHE_FRAME_NUM * 1024;
            break;
        }
        case ESP_CAPTURE_FMT_ID_MJPEG: {
            uint32_t frame_size[] = {40, 100, 200};  // Mapped to 640, 1280, 1920
            int frame_idx = MAX(sink_cfg->video_info.width, sink_cfg->video_info.height) / 640;
            if (frame_idx >= sizeof(frame_size) / sizeof(frame_size[0])) {
                frame_idx = sizeof(frame_size) / sizeof(frame_size[0]) - 1;
            }
            cache_size += frame_size[frame_idx] * DEFAULT_CACHE_FRAME_NUM * 1024;
            break;
        }
        default:
            break;
    }
    return cache_size;
}

static int muxer_data_reached(esp_muxer_data_info_t *muxer_data, void *ctx)
{
    capture_muxer_path_t *path = (capture_muxer_path_t *)ctx;
    if (path->enable_streaming && muxer_data->size) {
        if (path->muxer_frame_reached == false) {
            path->muxer_frame_reached = true;
            CAPTURE_PERF_MON(capture_path_get_path_type(path->path), "Muxer Frame Reached", {});
        }
        // Add data to queue
        int size = sizeof(uint32_t) + muxer_data->size;
        void *data = data_q_get_buffer(path->muxer_data_q, size);
        if (data) {
            *(uint32_t *)data = path->muxer_cur_pts;
            memcpy(data + sizeof(uint32_t), muxer_data->data, muxer_data->size);
            data_q_send_buffer(path->muxer_data_q, size);
        }
    }
    return 0;
}

static bool muxer_support_streaming(esp_muxer_type_t muxer_type)
{
    if (muxer_type == ESP_MUXER_TYPE_TS || muxer_type == ESP_MUXER_TYPE_FLV) {
        return true;
    }
    return false;
}

esp_capture_err_t capture_muxer_open(capture_path_handle_t path, esp_capture_muxer_cfg_t *muxer_cfg,
                                     capture_muxer_path_handle_t *h)
{
    capture_muxer_path_t *muxer_path = (capture_muxer_path_t *)capture_calloc(1, sizeof(capture_muxer_path_t));
    if (muxer_path == NULL) {
        ESP_LOGE(TAG, "Failed to allocate muxer path");
        return ESP_CAPTURE_ERR_NO_MEM;
    }
    do {
        muxer_path->muxer_cfg = *muxer_cfg;
        // Copy base configuration
        muxer_path->muxer_cfg.base_config = (esp_muxer_config_t *)capture_calloc(1, muxer_cfg->cfg_size);
        if (muxer_path->muxer_cfg.base_config == NULL) {
            break;
        }
        memcpy(muxer_path->muxer_cfg.base_config, muxer_cfg->base_config, muxer_cfg->cfg_size);
        muxer_path->path = path;
        capture_event_group_create(&muxer_path->event_grp);
        if (muxer_path->event_grp == NULL) {
            break;
        }
        // Turn on streaming output in default
        muxer_path->enable_streaming = true;
        *h = muxer_path;
        return ESP_CAPTURE_ERR_OK;
    } while (0);
    capture_muxer_close(muxer_path);
    return ESP_CAPTURE_ERR_NO_MEM;
}

static void muxer_flush_msg(capture_muxer_path_t *muxer_path)
{
    esp_capture_stream_frame_t frame = {0};
    while (msg_q_recv(muxer_path->muxer_q, &frame, sizeof(frame), true) == 0) {
        if (frame.data != NULL && frame.size > 0) {
            capture_path_release_share(muxer_path->path, &frame);
        }
    }
}

static void muxer_thread(void *arg)
{
    capture_muxer_path_t *muxer_path = (capture_muxer_path_t *)arg;
    esp_capture_stream_frame_t frame = {0};
    ESP_LOGI(TAG, "Enter muxer thread muxing %d", muxer_path->muxing);
    CAPTURE_PERF_MON(capture_path_get_path_type(muxer_path->path), "Muxer Thread Enter", {});
    muxer_path->muxer_frame_reached = false;

    while (muxer_path->muxing) {
        int ret = msg_q_recv(muxer_path->muxer_q, &frame, sizeof(frame), false);
        if (ret != 0) {
            ESP_LOGI(TAG, "Quit muxer for recv ret %d", ret);
            break;
        }
        if (frame.stream_type == STOP_CMD_STREAM_TYPE) {
            ESP_LOGI(TAG, "Muxer receive stop");
            break;
        }
        if (frame.data == NULL || frame.size == 0) {
            ESP_LOGW(TAG, "Receive empty frame");
            continue;
        }
        switch (frame.stream_type) {
            case ESP_CAPTURE_STREAM_TYPE_AUDIO: {
                esp_muxer_audio_packet_t audio_packet = {
                    .pts = frame.pts,
                    .data = frame.data,
                    .len = frame.size,
                };
                muxer_path->muxer_cur_pts = frame.pts;
                ret = esp_muxer_add_audio_packet(muxer_path->muxer, muxer_path->audio_stream_idx, &audio_packet);
                capture_path_release_share(muxer_path->path, &frame);
            } break;
            case ESP_CAPTURE_STREAM_TYPE_VIDEO: {
                esp_muxer_video_packet_t video_packet = {
                    .pts = frame.pts,
                    .data = frame.data,
                    .len = frame.size,
                };
                muxer_path->muxer_cur_pts = frame.pts;
                ret = esp_muxer_add_video_packet(muxer_path->muxer, muxer_path->video_stream_idx, &video_packet);
                capture_path_release_share(muxer_path->path, &frame);
            } break;
            default:
                break;
        }
    }
    CAPTURE_PERF_MON(capture_path_get_path_type(muxer_path->path), "Muxer Thread Leave", {});
    ESP_LOGI(TAG, "Leave muxer thread");
    capture_event_group_set_bits(muxer_path->event_grp, EVENT_GROUP_MUXER_EXITED);
    capture_thread_destroy(NULL);
}

static esp_capture_err_t open_muxer(capture_muxer_path_t *muxer_path)
{
    if (muxer_path->muxer) {
        return ESP_CAPTURE_ERR_OK;
    }
    esp_muxer_config_t *cfg = muxer_path->muxer_cfg.base_config;
    // Use default slice size if not set
    cfg->slice_duration = cfg->slice_duration ? cfg->slice_duration : SLICE_DURATION;
    cfg->ctx = muxer_path;
    // Use default cache size if not set (let user controlled)
    // cfg->ram_cache_size = cfg->ram_cache_size ? cfg->ram_cache_size : WRITE_CACHE_SIZE;
    // Clear data callback
    cfg->data_cb = NULL;
    if (muxer_path->enable_streaming) {
        if (muxer_support_streaming(cfg->muxer_type)) {
            cfg->data_cb = muxer_data_reached;
            cfg->ctx = muxer_path;
        } else {
            ESP_LOGW(TAG, "Muxer type %d does not support streaming", cfg->muxer_type);
            muxer_path->enable_streaming = false;
        }
    }
    muxer_path->muxer = esp_muxer_open(cfg, muxer_path->muxer_cfg.cfg_size);
    if (muxer_path->muxer == NULL) {
        ESP_LOGE(TAG, "Fail to open muxer");
        cfg->muxer_type = INVALID_MUXER_TYPE;
        return ESP_CAPTURE_ERR_NO_RESOURCES;
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t prepare_muxer_stream(capture_muxer_path_t *muxer_path)
{
    const esp_capture_sink_cfg_t *sink_cfg = capture_path_get_sink_cfg(muxer_path->path);
    int ret = open_muxer(muxer_path);
    if (ret != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(TAG, "Fail to open muxer");
        return ret;
    }
    muxer_path->muxer_cache_size = calc_muxer_cache_size(sink_cfg);
    muxer_path->audio_stream_idx = -1;
    muxer_path->video_stream_idx = -1;
    esp_capture_muxer_mask_t muxer_mask = muxer_path->muxer_cfg.muxer_mask;
    if (sink_cfg->audio_info.format_id && (muxer_mask == ESP_CAPTURE_MUXER_MASK_ALL || muxer_mask == ESP_CAPTURE_MUXER_MASK_AUDIO)) {
        esp_muxer_audio_stream_info_t audio_info = {
            .codec = get_muxer_acodec(sink_cfg->audio_info.format_id),
            .sample_rate = sink_cfg->audio_info.sample_rate,
            .bits_per_sample = sink_cfg->audio_info.bits_per_sample,
            .channel = sink_cfg->audio_info.channel,
            .min_packet_duration = MIN_AUDIO_FRAME_DURATION,
        };
        ret = esp_muxer_add_audio_stream(muxer_path->muxer, &audio_info, &muxer_path->audio_stream_idx);
        if (ret != ESP_MUXER_ERR_OK) {
            ESP_LOGE(TAG, "Fail to add audio stream for muxer ret:%d", ret);
        }
    }
    if (sink_cfg->video_info.format_id && (muxer_mask == ESP_CAPTURE_MUXER_MASK_ALL || muxer_mask == ESP_CAPTURE_MUXER_MASK_VIDEO)) {
        esp_muxer_video_stream_info_t video_info = {
            .codec = get_muxer_vcodec(sink_cfg->video_info.format_id),
            .fps = sink_cfg->video_info.fps,
            .width = sink_cfg->video_info.width,
            .height = sink_cfg->video_info.height,
            .min_packet_duration = MIN_VIDEO_FRAME_DURATION,
        };
        ret = esp_muxer_add_video_stream(muxer_path->muxer, &video_info, &muxer_path->video_stream_idx);
        if (ret != ESP_MUXER_ERR_OK) {
            ESP_LOGE(TAG, "Fail to add video stream for muxer ret:%d", ret);
        }
    }
    // We allow one type of stream add fail
    ret = (muxer_path->audio_stream_idx >= 0 || muxer_path->video_stream_idx >= 0) ? ESP_CAPTURE_ERR_OK : ESP_CAPTURE_ERR_NOT_SUPPORTED;
    return ret;
}

esp_capture_err_t capture_muxer_prepare(capture_muxer_path_handle_t muxer_path)
{
    // Already prepared
    if (muxer_path->prepared) {
        return ESP_CAPTURE_ERR_OK;
    }
    esp_capture_err_t ret = prepare_muxer_stream(muxer_path);
    if (ret != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(TAG, "Fail to prepare muxer ret:%d", ret);
        return ret;
    }
    // Allocate queue to receive audio and video data
    muxer_path->muxer_q = msg_q_create(MUXER_DEFAULT_Q_NUM, sizeof(esp_capture_stream_frame_t));
    if (muxer_path->muxer_q == NULL) {
        ESP_LOGE(TAG, "Failed to create muxer q");
        return ESP_CAPTURE_ERR_NO_MEM;
    }
    // Create muxer output queue if user want to fetch muxer data also
    if (muxer_path->enable_streaming && (muxer_path->muxer_cache_size > 0)) {
        if (muxer_path->muxer_data_q == NULL) {
            muxer_path->muxer_data_q = data_q_init(muxer_path->muxer_cache_size);
            if (muxer_path->muxer_data_q == NULL) {
                ESP_LOGE(TAG, "Fail to create output queue for muxer");
                return ESP_CAPTURE_ERR_NO_MEM;
            }
        }
    }
    muxer_path->prepared = true;
    return ret;
}

bool capture_muxer_stream_prepared(capture_muxer_path_handle_t muxer_path, esp_capture_stream_type_t stream_type)
{
    if (muxer_path->enabled == false || muxer_path->prepared == false) {
        return false;
    }
    if (stream_type == ESP_CAPTURE_STREAM_TYPE_VIDEO) {
        return (muxer_path->video_stream_idx >= 0);
    }
    if (stream_type == ESP_CAPTURE_STREAM_TYPE_AUDIO) {
        return (muxer_path->audio_stream_idx >= 0);
    }
    return false;
}

esp_capture_err_t capture_muxer_start(capture_muxer_path_handle_t muxer_path)
{
    if (muxer_path->enabled == false || muxer_path->prepared == false) {
        return ESP_CAPTURE_ERR_OK;
    }
    if (muxer_path->started) {
        return ESP_CAPTURE_ERR_OK;
    }
    esp_capture_err_t ret = ESP_CAPTURE_ERR_OK;
    do {
        muxer_path->muxing = true;
        capture_thread_handle_t handle;
        ret = capture_thread_create_from_scheduler(&handle, "Muxer", muxer_thread, muxer_path);
        if (ret != 0) {
            ESP_LOGE(TAG, "Fail to create muxer thread");
            muxer_path->enabled = false;
            muxer_path->muxing = false;
            ret = ESP_CAPTURE_ERR_NO_RESOURCES;
            break;
        }
    } while (0);
    if (ret != ESP_CAPTURE_ERR_OK) {
        return ret;
    }
    muxer_path->started = true;
    return ret;
}

msg_q_handle_t capture_muxer_get_muxer_q(capture_muxer_path_handle_t muxer_path)
{
    return muxer_path->muxer_q;
}

esp_capture_err_t capture_muxer_enable(capture_muxer_path_handle_t muxer_path, bool enable)
{
    if (muxer_path->enabled == enable) {
        return ESP_CAPTURE_ERR_OK;
    }
    muxer_path->enabled = enable;
    esp_capture_err_t ret;
    if (enable) {
        ret = capture_muxer_start(muxer_path);
    } else {
        ret = capture_muxer_stop(muxer_path);
    }
    return ret;
}

esp_capture_err_t capture_muxer_disable_streaming(capture_muxer_path_handle_t muxer_path)
{
    muxer_path->enable_streaming = false;
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t capture_muxer_acquire_frame(capture_muxer_path_handle_t muxer_path, esp_capture_stream_frame_t *frame, bool no_wait)
{
    esp_capture_err_t ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
    if (muxer_path->started == false || muxer_path->muxer_data_q == NULL) {
        return ret;
    }
    int size = 0;
    void *data = NULL;
    if (no_wait == false) {
        data_q_read_lock(muxer_path->muxer_data_q, &data, &size);
    } else if (data_q_have_data(muxer_path->muxer_data_q)) {
        data_q_read_lock(muxer_path->muxer_data_q, &data, &size);
    }
    frame->size = 0;
    if (data) {
        frame->pts = *(uint32_t *)data;
        frame->data = (uint8_t *)data + sizeof(uint32_t);
        frame->size = size - sizeof(uint32_t);
        return ESP_CAPTURE_ERR_OK;
    }
    return ESP_CAPTURE_ERR_NOT_FOUND;
}

esp_capture_err_t capture_muxer_release_frame(capture_muxer_path_handle_t muxer_path, esp_capture_stream_frame_t *frame)
{
    int ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
    if (muxer_path->started == false || muxer_path->muxer_data_q == NULL) {
        return ret;
    }
    data_q_read_unlock(muxer_path->muxer_data_q);
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t capture_muxer_stop(capture_muxer_path_handle_t muxer_path)
{
    // Wait for thread to quit
    if (muxer_path->muxing) {
        if (muxer_path->muxer_data_q) {
            data_q_consume_all(muxer_path->muxer_data_q);
        }
        esp_capture_stream_frame_t frame = {0};
        frame.stream_type = STOP_CMD_STREAM_TYPE;
        msg_q_send(muxer_path->muxer_q, &frame, sizeof(frame));
        capture_event_group_wait_bits(muxer_path->event_grp, EVENT_GROUP_MUXER_EXITED, 1000);
        capture_event_group_clr_bits(muxer_path->event_grp, EVENT_GROUP_MUXER_EXITED);
        // Receive all pending messages
        muxer_flush_msg(muxer_path);
        muxer_path->muxing = false;
    }
    // Close muxer
    if (muxer_path->muxer) {
        esp_muxer_close(muxer_path->muxer);
        muxer_path->muxer = NULL;
    }
    muxer_path->started = false;
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t capture_muxer_close(capture_muxer_path_handle_t muxer_path)
{
    muxer_path->started = false;
    capture_muxer_stop(muxer_path);
    if (muxer_path->muxer_q) {
        msg_q_destroy(muxer_path->muxer_q);
        muxer_path->muxer_q = NULL;
    }
    // Start to destroy queue
    if (muxer_path->muxer_data_q) {
        data_q_deinit(muxer_path->muxer_data_q);
        muxer_path->muxer_data_q = NULL;
    }
    if (muxer_path->event_grp) {
        capture_event_group_destroy(muxer_path->event_grp);
        muxer_path->event_grp = NULL;
    }
    CAPTURE_SAFE_FREE(muxer_path->muxer_cfg.base_config);
    capture_free(muxer_path);
    return ESP_CAPTURE_ERR_OK;
}

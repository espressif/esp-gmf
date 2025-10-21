/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "gmf_capture_path_mngr.h"
#include "capture_pipeline_utils.h"
#include "esp_gmf_pipeline.h"
#include "capture_os.h"
#include "capture_utils.h"
#include "capture_perf_mon.h"

#include "esp_log.h"

#define TAG "GMF_PATH_MNGR"

#define CAPTURE_GMF_TASK_DEFAULT_PRIORITY   10
#define CAPTURE_GMF_TASK_DEFAULT_STACK_SIZE (4 * 1024)

static esp_capture_err_t get_pipelines(gmf_capture_path_mngr_t *mngr)
{
    // Get pipeline number firstly
    esp_capture_pipeline_builder_if_t *builder = mngr->pipeline_builder;
    uint8_t pipeline_num = 0;
    builder->get_pipelines(builder, NULL, &pipeline_num);
    int ret = ESP_CAPTURE_ERR_OK;
    do {
        if (pipeline_num == 0) {
            ESP_LOGE(TAG, "No pipeline");
            ret = ESP_CAPTURE_ERR_INVALID_ARG;
            break;
        }
        mngr->pipeline = (esp_capture_gmf_pipeline_t *)capture_calloc(1, pipeline_num * sizeof(esp_capture_gmf_pipeline_t));
        if (mngr->pipeline == NULL) {
            ret = ESP_CAPTURE_ERR_NO_MEM;
            break;
        }
        builder->get_pipelines(builder, mngr->pipeline, &pipeline_num);
        mngr->task = (esp_gmf_task_handle_t *)capture_calloc(1, pipeline_num * sizeof(esp_gmf_task_handle_t));
        if (mngr->task == NULL) {
            ESP_LOGE(TAG, "Fail to alloc audio path res");
            ret = ESP_CAPTURE_ERR_NO_MEM;
            break;
        }
        mngr->pipeline_ref = (gmf_capture_pipeline_ref_t *)capture_calloc(1, pipeline_num * sizeof(gmf_capture_pipeline_ref_t));
        if (mngr->pipeline_ref == NULL) {
            ESP_LOGE(TAG, "Fail to alloc pipeline ref");
            ret = ESP_CAPTURE_ERR_NO_MEM;
            break;
        }
        for (int i = 0; i < pipeline_num; i++) {
            mngr->pipeline_ref[i].pipeline = &mngr->pipeline[i];
            mngr->pipeline_ref[i].parent = mngr;
        }
        mngr->run_mask = (uint8_t *)capture_calloc(1, pipeline_num * sizeof(uint8_t));
        if (mngr->run_mask == NULL) {
            ret = ESP_CAPTURE_ERR_NO_MEM;
            break;
        }
        mngr->pipeline_num = pipeline_num;
        // Notify for pipeline built before actual run
        if (mngr->cfg.event_cb) {
            esp_capture_path_event_type_t built_event = (mngr->stream_type) == ESP_CAPTURE_STREAM_TYPE_AUDIO ? ESP_CAPTURE_PATH_EVENT_AUDIO_PIPELINE_BUILT : ESP_CAPTURE_PATH_EVENT_VIDEO_PIPELINE_BUILT;
            mngr->cfg.event_cb(mngr->cfg.src_ctx, 0, built_event);
        }
    } while (0);
    return ret;
}

static bool have_valid_path(gmf_capture_path_mngr_t *mngr)
{
    for (int i = 0; i < mngr->path_num; i++) {
        gmf_capture_path_res_t *res = gmf_capture_path_mngr_get_idx(mngr, i);
        if (res->configured) {
            return true;
        }
    }
    return false;
}

static esp_capture_err_t prepare_for_all_path(gmf_capture_path_mngr_t *mngr, gmf_capture_prepare_all_path_cb prepare_all)
{
    if (have_valid_path(mngr) == false) {
        return ESP_CAPTURE_ERR_OK;
    }
    // Audio source is open in audio source element
    int ret = ESP_CAPTURE_ERR_OK;
    if (mngr->pipeline_builder->negotiate) {
        CAPTURE_PERF_MON(0, (mngr->stream_type == ESP_CAPTURE_STREAM_TYPE_AUDIO) ? "Negotiate All Audio Sink" : "Negotiate All Video Sink", {
            ret = mngr->pipeline_builder->negotiate(mngr->pipeline_builder, ESP_CAPTURE_PIPELINE_NEGO_ALL_MASK);
        });
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to negotiate all path");
            return ret;
        }
    }
    for (int i = 0; i < mngr->path_num; i++) {
        gmf_capture_path_res_t *res = gmf_capture_path_mngr_get_idx(mngr, i);
        if (res->configured) {
            res->negotiated = true;
        }
    }
    if (prepare_all) {
        CAPTURE_PERF_MON(0, (mngr->stream_type == ESP_CAPTURE_STREAM_TYPE_AUDIO) ? "Prepare All Audio Pipe" : "Prepare All Video Pipe", {
            ret = prepare_all(mngr);
        });
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to prepare all pipeline");
            return ret;
        }
    }
    return ret;
}

static esp_capture_path_event_type_t map_pipeline_event_type(gmf_capture_path_mngr_t *mngr, int event)
{
    if (mngr->stream_type == ESP_CAPTURE_STREAM_TYPE_AUDIO) {
        return (event == ESP_GMF_EVENT_STATE_ERROR) ?
                ESP_CAPTURE_PATH_EVENT_AUDIO_ERROR : ESP_CAPTURE_PATH_EVENT_AUDIO_FINISHED;
    }
    if (mngr->stream_type == ESP_CAPTURE_STREAM_TYPE_VIDEO) {
        return (event == ESP_GMF_EVENT_STATE_ERROR) ?
                ESP_CAPTURE_PATH_EVENT_VIDEO_ERROR : ESP_CAPTURE_PATH_EVENT_VIDEO_FINISHED;
    }
    return ESP_CAPTURE_PATH_EVENT_TYPE_NONE;
}

static esp_gmf_err_t pipeline_event_hdlr(esp_gmf_event_pkt_t *pkt, void *ctx)
{
    if (pkt == NULL || pkt->type != ESP_GMF_EVT_TYPE_CHANGE_STATE) {
        return ESP_GMF_ERR_OK;
    }
    gmf_capture_pipeline_ref_t *pipeline_ref = (gmf_capture_pipeline_ref_t *)ctx;
    if (pipeline_ref == NULL || pipeline_ref->parent == NULL) {
        return ESP_GMF_ERR_NOT_FOUND;
    }
    gmf_capture_path_mngr_t *mngr = pipeline_ref->parent;
    int pipe_event = pkt->sub;
    // Handle pipeline error events
    if ((pipe_event == ESP_GMF_EVENT_STATE_STOPPED) || (pipe_event == ESP_GMF_EVENT_STATE_FINISHED) ||
        (pipe_event == ESP_GMF_EVENT_STATE_ERROR)) {
        // Notify error for path
        for (int i = 0; i < mngr->path_num; i++) {
            gmf_capture_path_res_t *res = gmf_capture_path_mngr_get_idx(pipeline_ref->parent, i);
            if (pipeline_ref->pipeline->path_mask & (1 << res->path)) {
                esp_capture_path_event_type_t event = map_pipeline_event_type(mngr, pipe_event);
                if (event) {
                    mngr->cfg.event_cb(mngr->cfg.src_ctx, res->path, event);
                }
            }
        }
    }
    return ESP_GMF_ERR_OK;
}

static esp_capture_err_t prepare_pipeline(gmf_capture_path_mngr_t *mngr, uint8_t path, gmf_capture_prepare_path_cb prepare_cb)
{
    gmf_capture_path_res_t *res = gmf_capture_path_mngr_get_path(mngr, path);
    uint8_t path_mask = (1 << path);
    if (res->negotiated == false) {
        bool for_all = true;
        for (int i = 0; i < mngr->path_num; i++) {
            gmf_capture_path_res_t *each = gmf_capture_path_mngr_get_path(mngr, i);
            if (each->negotiated) {
                for_all = false;
                break;
            }
        }
        if (mngr->pipeline_builder->negotiate) {
            int ret = ESP_CAPTURE_ERR_OK;
            uint8_t nego_mask = for_all ? ESP_CAPTURE_PIPELINE_NEGO_ALL_MASK : path_mask;
            CAPTURE_PERF_MON(path, (mngr->stream_type == ESP_CAPTURE_STREAM_TYPE_AUDIO) ? "Negotiate Audio Sink" : "Negotiate Video Sink", {
                ret = mngr->pipeline_builder->negotiate(mngr->pipeline_builder, nego_mask);
            });
            if (ret != ESP_CAPTURE_ERR_OK) {
                ESP_LOGE(TAG, "Fail to negotiate audio pipeline");
                return ret;
            }
            if (res->configured) {
                res->negotiated = true;
            }
        }
    }
    for (int i = 0; i < mngr->pipeline_num; i++) {
        if ((mngr->pipeline[i].path_mask & path_mask) == 0) {
            continue;
        }
        // Task already created just add reference count
        if (mngr->task[i]) {
            continue;
        }
        esp_gmf_pipeline_handle_t pipeline = mngr->pipeline[i].pipeline;
        if (capture_pipeline_is_sink(pipeline)) {
            int ret;
            CAPTURE_PERF_MON(path, (mngr->stream_type == ESP_CAPTURE_STREAM_TYPE_AUDIO) ? "Prepare Audio Sink" : "Prepare Video Sink", {
                ret = prepare_cb(res);
            });
            if (ret != ESP_CAPTURE_ERR_OK) {
                return ret;
            }
        }
        esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
        esp_capture_thread_scheduler_cb_t scheduler = capture_thread_get_scheduler();
        cfg.thread.stack_in_ext = true;
        if (scheduler && mngr->pipeline[i].name) {
            esp_capture_thread_schedule_cfg_t scheduler_cfg = {
                .priority = CAPTURE_GMF_TASK_DEFAULT_PRIORITY,
                .stack_size = CAPTURE_GMF_TASK_DEFAULT_STACK_SIZE,
            };
            scheduler_cfg.stack_in_ext = true;
            scheduler(mngr->pipeline[i].name, &scheduler_cfg);
            cfg.thread.core = scheduler_cfg.core_id;
            cfg.thread.prio = scheduler_cfg.priority;
            cfg.thread.stack = scheduler_cfg.stack_size;
            cfg.thread.stack_in_ext = scheduler_cfg.stack_in_ext;
        }
        cfg.name = mngr->pipeline[i].name;
        CAPTURE_PERF_MON(path, (mngr->stream_type == ESP_CAPTURE_STREAM_TYPE_AUDIO) ? "Create Audio Task" : "Create Video Task", {
            esp_gmf_task_init(&cfg, &mngr->task[i]);
        });
        if (mngr->task[i] == NULL) {
            ESP_LOGE(TAG, "Fail to create task %s", CAPTURE_SAFE_STR(mngr->pipeline[i].name));
            return ESP_CAPTURE_ERR_NOT_SUPPORTED;
        }
        esp_gmf_pipeline_bind_task(pipeline, mngr->task[i]);
        esp_gmf_pipeline_loading_jobs(pipeline);
        // Handle pipeline event
        esp_gmf_pipeline_set_event(pipeline, pipeline_event_hdlr, &mngr->pipeline_ref[i]);
        esp_gmf_pipeline_prev_run(pipeline);
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t run_pipelines(gmf_capture_path_mngr_t *mngr, uint8_t path)
{
    uint8_t path_mask = (1 << path);
    for (int i = 0; i < mngr->pipeline_num; i++) {
        if ((mngr->pipeline[i].path_mask & path_mask) == 0) {
            continue;
        }
        if (mngr->task[i] == NULL) {
            continue;
        }
        // Already running just add this path mask
        if (mngr->run_mask[i]) {
            mngr->run_mask[i] |= path_mask;
            continue;
        }
        esp_gmf_pipeline_handle_t pipeline = mngr->pipeline[i].pipeline;
        CAPTURE_PERF_MON(path, (mngr->stream_type == ESP_CAPTURE_STREAM_TYPE_AUDIO) ? "Run Audio Task" : "Run Video Task", {
            esp_gmf_pipeline_run(pipeline);
        });
        mngr->run_mask[i] |= path_mask;
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t release_pipelines(gmf_capture_path_mngr_t *mngr, uint8_t path, gmf_capture_release_path_cb release_cb)
{
    gmf_capture_path_res_t *res = gmf_capture_path_mngr_get_path(mngr, path);
    uint8_t path_mask = (1 << res->path);
    for (int i = 0; i < mngr->pipeline_num; i++) {
        esp_capture_gmf_pipeline_t *pipeline = &mngr->pipeline[i];
        if ((pipeline->path_mask & path_mask) == 0) {
            continue;
        }
        // Task already Stopped
        if (mngr->task[i] == NULL) {
            continue;
        }
        if (mngr->run_mask[i]) {
            // Shared pipeline by other path
            continue;
        }
        esp_gmf_pipeline_bind_task(pipeline->pipeline, NULL);
        esp_gmf_task_deinit(mngr->task[i]);
        mngr->task[i] = NULL;
    }

    if (release_cb) {
        release_cb(res);
    }
    res->negotiated = false;
    return ESP_CAPTURE_ERR_OK;
}

static void stop_pipelines(gmf_capture_path_mngr_t *mngr, uint8_t path, gmf_capture_stop_path_cb stop_cb)
{
    gmf_capture_path_res_t *res = gmf_capture_path_mngr_get_path(mngr, path);
    uint8_t path_mask = (1 << res->path);

    // Stop from sink to source
    for (int i = 0; i < mngr->pipeline_num; i++) {
        int sel_pipe = mngr->pipeline_num - 1 - i;
        esp_capture_gmf_pipeline_t *pipeline = &mngr->pipeline[sel_pipe];
        if ((pipeline->path_mask & path_mask) == 0) {
            continue;
        }
        // Task already Stopped
        if (mngr->task[sel_pipe] == NULL) {
            continue;
        }
        if (mngr->run_mask[sel_pipe] & (~path_mask)) {
            // Shared pipeline by other path
            ESP_LOGD(TAG, "Pipeline still being used by others %x", mngr->run_mask[sel_pipe]);
            mngr->run_mask[sel_pipe] &= (~path_mask);
            continue;
        }
        if (capture_pipeline_is_sink(pipeline->pipeline)) {
            CAPTURE_PERF_MON(res->path, (mngr->stream_type == ESP_CAPTURE_STREAM_TYPE_AUDIO) ? "Prepare Stop Audio Sink" : "Prepare Stop Video Sink", {
                stop_cb(res);
            });
        } else {
            // Src is stopped all related sink need re-negotiate
            for (int i = 0; i < mngr->path_num; i++) {
                gmf_capture_path_res_t *each = gmf_capture_path_mngr_get_idx(mngr, i);
                each->negotiated = false;
            }
        }
        ESP_LOGD(TAG, "Start to stop pipeline %d", sel_pipe);
        CAPTURE_PERF_MON(res->path, (mngr->stream_type == ESP_CAPTURE_STREAM_TYPE_AUDIO) ? "Stop Audio Pipeline" : "Stop Video Pipeline", {
            esp_gmf_pipeline_stop(pipeline->pipeline);
        });
        ESP_LOGD(TAG, "End to stop pipeline %d", sel_pipe);
        esp_gmf_pipeline_reset(pipeline->pipeline);
        mngr->run_mask[sel_pipe] &= (~path_mask);
    }
}

static esp_capture_err_t start_path(gmf_capture_path_mngr_t *mngr, uint8_t path, gmf_capture_prepare_path_cb prepare_cb)
{
    gmf_capture_path_res_t *res = gmf_capture_path_mngr_get_path(mngr, path);
    if (res->configured == false) {
        return ESP_CAPTURE_ERR_OK;
    }
    if (res->started) {
        return ESP_CAPTURE_ERR_OK;
    }
    int ret = ESP_CAPTURE_ERR_OK;
    do {
        if (capture_pipeline_verify(mngr->pipeline, mngr->pipeline_num, path) == false) {
            ret = ESP_CAPTURE_ERR_NOT_FOUND;
            ESP_LOGE(TAG, "Fail to verify pipeline");
            break;
        }
        ret = prepare_pipeline(mngr, path, prepare_cb);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to prepare pipeline");
            break;
        }
        ret = run_pipelines(mngr, path);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to run pipeline");
            break;
        }
        res->started = true;
        return ESP_CAPTURE_ERR_OK;
    } while (0);
    // Report error event
    esp_capture_path_event_type_t err_event = (mngr->stream_type) == ESP_CAPTURE_STREAM_TYPE_AUDIO ?
                                              ESP_CAPTURE_PATH_EVENT_AUDIO_ERROR : ESP_CAPTURE_PATH_EVENT_VIDEO_ERROR;
    mngr->cfg.event_cb(mngr->cfg.src_ctx, path, err_event);
    return ret;
}

static esp_capture_err_t stop_path(gmf_capture_path_mngr_t *mngr, uint8_t path, gmf_capture_stop_path_cb stop_cb,
                                   gmf_capture_release_path_cb release_cb)
{
    gmf_capture_path_res_t *res = gmf_capture_path_mngr_get_path(mngr, path);
    if (res->started == false) {
        return ESP_CAPTURE_ERR_OK;
    }
    res->started = false;
    res->negotiated = false;
    stop_pipelines(mngr, path, stop_cb);
    release_pipelines(mngr, path, release_cb);
    ESP_LOGD(TAG, "Path %d stop finished\n", path);
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t start_all_path(gmf_capture_path_mngr_t *mngr, gmf_capture_prepare_all_path_cb prepare_all,
                                        gmf_capture_prepare_path_cb prepare_cb)
{
    int ret = ESP_CAPTURE_ERR_OK;
    // Only get pipeline when all sinks are set
    if (get_pipelines(mngr) != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(TAG, "Fail to get pipelines for %d", mngr->stream_type);
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    ret = prepare_for_all_path(mngr, prepare_all);
    if (ret != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(TAG, "Fail to prepare for all path");
        return ret;
    }
    for (int i = 0; i < mngr->path_num; i++) {
        gmf_capture_path_res_t *res = gmf_capture_path_mngr_get_idx(mngr, i);
        if (res->enable) {
            ret = start_path(mngr, res->path, prepare_cb);
            if (ret != ESP_CAPTURE_ERR_OK) {
                ESP_LOGE(TAG, "Fail to start path %d", i);
                break;
            }
        }
    }
    return ret;
}

static esp_capture_err_t stop_all_path(gmf_capture_path_mngr_t *mngr, gmf_capture_stop_path_cb stop_cb,
                                       gmf_capture_release_path_cb release_cb)
{
    int ret = ESP_CAPTURE_ERR_OK;
    for (int i = 0; i < mngr->path_num; i++) {
        gmf_capture_path_res_t *res = gmf_capture_path_mngr_get_idx(mngr, i);
        ESP_LOGD(TAG, "Start to stop path %d", i);
        ret = stop_path(mngr, res->path, stop_cb, release_cb);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to stop path %d", i);
        }
    }
    SAFE_FREE(mngr->run_mask);
    SAFE_FREE(mngr->task);
    SAFE_FREE(mngr->pipeline_ref);
    SAFE_FREE(mngr->pipeline);
    mngr->pipeline_num = 0;
    if (mngr->pipeline_builder->release_pipelines) {
        mngr->pipeline_builder->release_pipelines(mngr->pipeline_builder);
    }
    return ret;
}

esp_capture_err_t gmf_capture_path_mngr_open(gmf_capture_path_mngr_t *mngr, esp_capture_stream_type_t stream_type,
                                             esp_capture_path_cfg_t *cfg, int res_size)
{
    if (cfg == NULL || cfg->frame_avail == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    mngr->stream_type = stream_type;
    mngr->cfg = *cfg;
    mngr->res_size = res_size;
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t gmf_capture_path_mngr_add_path(gmf_capture_path_mngr_t *mngr, uint8_t path, esp_capture_stream_info_t *sink)
{
    if (mngr->stream_type == ESP_CAPTURE_STREAM_TYPE_AUDIO && sink->audio_info.format_id == ESP_CAPTURE_FMT_ID_NONE) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    if (mngr->stream_type == ESP_CAPTURE_STREAM_TYPE_VIDEO && sink->video_info.format_id == ESP_CAPTURE_FMT_ID_NONE) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    gmf_capture_path_res_t *res = gmf_capture_path_mngr_get_path(mngr, path);
    if (res == NULL) {
        int size = (mngr->path_num + 1) * mngr->res_size;
        uint8_t *new_res = (uint8_t *)capture_realloc(mngr->res, size);
        if (new_res == NULL) {
            ESP_LOGE(TAG, "No memory for new path %d", path);
            return ESP_CAPTURE_ERR_NO_MEM;
        }
        mngr->res = (gmf_capture_path_res_t *)new_res;
        // Set basic information for path
        uint8_t *addr = (uint8_t *)mngr->res;
        res = (gmf_capture_path_res_t *)(addr + mngr->path_num * mngr->res_size);
        memset(res, 0, mngr->res_size);
        res->path = path;
        res->parent = mngr;
        mngr->path_num++;
    }
    res->configured = true;
    esp_capture_pipeline_builder_if_t *builder = mngr->pipeline_builder;
    builder->set_sink_cfg(builder, path, sink);
    return ESP_CAPTURE_ERR_OK;
};

gmf_capture_path_res_t *gmf_capture_path_mngr_get_path(gmf_capture_path_mngr_t *mngr, uint8_t path)
{
    for (int i = 0; i < mngr->path_num; i++) {
        uint8_t *addr = (uint8_t *)mngr->res;
        gmf_capture_path_res_t *res = (gmf_capture_path_res_t *)(addr + i * mngr->res_size);
        if (res->path == path) {
            return res;
        }
    }
    return NULL;
}

gmf_capture_path_res_t *gmf_capture_path_mngr_get_idx(gmf_capture_path_mngr_t *mngr, uint8_t idx)
{
    if (idx < mngr->path_num) {
        uint8_t *addr = (uint8_t *)mngr->res;
        return (gmf_capture_path_res_t *)(addr + idx * mngr->res_size);
    }
    return NULL;
}

esp_capture_err_t gmf_capture_path_mngr_enable_path(gmf_capture_path_mngr_t *mngr,
                                                    uint8_t path, bool enable,
                                                    gmf_capture_prepare_path_cb prepare_cb,
                                                    gmf_capture_stop_path_cb stop_cb,
                                                    gmf_capture_release_path_cb release_cb)
{
    gmf_capture_path_res_t *res = gmf_capture_path_mngr_get_path(mngr, path);
    if (res == NULL) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    // Already enabled
    if (res->enable == enable) {
        return ESP_CAPTURE_ERR_OK;
    }
    res->enable = enable;
    if (mngr->started == false) {
        return ESP_CAPTURE_ERR_OK;
    }
    int ret = 0;
    if (enable) {
        ret = start_path(mngr, path, prepare_cb);
    } else {
        ret = stop_path(mngr, path, stop_cb, release_cb);
    }
    return ret;
}

esp_capture_err_t gmf_capture_path_mngr_start(gmf_capture_path_mngr_t *mngr, gmf_capture_prepare_all_path_cb prepare_all,
                                              gmf_capture_prepare_path_cb prepare_cb)
{
    // Already started
    if (mngr->started == true) {
        return ESP_CAPTURE_ERR_OK;
    }
    mngr->started = true;
    // Skip for no valid path
    if (mngr->path_num == 0) {
        return ESP_CAPTURE_ERR_OK;
    }
    return start_all_path(mngr, prepare_all, prepare_cb);
}

esp_capture_err_t gmf_capture_path_mngr_frame_reached(gmf_capture_path_res_t *res, esp_capture_stream_frame_t *frame)
{
    gmf_capture_path_mngr_t *mngr = res->parent;
    if (mngr->cfg.frame_avail) {
        return mngr->cfg.frame_avail(mngr->cfg.src_ctx, res->path, frame);
    }
    return ESP_CAPTURE_ERR_NOT_SUPPORTED;
}

esp_capture_err_t gmf_capture_path_mngr_stop(gmf_capture_path_mngr_t *mngr, gmf_capture_stop_path_cb stop_cb,
                                             gmf_capture_release_path_cb release_cb)
{
    if (mngr->started == false) {
        return ESP_CAPTURE_ERR_OK;
    }
    mngr->started = false;
    // Skip for no valid path
    if (mngr->path_num == 0) {
        return ESP_CAPTURE_ERR_OK;
    }
    return stop_all_path(mngr, stop_cb, release_cb);
}

esp_capture_err_t gmf_capture_path_mngr_close(gmf_capture_path_mngr_t *mngr)
{
    SAFE_FREE(mngr->res);
    mngr->pipeline = 0;
    mngr->path_num = 0;
    return ESP_CAPTURE_ERR_OK;
}

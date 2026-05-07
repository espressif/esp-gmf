/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_video_render_worker.h"
#include "video_render_sys.h"
#include "video_render_utils.h"
#include "esp_gmf_oal_thread.h"
#include "data_queue.h"
#include "esp_log.h"

#define TAG  "VIDEO_RENDER_WORKER"

#define VIDEO_RENDER_WORKER_EXIT_BIT  (1 << 0)

typedef struct {
    void          *event_grp;
    volatile bool  is_running;
    bool           exit_on_error;
    uint8_t        align_size;
    data_q_t      *queue;
    void          *ctx;
    int (*worker)(esp_video_render_frame_t *frame, void *ctx);
} worker_inst_t;

static void worker_task(void *arg)
{
    worker_inst_t *inst = (worker_inst_t *)arg;
    while (inst->is_running) {
        esp_video_render_frame_t *frame = NULL;
        int size = 0;
        int ret = data_q_read_lock(inst->queue, (void **)&frame, &size);
        if (ret != 0) {
            break;
        }
        ret = inst->worker(frame, inst->ctx);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to worker frame ret %d", ret);
            if (inst->exit_on_error) {
                data_q_read_unlock(inst->queue);
                break;
            }
        }
        ret = data_q_read_unlock(inst->queue);
        if (ret != 0) {
            break;
        }
    }
    video_render_event_grp_set_bits(inst->event_grp, VIDEO_RENDER_WORKER_EXIT_BIT);
    esp_gmf_oal_thread_delete(NULL);
}

esp_video_render_err_t esp_video_render_worker_create(esp_video_render_worker_cfg_t *cfg, esp_video_render_worker_handle_t *handle)
{
    if (handle == NULL || cfg == NULL || cfg->worker == NULL || cfg->cache_size == 0 ||
        cfg->task_cfg.stack_size == 0) {
        ESP_LOGE(TAG, "Invalid argument handle=%p cfg=%p", handle, cfg);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    worker_inst_t *inst = (worker_inst_t *)video_render_calloc(1, sizeof(worker_inst_t));
    if (inst == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    do {
        inst->queue = data_q_init(cfg->cache_size);
        if (inst->queue == NULL) {
            break;
        }
        video_render_event_grp_create(&inst->event_grp);
        if (inst->event_grp == NULL) {
            break;
        }
        inst->worker = cfg->worker;
        inst->ctx = cfg->ctx;
        inst->is_running = true;
        inst->exit_on_error = cfg->exit_on_error;
        inst->align_size = cfg->align_size ? cfg->align_size : video_render_get_default_alignment();
        if (0 != esp_gmf_oal_thread_create(NULL, "Worker", worker_task, inst,
                                           cfg->task_cfg.stack_size, cfg->task_cfg.priority, true, cfg->task_cfg.core_id)) {
            ESP_LOGE(TAG, "Failed to create output worker");
            inst->is_running = false;
            break;
        }
        *handle = (esp_video_render_worker_handle_t)inst;
        return ESP_VIDEO_RENDER_ERR_OK;
    } while (0);
    esp_video_render_worker_destroy(inst);
    return ESP_VIDEO_RENDER_ERR_NO_RESOURCE;
}

esp_video_render_err_t esp_video_render_worker_write(esp_video_render_worker_handle_t handle, esp_video_render_frame_t *frame)
{
    if (handle == NULL || frame == NULL || frame->size == 0) {
        ESP_LOGE(TAG, "Invalid argument handle=%p frame=%p", handle, frame);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    worker_inst_t *inst = (worker_inst_t *)handle;
    if (inst->queue == NULL) {
        ESP_LOGE(TAG, "Worker cache queue not exists");
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }
    int q_size = frame->size + sizeof(esp_video_render_frame_t) + inst->align_size;
    void *data = data_q_get_buffer(inst->queue, q_size);
    if (data == NULL) {
        ESP_LOGE(TAG, "Failed to get buffer from worker cache queue");
        return ESP_VIDEO_RENDER_ERR_NO_RESOURCE;
    }
    esp_video_render_frame_t *q_frame = (esp_video_render_frame_t *)data;
    *q_frame = *frame;
    q_frame->data = (uint8_t *)ALIGN_UP((intptr_t)data + sizeof(esp_video_render_frame_t), inst->align_size);
    memcpy(q_frame->data, frame->data, frame->size);
    q_size = (int)((q_frame->data + q_frame->size) - (uint8_t *)q_frame);
    int ret = data_q_send_buffer(inst->queue, q_size);
    return ret == 0 ? ESP_VIDEO_RENDER_ERR_OK : ESP_VIDEO_RENDER_ERR_FAIL;
}

esp_video_render_err_t esp_video_render_worker_destroy(esp_video_render_worker_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "Invalid argument handle=%p", handle);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    worker_inst_t *inst = (worker_inst_t *)handle;
    bool is_running = inst->is_running;
    // Wait for worker exit
    if (is_running) {
        inst->is_running = false;
        if (inst->queue) {
            data_q_wakeup(inst->queue);
        }
        video_render_event_grp_wait_bits(inst->event_grp, VIDEO_RENDER_WORKER_EXIT_BIT, VIDEO_RENDER_MAX_LOCK_TIME);
    }
    if (inst->queue) {
        data_q_deinit(inst->queue);
        inst->queue = NULL;
    }
    if (inst->event_grp) {
        video_render_event_grp_destroy(inst->event_grp);
        inst->event_grp = NULL;
    }
    video_render_free(inst);
    return ESP_VIDEO_RENDER_ERR_OK;
}

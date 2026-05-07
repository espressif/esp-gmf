/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdatomic.h>
#include "video_render_proc.h"
#include "video_render_sys.h"
#include "video_render_utils.h"
#include "esp_video_render_proc.h"
#include "esp_gmf_oal_thread.h"
#include "data_queue.h"
#include "esp_log.h"

#define TAG  "ESP_VIDEO_RENDER_PROC"

#define VIDEO_RENDER_PROC_IN_WORKING_BIT   (1 << 0)
#define VIDEO_RENDER_PROC_OUT_WORKING_BIT  (1 << 1)

typedef struct {
    void          *event_grp;
    data_q_t      *queue;
    volatile bool  is_working;
    uint8_t        wait_bit;
    int (*on_frame)(esp_video_render_frame_t *frame, void *ctx);
    void *ctx;
} worker_t;

typedef struct {
    esp_video_render_proc_out_cfg_t  out_cfg;
    void                            *event_grp;
    video_render_proc_handle_t       handle;
    atomic_int                       refer_count;
    worker_t                        *in_worker;
    worker_t                        *out_worker;
    esp_video_render_frame_info_t    out_frame_info;
    bool                             info_ready;
    bool                             is_writing;
    esp_video_render_fb_t            cur_fb;
    esp_video_render_frame_t        *cur_frame;
} video_render_proc_t;

static esp_gmf_err_io_t sink_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    video_render_proc_t *proc = (video_render_proc_t *)handle;
    proc->cur_frame = NULL;
    if (load->buf) {
        return ESP_GMF_IO_OK;
    }
    int queue_data_size = wanted_size + sizeof(esp_video_render_frame_t) + video_render_get_default_alignment();
    void *data = data_q_get_buffer(proc->out_worker->queue, queue_data_size);
    if (data == NULL) {
        return ESP_GMF_IO_FAIL;
    }
    esp_video_render_frame_t *frame = (esp_video_render_frame_t *)data;
    frame->data = (uint8_t *)ALIGN_UP((intptr_t)data + sizeof(esp_video_render_frame_t), video_render_get_default_alignment());
    frame->size = wanted_size;
    proc->cur_frame = frame;
    load->buf = frame->data;
    load->buf_length = wanted_size;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t sink_release(void *handle, esp_gmf_payload_t *load, int wait_ticks)
{
    video_render_proc_t *proc = (video_render_proc_t *)handle;
    esp_gmf_err_io_t ret = ESP_GMF_IO_OK;
    if (proc->info_ready == false) {
        video_render_proc_get_out_frame_info(proc->handle, &proc->out_frame_info);
        proc->info_ready = true;
    }
    int valid_size = 0;
    do {
        if (load->buf == NULL || load->valid_size == 0) {
            ret = ESP_GMF_IO_FAIL;
            break;
        }
        if (proc->cur_frame == NULL) {
            int queue_data_size = load->valid_size + sizeof(esp_video_render_frame_t) + video_render_get_default_alignment();
            void *data = data_q_get_buffer(proc->out_worker->queue, queue_data_size);
            if (data == NULL) {
                ret = ESP_GMF_IO_FAIL;
                break;
            }
            proc->cur_frame = (esp_video_render_frame_t *)data;
            proc->cur_frame->data = (uint8_t *)ALIGN_UP((intptr_t)data + sizeof(esp_video_render_frame_t), video_render_get_default_alignment());
            memcpy(proc->cur_frame->data, load->buf, load->valid_size);
        } else if (load->buf != proc->cur_frame->data) {
            ESP_LOGE(TAG, "Frame buffer mismatch");
            ret = ESP_GMF_IO_FAIL;
            break;
        } else {
            load->buf = NULL;
        }
        proc->cur_frame->format = proc->out_frame_info.format;
        proc->cur_frame->width = proc->out_frame_info.width;
        proc->cur_frame->height = proc->out_frame_info.height;
        proc->cur_frame->size = load->valid_size;
        valid_size = (int)((proc->cur_frame->data + proc->cur_frame->size) - (uint8_t *)proc->cur_frame);
    } while (0);
    if (proc->cur_frame) {
        if (data_q_send_buffer(proc->out_worker->queue, valid_size) != 0) {
            ret = ESP_GMF_IO_FAIL;
        }
    }
    proc->cur_frame = NULL;
    return ret;
}

static esp_gmf_err_io_t sink_fb_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    video_render_proc_t *proc = (video_render_proc_t *)handle;
    if (load->buf) {
        return ESP_GMF_IO_OK;
    }
    proc->cur_fb.data = NULL;
    proc->cur_fb.size = 0;
    int ret = proc->out_cfg.fb_cfg.acquire_fb(&proc->cur_fb, proc->out_cfg.out_ctx);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to acquire frame buffer ret %d", ret);
        return ESP_GMF_IO_FAIL;
    }
    load->buf = proc->cur_fb.data;
    load->buf_length = proc->cur_fb.size;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t sink_fb_release(void *handle, esp_gmf_payload_t *load, int wait_ticks)
{
    video_render_proc_t *proc = (video_render_proc_t *)handle;
    if (proc->info_ready == false) {
        video_render_proc_get_out_frame_info(proc->handle, &proc->out_frame_info);
        proc->info_ready = true;
    }
    esp_gmf_err_io_t ret = ESP_GMF_IO_OK;
    do {
        if (load->buf == NULL || load->valid_size == 0) {
            break;
        }
        if (load->buf != proc->cur_fb.data) {
            int acquire_ret = proc->out_cfg.fb_cfg.acquire_fb(&proc->cur_fb, proc->out_cfg.out_ctx);
            if (acquire_ret != 0 || load->valid_size != proc->cur_fb.size) {
                ESP_LOGE(TAG, "Failed to acquire frame buffer ret %d", acquire_ret);
                ret = ESP_GMF_IO_FAIL;
                break;
            }
            memcpy(proc->cur_fb.data, load->buf, load->valid_size);
        }
        if (proc->out_frame_info.format != proc->cur_fb.info.format ||
            proc->out_frame_info.width != proc->cur_fb.info.width ||
            proc->out_frame_info.height != proc->cur_fb.info.height) {
            ESP_LOGE(TAG, "Frame info mismatch from %dx%d to %dx%d",
                     proc->out_frame_info.width, proc->out_frame_info.height,
                     proc->cur_fb.info.width, proc->cur_fb.info.height);
            ret = ESP_GMF_IO_FAIL;
            break;
        }
        // Callback for frame buffer
        if (proc->out_cfg.on_fb) {
            proc->out_cfg.on_fb(&proc->cur_fb, proc->out_cfg.out_ctx);
        }
    } while (0);
    if (proc->cur_fb.data != NULL && proc->out_cfg.fb_cfg.release_fb) {
        proc->out_cfg.fb_cfg.release_fb(&proc->cur_fb, proc->out_cfg.out_ctx);
    }
    proc->cur_fb.data = NULL;
    return ret;
}

static void destroy_worker(worker_t *worker)
{
    if (worker == NULL) {
        return;
    }
    if (worker->queue) {
        data_q_deinit(worker->queue);
        worker->queue = NULL;
    }
    video_render_free(worker);
}

static void destroy_proc(video_render_proc_t *proc)
{
    if (proc->in_worker) {
        destroy_worker(proc->in_worker);
        proc->in_worker = NULL;
    }
    if (proc->out_worker) {
        destroy_worker(proc->out_worker);
        proc->out_worker = NULL;
    }
    if (proc->handle) {
        video_render_proc_close(proc->handle);
        video_render_proc_destroy(proc->handle);
        proc->handle = NULL;
    }
    if (proc->event_grp) {
        video_render_event_grp_destroy(proc->event_grp);
        proc->event_grp = NULL;
    }
    video_render_free(proc);
}

static void dec_refer_count(video_render_proc_t *proc)
{
    int prev = atomic_fetch_sub(&proc->refer_count, 1);
    if (prev == 1) {
        destroy_proc(proc);
    }
}

static void inc_refer_count(video_render_proc_t *proc)
{
    atomic_fetch_add(&proc->refer_count, 1);
}

esp_video_render_err_t esp_video_render_proc_open(esp_video_render_proc_cfg_t *cfg, esp_video_render_proc_handle_t *proc_hd)
{
    if (cfg == NULL || proc_hd == NULL || cfg->pool == NULL ||
        cfg->in_frame_info.format == ESP_VIDEO_RENDER_FORMAT_NONE) {
        ESP_LOGE(TAG, "Invalid argument cfg=%p, proc_hd=%p", cfg, proc_hd);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (cfg->out_cfg.fb_out) {
        if (cfg->out_cfg.fb_cfg.acquire_fb == NULL ||
            cfg->out_cfg.fb_cfg.release_fb == NULL ||
            cfg->out_cfg.on_fb == NULL
        ) {
            ESP_LOGE(TAG, "Frame buffer output need acquire release and on fb callback");
            return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
        }
    } else {
        if (cfg->out_cfg.on_frame == NULL) {
            ESP_LOGE(TAG, "Frame callback output need on frame callback");
            return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
        }
    }
    video_render_proc_t *proc = (video_render_proc_t *)video_render_calloc(1, sizeof(video_render_proc_t));
    if (proc == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_NO_RESOURCE;
    do {
        inc_refer_count(proc);
        proc->out_cfg = cfg->out_cfg;
        ret = video_render_proc_create(cfg->pool, &proc->handle);
        if (ret != ESP_VIDEO_RENDER_ERR_OK || proc->handle == NULL) {
            ESP_LOGE(TAG, "Failed to create proc");
            break;
        }
        video_render_event_grp_create(&proc->event_grp);
        if (proc->event_grp == NULL) {
            ESP_LOGE(TAG, "Failed to create event group");
            ret = ESP_VIDEO_RENDER_ERR_NO_RESOURCE;
            break;
        }
        if (proc->out_cfg.fb_out) {
            ret = video_render_proc_set_out_port(proc->handle, sink_fb_acquire, sink_fb_release, proc);
        } else {
            ret = video_render_proc_set_writer(proc->handle, proc->out_cfg.on_frame, proc->out_cfg.out_ctx);
        }
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to set output path");
            break;
        }
        ret = video_render_proc_open(proc->handle, &cfg->in_frame_info, &cfg->out_frame_info);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to open proc");
            break;
        }
        *proc_hd = (esp_video_render_proc_handle_t)proc;
        return ESP_VIDEO_RENDER_ERR_OK;
    } while (0);
    destroy_proc(proc);
    return ret;
}

esp_video_render_err_t esp_video_render_proc_write(esp_video_render_proc_handle_t proc_hd, esp_video_render_frame_t *frame)
{
    if (proc_hd == NULL || frame == NULL || frame->size == 0) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_proc_t *proc = (video_render_proc_t *)proc_hd;
    inc_refer_count(proc);
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    do {
        if (proc->handle == NULL) {
            ret = ESP_VIDEO_RENDER_ERR_INVALID_STATE;
            break;
        }
        proc->is_writing = true;
        if (proc->in_worker && proc->in_worker->queue) {
            int q_size = frame->size + sizeof(esp_video_render_frame_t) + video_render_get_default_alignment();
            void *data = data_q_get_buffer(proc->in_worker->queue, q_size);
            if (data == NULL) {
                ret = ESP_VIDEO_RENDER_ERR_NO_RESOURCE;
                break;
            }
            esp_video_render_frame_t *q_frame = (esp_video_render_frame_t *)data;
            *q_frame = *frame;
            q_frame->data = (uint8_t *)ALIGN_UP((intptr_t)data + sizeof(esp_video_render_frame_t), video_render_get_default_alignment());
            memcpy(q_frame->data, frame->data, frame->size);
            q_size = (int)((q_frame->data + q_frame->size) - (uint8_t *)q_frame);
            int send_ret = data_q_send_buffer(proc->in_worker->queue, q_size);
            ret = send_ret == 0 ? ESP_VIDEO_RENDER_ERR_OK : ESP_VIDEO_RENDER_ERR_FAIL;
            break;
        }
        ret = video_render_proc_write(proc->handle, frame);
    } while (0);
    dec_refer_count(proc);
    return ret;
}

esp_video_render_err_t esp_video_render_proc_get_out_frame_info(esp_video_render_proc_handle_t proc_hd, esp_video_render_frame_info_t *info)
{
    if (proc_hd == NULL || info == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_proc_t *proc = (video_render_proc_t *)proc_hd;
    return video_render_proc_get_out_frame_info(proc->handle, info);
}

esp_video_render_err_t esp_video_render_proc_acquire_out(esp_video_render_proc_handle_t proc_hd, esp_video_render_frame_t **frame)
{
    if (proc_hd == NULL || frame == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_proc_t *proc = (video_render_proc_t *)proc_hd;
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    inc_refer_count(proc);
    do {
        if (proc->out_worker == NULL || proc->out_worker->is_working) {
            ESP_LOGE(TAG, "Output worker not set or under working");
            ret = ESP_VIDEO_RENDER_ERR_INVALID_STATE;
            break;
        }
        int size = 0;
        *frame = NULL;
        int read_ret = data_q_read_lock(proc->out_worker->queue, (void **)frame, &size);
        ret = read_ret == 0 ? ESP_VIDEO_RENDER_ERR_OK : ESP_VIDEO_RENDER_ERR_FAIL;
    } while (0);
    dec_refer_count(proc);
    return ret;
}

esp_video_render_err_t esp_video_render_proc_release_out(esp_video_render_proc_handle_t proc_hd, esp_video_render_frame_t *frame)
{
    if (proc_hd == NULL || frame == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_proc_t *proc = (video_render_proc_t *)proc_hd;
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    inc_refer_count(proc);
    do {
        if (proc->out_worker == NULL || proc->out_worker->queue == NULL) {
            ret = ESP_VIDEO_RENDER_ERR_INVALID_STATE;
            break;
        }
        int read_ret = data_q_read_unlock(proc->out_worker->queue);
        ret = read_ret == 0 ? ESP_VIDEO_RENDER_ERR_OK : ESP_VIDEO_RENDER_ERR_FAIL;
    } while (0);
    dec_refer_count(proc);
    return ret;
}

esp_video_render_err_t esp_video_render_proc_close(esp_video_render_proc_handle_t proc_hd)
{
    if (proc_hd == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_proc_t *proc = (video_render_proc_t *)proc_hd;
    uint8_t wait_bits = 0;
    if (proc->in_worker && proc->in_worker->queue) {
        if (proc->in_worker->is_working) {
            wait_bits |= VIDEO_RENDER_PROC_IN_WORKING_BIT;
            proc->in_worker->is_working = false;
        }
        data_q_wakeup(proc->in_worker->queue);
    }
    if (proc->out_worker && proc->out_worker->queue) {
        if (proc->out_worker->is_working) {
            wait_bits |= VIDEO_RENDER_PROC_OUT_WORKING_BIT;
            proc->out_worker->is_working = false;
        }
        data_q_wakeup(proc->out_worker->queue);
    }
    // Wait for working task exit
    if (wait_bits && proc->event_grp) {
        video_render_event_grp_wait_bits(proc->event_grp, wait_bits, VIDEO_RENDER_MAX_LOCK_TIME);
    }
    dec_refer_count(proc);
    return ESP_VIDEO_RENDER_ERR_OK;
}

static int input_worker(esp_video_render_frame_t *frame, void *ctx)
{
    video_render_proc_t *proc = (video_render_proc_t *)ctx;
    return video_render_proc_write(proc->handle, frame);
}

static void worker_task(void *arg)
{
    worker_t *worker = (worker_t *)arg;
    while (worker->is_working) {
        esp_video_render_frame_t *frame = NULL;
        int size = 0;
        int ret = data_q_read_lock(worker->queue, (void **)&frame, &size);
        if (ret != 0) {
            break;
        }
        if (worker->on_frame) {
            ret = worker->on_frame(frame, worker->ctx);
            if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                ESP_LOGE(TAG, "Failed to worker frame ret %d", ret);
            }
        }
        ret = data_q_read_unlock(worker->queue);
        if (ret != 0) {
            break;
        }
    }
    video_render_event_grp_set_bits(worker->event_grp, worker->wait_bit);
    esp_gmf_oal_thread_delete(NULL);
}

esp_video_render_err_t esp_video_render_proc_set_in_worker(esp_video_render_proc_handle_t proc_hd,
                                                           esp_video_render_proc_in_worker_cfg_t *cfg)
{
    if (proc_hd == NULL || cfg == NULL || cfg->in_cache_size == 0 || cfg->task_cfg.stack_size == 0) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_proc_t *proc = (video_render_proc_t *)proc_hd;
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    worker_t *worker = NULL;
    do {
        inc_refer_count(proc);
        if (proc->in_worker || proc->is_writing) {
            ESP_LOGE(TAG, "Input worker already set or writing");
            ret = ESP_VIDEO_RENDER_ERR_INVALID_STATE;
            break;
        }
        worker = (worker_t *)video_render_calloc(1, sizeof(worker_t));
        if (worker == NULL) {
            ret = ESP_VIDEO_RENDER_ERR_NO_MEM;
            break;
        }
        worker->queue = data_q_init(cfg->in_cache_size);
        if (worker->queue == NULL) {
            ret = ESP_VIDEO_RENDER_ERR_NO_RESOURCE;
            break;
        }
        proc->in_worker = worker;
        worker->on_frame = input_worker;
        worker->ctx = proc;
        worker->is_working = true;
        worker->wait_bit = VIDEO_RENDER_PROC_IN_WORKING_BIT;
        worker->event_grp = proc->event_grp;
        if (0 != esp_gmf_oal_thread_create(NULL, "ProcIn", worker_task, worker,
                                           cfg->task_cfg.stack_size, cfg->task_cfg.priority, true, cfg->task_cfg.core_id)) {
            ESP_LOGE(TAG, "Failed to create input worker");
            worker->is_working = false;
            ret = ESP_VIDEO_RENDER_ERR_FAIL;
            break;
        }
    } while (0);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        destroy_worker(worker);
        proc->in_worker = NULL;
    }
    dec_refer_count(proc);
    return ret;
}

esp_video_render_err_t esp_video_render_proc_set_out_worker(esp_video_render_proc_handle_t proc_hd,
                                                            esp_video_render_proc_out_worker_cfg_t *cfg)
{
    if (proc_hd == NULL || cfg == NULL || cfg->out_cache_size == 0 ||
        (cfg->cache_only == false && cfg->task_cfg.stack_size == 0)) {
        ESP_LOGE(TAG, "Invalid argument for proc_hd: %p cfg: %p", proc_hd, cfg);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_proc_t *proc = (video_render_proc_t *)proc_hd;
    inc_refer_count(proc);
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    worker_t *worker = NULL;
    do {
        if (proc->out_cfg.fb_out) {
            ESP_LOGE(TAG, "When fb output is set, no need to set output worker");
            ret = ESP_VIDEO_RENDER_ERR_INVALID_STATE;
            break;
        }
        if (proc->out_worker || proc->is_writing) {
            ESP_LOGE(TAG, "Output worker already set or writing");
            ret = ESP_VIDEO_RENDER_ERR_INVALID_STATE;
            break;
        }
        worker = (worker_t *)video_render_calloc(1, sizeof(worker_t));
        if (worker == NULL) {
            ret = ESP_VIDEO_RENDER_ERR_NO_MEM;
            break;
        }
        worker->queue = data_q_init(cfg->out_cache_size);
        if (worker->queue == NULL) {
            ret = ESP_VIDEO_RENDER_ERR_NO_RESOURCE;
            break;
        }
        proc->out_worker = worker;
        worker->on_frame = proc->out_cfg.on_frame;
        worker->ctx = proc->out_cfg.out_ctx;
        ret = video_render_proc_set_out_port(proc->handle, sink_acquire, sink_release, proc);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to set output port");
            ret = ESP_VIDEO_RENDER_ERR_FAIL;
            break;
        }
        // When cache only is set, no need to create output worker
        if (cfg->cache_only) {
            break;
        }
        worker->is_working = true;
        worker->wait_bit = VIDEO_RENDER_PROC_OUT_WORKING_BIT;
        worker->event_grp = proc->event_grp;
        if (0 != esp_gmf_oal_thread_create(NULL, "ProcOut", worker_task, worker,
                                           cfg->task_cfg.stack_size, cfg->task_cfg.priority, true, cfg->task_cfg.core_id)) {
            ESP_LOGE(TAG, "Failed to create output worker");
            worker->is_working = false;
            ret = ESP_VIDEO_RENDER_ERR_FAIL;
            break;
        }
    } while (0);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        destroy_worker(worker);
        proc->out_worker = NULL;
    }
    dec_refer_count(proc);
    return ret;
}

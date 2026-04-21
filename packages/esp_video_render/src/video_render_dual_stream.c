/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_video_render_types.h"
#include "esp_video_render.h"
#include "esp_video_render_dual_stream.h"
#include "video_render_sys.h"
#include "video_render_utils.h"
#include "data_queue.h"
#include "video_render_proc.h"
#include "esp_log.h"
#include "esp_gmf_oal_thread.h"
#include <string.h>

#define TAG  "RENDER_DUAL_STREAM"

#define DECODE_ALIGN            (64)
#define DECODE_TASK_STACK_SIZE  (32 * 1024)
#define DECODE_TASK_PRIORITY    (CONFIG_ESP_VIDEO_RENDER_BLEND_THREAD_PRIORITY - 2)
#define MAX_FRAME_SIZE          (20 * 1024)

typedef struct render_dual_eyes_t render_dual_eyes_t;

typedef struct {
    data_q_t                         *src_q;
    bool                              dec_running;
    esp_video_render_fb_t             cur_fb;
    esp_video_render_handle_t         render;
    esp_video_render_stream_handle_t  stream;
    video_render_proc_handle_t        proc;
    render_dual_eyes_t               *eyes;
    esp_video_render_rect_t           rect;
    uint32_t                          fixed_size;
    esp_video_render_frame_t          out_frame;
} render_eyes_info_t;

struct render_dual_eyes_t {
    render_eyes_info_t           eye_info[2];
    uint8_t                      fps;
    uint32_t                     max_frame_size;
    bool                         render_async;
    video_render_mutex_handle_t  lock;
};

static int raw_write_cb(esp_video_render_frame_t *frame, void *ctx)
{
    render_eyes_info_t *eye_info = (render_eyes_info_t *)ctx;
    // Open stream
    if (eye_info->stream == NULL) {
        esp_video_render_stream_info_t stream_info = {
            .info = {
                .format = frame->format,
                .width = frame->width,
                .height = frame->height,
                .fps = eye_info->eyes->fps,
            },
            .cached = eye_info->eyes->render_async,  // It will copy decode out to a temp buffer for render and decode continue
        };
        int ret = esp_video_render_stream_open(eye_info->render, &stream_info, &eye_info->stream);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Fail to open stream ret %d", ret);
        }
        // Render in a separate task
        if (eye_info->eyes->render_async) {
            esp_video_render_stream_render_async(eye_info->stream);
        }
        if (eye_info->rect.width && eye_info->rect.height) {
            esp_video_render_stream_set_disp_rect(eye_info->stream, &eye_info->rect);
        }
    }
    return esp_video_render_stream_write(eye_info->stream, frame);
}

static esp_gmf_err_io_t sink_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    render_eyes_info_t *eye_info = (render_eyes_info_t *)handle;
    eye_info->cur_fb.data = NULL;
    if (load->buf) {
        // Already has buffer
        return ESP_GMF_IO_OK;
    }
    // Get frame buffer and lock for it
    esp_video_render_err_t ret = esp_video_render_stream_acquire_fb(eye_info->stream, &eye_info->cur_fb);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        return ESP_GMF_IO_FAIL;
    }
    if (wanted_size > eye_info->cur_fb.size) {
        ESP_LOGE(TAG, "Acquire size too big %d", (int)wanted_size);
        esp_video_render_stream_release_fb(eye_info->stream, &eye_info->cur_fb);
        return ESP_GMF_IO_FAIL;
    }
    load->buf = eye_info->cur_fb.data;
    load->buf_length = eye_info->cur_fb.size;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t sink_release(void *handle, esp_gmf_payload_t *load, int wait_ticks)
{
    render_eyes_info_t *eye_info = (render_eyes_info_t *)handle;
    if (eye_info->cur_fb.data == NULL) {
        esp_video_render_err_t ret = esp_video_render_stream_acquire_fb(eye_info->stream, &eye_info->cur_fb);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            return ESP_GMF_IO_FAIL;
        }
    }
    if (load->valid_size == eye_info->cur_fb.size) {
        // Copy into frame buffer if dec into temp buffer
        if (load->buf != eye_info->cur_fb.data) {
            memcpy(eye_info->cur_fb.data, load->buf, eye_info->cur_fb.size);
        }
        esp_video_render_stream_write_fb(eye_info->stream, &eye_info->cur_fb);
    }
    esp_video_render_stream_release_fb(eye_info->stream, &eye_info->cur_fb);
    return ESP_GMF_IO_OK;
}

static void dec_thread(void *arg)
{
    render_eyes_info_t *eye_info = (render_eyes_info_t *)arg;
    int eye_idx = (eye_info == &eye_info->eyes->eye_info[0]) ? 0 : 1;
    ESP_LOGI(TAG, "Dec for eye %d running", eye_idx);
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    while (eye_info->dec_running) {
        void *data = NULL;
        int size = 0;
        data_q_read_lock(eye_info->src_q, &data, &size);
        if (data == NULL) {
            break;
        }
        esp_video_render_frame_t *frame = (esp_video_render_frame_t *)data;
        do {
            // Open proc firstly
            if (eye_info->proc == NULL) {
                void *pool = NULL;
                ret = esp_video_render_get_pool(eye_info->render, &pool);
                if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                    ESP_LOGE(TAG, "Fail to get render pool");
                    break;
                }
                ret = video_render_proc_create(pool, &eye_info->proc);
                if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                    ESP_LOGE(TAG, "Fail to create proc");
                    break;
                }
                esp_video_render_frame_info_t in_frame = {
                    .format = frame->format,
                    .width = frame->width,
                    .height = frame->height,
                };
                esp_video_render_disp_info_t disp_info = {};
                esp_video_render_get_display_info(eye_info->render, &disp_info);
                esp_video_render_frame_info_t out_frame = {
                    .format = disp_info.format,
                    .width = eye_info->rect.width ? eye_info->rect.width : frame->width,
                    .height = eye_info->rect.height ? eye_info->rect.height : frame->height,
                };
                if (eye_info->rect.width == disp_info.width &&
                    eye_info->rect.height == disp_info.height) {
                    ESP_LOGI(TAG, "Display on frame buffer for eye %d", eye_idx);
                    video_render_proc_set_out_port(eye_info->proc, sink_acquire, sink_release, eye_info);
                    esp_video_render_stream_info_t stream_info = {
                        .info = {
                            .format = disp_info.format,
                            .width = disp_info.width,
                            .height = disp_info.height,
                            .fps = eye_info->eyes->fps,
                        },
                    };
                    ret = esp_video_render_stream_open(eye_info->render, &stream_info, &eye_info->stream);
                    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                        ESP_LOGE(TAG, "Fail to open stream ret %d", ret);
                        break;
                    }
                    // Render in a separate task
                    if (eye_info->eyes->render_async) {
                        esp_video_render_stream_render_async(eye_info->stream);
                    }
                    if (eye_info->rect.width && eye_info->rect.height) {
                        esp_video_render_stream_set_disp_rect(eye_info->stream, &eye_info->rect);
                    }
                } else {
                    video_render_proc_set_writer(eye_info->proc, raw_write_cb, eye_info);
                }
                ret = video_render_proc_open(eye_info->proc, &in_frame, &out_frame);
                if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                    ESP_LOGE(TAG, "Fail to open proc");
                    break;
                }
            }
            ret = video_render_proc_write(eye_info->proc, frame);
            if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                ESP_LOGE(TAG, "Fail to write proc ret %d", ret);
                break;
            }
        } while (0);
        data_q_read_unlock(eye_info->src_q);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            break;
        }
    }
    eye_info->dec_running = false;
    ESP_LOGI(TAG, "Dec for eye %d exited", eye_idx);
    esp_gmf_oal_thread_delete(NULL);
}

esp_video_render_err_t esp_video_render_dual_stream_open(esp_video_render_dual_stream_cfg_t *cfg, esp_video_render_dual_stream_handle_t *handle)
{
    if (cfg == NULL || handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    render_dual_eyes_t *eyes = (render_dual_eyes_t *)video_render_calloc(1, sizeof(render_dual_eyes_t));
    if (eyes == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    eyes->max_frame_size = cfg->max_frame_size ? cfg->max_frame_size : MAX_FRAME_SIZE;
    int src_num = cfg->frame_count ? cfg->frame_count : 2;
    do {
        eyes->lock = video_render_mutex_create();
        if (eyes->lock == NULL) {
            ret = ESP_VIDEO_RENDER_ERR_NO_RESOURCE;
            break;
        }
        eyes->fps = cfg->fps;
        eyes->render_async = cfg->render_async;
        for (int i = 0; i < 2; i++) {
            render_eyes_info_t *eye_info = &eyes->eye_info[i];
            if (cfg->render[i] == NULL) {
                ret = ESP_VIDEO_RENDER_ERR_INVALID_ARG;
                break;
            }
            eye_info->render = cfg->render[i];
            eye_info->eyes = eyes;
            int padding = sizeof(esp_video_render_frame_t) + DECODE_ALIGN + 8;
            eye_info->src_q = data_q_init((eyes->max_frame_size + padding) * src_num);
            if (eye_info->src_q == NULL) {
                ret = ESP_VIDEO_RENDER_ERR_NO_MEM;
                break;
            }
            char task_name[32];
            snprintf(task_name, sizeof(task_name), "Eye_Dec%d", i);
            eye_info->dec_running = true;
            esp_video_render_task_cfg_t task_cfg = cfg->task_cfg[i];
            if (task_cfg.stack_size == 0) {
                task_cfg.stack_size = DECODE_TASK_STACK_SIZE;
            }
            if (task_cfg.priority == 0) {
                task_cfg.priority = DECODE_TASK_PRIORITY;
            }
            if (ESP_GMF_ERR_OK != esp_gmf_oal_thread_create(NULL, task_name, dec_thread, eye_info,
                                                            task_cfg.stack_size, task_cfg.priority, true, task_cfg.core_id)) {
                ESP_LOGE(TAG, "Failed to create decode thread %s", task_name);
                eye_info->dec_running = false;
                ret = ESP_VIDEO_RENDER_ERR_NO_MEM;
                break;
            }
        }
    } while (0);
    if (ret == ESP_VIDEO_RENDER_ERR_OK) {
        *handle = (esp_video_render_dual_stream_handle_t)eyes;
    } else {
        esp_video_render_dual_stream_close((esp_video_render_dual_stream_handle_t)eyes);
    }
    return ret;
}

esp_video_render_err_t esp_video_render_dual_stream_set_display_rect(esp_video_render_dual_stream_handle_t handle,
                                                                     uint8_t eyes_idx,
                                                                     esp_video_render_rect_t *rect)
{
    if (handle == NULL || eyes_idx >= 2 || rect == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    render_dual_eyes_t *eyes = (render_dual_eyes_t *)handle;
    render_eyes_info_t *eye_info = &eyes->eye_info[eyes_idx];
    // Get display info from render
    esp_video_render_disp_info_t disp_info = {};
    esp_video_render_err_t ret = esp_video_render_get_display_info(eye_info->render, &disp_info);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        return ret;
    }
    if (rect->x + rect->width > disp_info.width || rect->y + rect->height > disp_info.height) {
        ESP_LOGE(TAG, "Out of display for eye %d", eyes_idx);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (eye_info->stream) {
        esp_video_render_stream_set_disp_rect(eye_info->stream, rect);
    } else {
        eye_info->rect = *rect;
    }
    return ret;
}

esp_video_render_err_t esp_video_render_dual_stream_get_buffer(esp_video_render_dual_stream_handle_t handle,
                                                               uint8_t eyes_idx,
                                                               esp_video_render_frame_t *frame)
{
    if (handle == NULL || eyes_idx >= 2 || frame == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    render_dual_eyes_t *eyes = (render_dual_eyes_t *)handle;
    render_eyes_info_t *eye_info = &eyes->eye_info[eyes_idx];
    int size = sizeof(esp_video_render_frame_t) + DECODE_ALIGN + eyes->max_frame_size;
    void *data = data_q_get_buffer(eye_info->src_q, size);
    if (data == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    uint8_t *ptr = (uint8_t *)data + sizeof(esp_video_render_frame_t);
    frame->data = (uint8_t *)ALIGN_UP((intptr_t)ptr, DECODE_ALIGN);
    frame->size = eyes->max_frame_size;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_video_render_dual_stream_release_buffer(esp_video_render_dual_stream_handle_t handle,
                                                                   uint8_t eyes_idx,
                                                                   esp_video_render_frame_t *frame)
{
    if (handle == NULL || eyes_idx >= 2 || frame == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    render_dual_eyes_t *eyes = (render_dual_eyes_t *)handle;
    render_eyes_info_t *eye_info = &eyes->eye_info[eyes_idx];
    if (frame->data) {
        data_q_send_buffer(eye_info->src_q, 0);
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t send_frame(render_eyes_info_t *eye_info, esp_video_render_frame_t *frame)
{
    void *data = data_q_get_write_data(eye_info->src_q);
    int max_block = (int)(sizeof(esp_video_render_frame_t) + DECODE_ALIGN + eye_info->eyes->max_frame_size);
    uint8_t *block_start = (uint8_t *)data;
    uint8_t *block_end = block_start + max_block;
    if ((uint8_t *)frame->data < block_start || ((uint8_t *)frame->data + frame->size) > block_end) {
        ESP_LOGE(TAG, "Frame data out of range");
        data_q_send_buffer(eye_info->src_q, 0);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (eye_info->dec_running == false) {
        data_q_send_buffer(eye_info->src_q, 0);
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }
    memcpy(data, frame, sizeof(esp_video_render_frame_t));
    int size = (int)(((uint8_t *)frame->data + frame->size) - (uint8_t *)data);
    int ret = data_q_send_buffer(eye_info->src_q, size);
    return ret == 0 ? ESP_VIDEO_RENDER_ERR_OK : ESP_VIDEO_RENDER_ERR_FAIL;
}

esp_video_render_err_t esp_video_render_dual_stream_send_buffer(esp_video_render_dual_stream_handle_t handle,
                                                                esp_video_render_frame_t *frame_left,
                                                                esp_video_render_frame_t *frame_right)
{
    if (handle == NULL || frame_left == NULL || frame_right == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    render_dual_eyes_t *eyes = (render_dual_eyes_t *)handle;
    render_eyes_info_t *left = &eyes->eye_info[0];
    render_eyes_info_t *right = &eyes->eye_info[1];
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    esp_video_render_err_t err_ret;
    // If one send fail try another also
    err_ret = send_frame(left, frame_left);
    if (err_ret != ESP_VIDEO_RENDER_ERR_OK) {
        ret = err_ret;
    }
    err_ret = send_frame(right, frame_right);
    if (err_ret != ESP_VIDEO_RENDER_ERR_OK) {
        ret = err_ret;
    }
    return ret;
}

esp_video_render_err_t esp_video_render_dual_stream_close(esp_video_render_dual_stream_handle_t handle)
{
    if (handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    render_dual_eyes_t *eyes = (render_dual_eyes_t *)handle;
    render_eyes_info_t *left = &eyes->eye_info[0];
    render_eyes_info_t *right = &eyes->eye_info[1];
    // Wakeup for decode thread
    for (int i = 0; i < 2; i++) {
        render_eyes_info_t *eye_info = &eyes->eye_info[i];
        if (eye_info->src_q) {
            data_q_wakeup(eye_info->src_q);
        }
    }
    // Waiting for task exit
    while (left->dec_running || right->dec_running) {
        video_render_delay(10);
    }
    // Clearup
    for (int i = 0; i < 2; i++) {
        render_eyes_info_t *eye_info = &eyes->eye_info[i];
        if (eye_info->proc) {
            video_render_proc_destroy(eye_info->proc);
            eye_info->proc = NULL;
        }
        if (eye_info->stream) {
            esp_video_render_stream_close(eye_info->stream);
            eye_info->stream = NULL;
        }
        if (eye_info->src_q) {
            data_q_deinit(eye_info->src_q);
            eye_info->src_q = NULL;
        }
    }
    if (eyes->lock) {
        video_render_mutex_destroy(eyes->lock);
        eyes->lock = NULL;
    }
    video_render_free(eyes);
    return ESP_VIDEO_RENDER_ERR_OK;
}

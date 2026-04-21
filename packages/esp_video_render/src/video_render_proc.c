/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "video_render_proc.h"
#include "video_render_sys.h"
#include "video_render_pipeline.h"
#include "esp_log.h"
#include "esp_video_render_log.h"
#include "video_render_measure.h"

#define TAG  "VIDEO_RENDER_PROC"

#define MAX_FRAME_COUNT  1

typedef struct {
    esp_gmf_pool_handle_t        pool;
    esp_gmf_element_handle_t    *proc_elements;
    uint8_t                      proc_num;
    esp_gmf_pipeline_handle_t    pipeline;
    bool                         is_opened;
    bool                         is_error;
    bool                         is_writing;
    esp_video_render_write_cb_t  writer;
    port_acquire                 out_acquire;
    port_release                 out_release;
    void                        *ctx;
    uint8_t                     *frame_buffer;
    uint32_t                     frame_size;
    uint8_t                     *out_frame_buf[MAX_FRAME_COUNT];
    uint8_t                     *borrow_frame_buf;
    uint32_t                     out_frame_size;
    uint8_t                      out_frame_index;
    esp_video_render_frame_t     out_frame;
} video_proc_t;

const char *esp_gmf_video_get_format_string(uint32_t codec);

#define IS_SAME_FRAME_INFO(a, b)  \
    ((a)->format == (b)->format && (a)->width == (b)->width && (a)->height == (b)->height && (a)->fps == (b)->fps)

static esp_gmf_err_t pipeline_event_hdlr(esp_gmf_event_pkt_t *pkt, void *ctx)
{
    video_proc_t *proc = (video_proc_t *)ctx;
    if (pkt == NULL) {
        return ESP_GMF_ERR_OK;
    }
    if (pkt->type == ESP_GMF_EVT_TYPE_REPORT_INFO && pkt->payload) {
        esp_gmf_info_video_t *info = (esp_gmf_info_video_t *)pkt->payload;
        proc->out_frame.width = info->width;
        proc->out_frame.height = info->height;
        proc->out_frame.format = (esp_video_render_format_t)info->format_id;
        ESP_LOGI(TAG, "Output frame %s %dx%d", esp_gmf_video_get_format_string(proc->out_frame.format),
                 proc->out_frame.width, proc->out_frame.height);
    }
    if (pkt->type != ESP_GMF_EVT_TYPE_CHANGE_STATE) {
        return ESP_GMF_ERR_OK;
    }
    int pipe_event = pkt->sub;
    ESP_LOGI(TAG, "Get pipeline state event %d", pipe_event);
    if (ESP_GMF_EVENT_STATE_ERROR == pipe_event) {
        proc->is_error = true;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_io_t src_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    video_proc_t *proc = (video_proc_t *)handle;
    if (proc->frame_buffer) {
        load->buf = proc->frame_buffer;
        load->buf_length = proc->frame_size;
        load->valid_size = proc->frame_size;
        return ESP_GMF_IO_OK;
    }
    return ESP_GMF_IO_FAIL;
}

static esp_gmf_err_io_t src_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    video_proc_t *proc = (video_proc_t *)handle;
    if (proc->frame_buffer && load->valid_size <= proc->frame_size) {
        // Consume for input buffer
        proc->frame_buffer += load->valid_size;
        proc->frame_size -= load->valid_size;
        return ESP_GMF_IO_OK;
    }
    return ESP_GMF_IO_FAIL;
}

static bool is_internal_frame_buf(video_proc_t *proc, uint8_t *buf)
{
    for (int i = 0; i < MAX_FRAME_COUNT; i++) {
        if (proc->out_frame_buf[i] == buf) {
            return true;
        }
    }
    return false;
}

static esp_gmf_err_io_t sink_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    video_proc_t *proc = (video_proc_t *)handle;
    if (load->buf && is_internal_frame_buf(proc, load->buf) == false) {
        // Already has buffer
        return ESP_GMF_IO_OK;
    }
    if (proc->out_frame_size < wanted_size) {
        if (proc->out_frame_size) {
            ESP_LOGE(TAG, "Not support change frame size in middle");
            return ESP_GMF_IO_FAIL;
        }
        proc->out_frame_size = 0;
        for (int i = 0; i < MAX_FRAME_COUNT; i++) {
            if (proc->out_frame_buf[i]) {
                video_render_free(proc->out_frame_buf[i]);
                proc->out_frame_buf[i] = NULL;
            }
            proc->out_frame_buf[i] = video_render_malloc_align(wanted_size, 64);
            if (proc->out_frame_buf[i] == NULL) {
                // Not enough memory
                return ESP_GMF_IO_FAIL;
            }
        }
        proc->out_frame_size = wanted_size;
    }
    load->buf = proc->out_frame_buf[proc->out_frame_index];
    load->buf_length = proc->out_frame_size;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t sink_release(void *handle, esp_gmf_payload_t *load, int wait_ticks)
{
    video_proc_t *proc = (video_proc_t *)handle;
    if (load->buf) {
        proc->out_frame.data = load->buf;
        proc->out_frame.size = load->valid_size;
        int ret = 0;
        if (proc->writer) {
            ret = proc->writer(&proc->out_frame, proc->ctx);
        }
        // Set borrow flag so that not free during close
        proc->borrow_frame_buf = (proc->out_frame.data == NULL) ? load->buf : NULL;
        // Hacking code if writer clear proc->out_frame.data then GMF payload buf ownership is transfer
        if (proc->out_frame_buf[proc->out_frame_index] || proc->out_frame.data == NULL) {
            load->buf = NULL;
        }
        proc->out_frame_index = (proc->out_frame_index + 1) % MAX_FRAME_COUNT;
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            return ESP_GMF_IO_FAIL;
        }
    }
    return ESP_GMF_IO_OK;
}

static void free_procs(video_proc_t *proc)
{
    if (proc->proc_elements) {
        for (int i = 0; i < proc->proc_num; i++) {
            if (proc->proc_elements[i]) {
                esp_gmf_obj_delete(proc->proc_elements[i]);
                proc->proc_elements[i] = NULL;
            }
        }
    }
    video_render_free(proc->proc_elements);
    proc->proc_elements = NULL;
    proc->proc_num = 0;
}

static inline esp_gmf_job_err_t run_one_element(video_proc_t *proc, esp_gmf_element_handle_t element)
{
    esp_gmf_job_err_t ret;
    do {
        // Open element if needed
        esp_gmf_event_state_t st = ESP_GMF_EVENT_STATE_NONE;
        esp_gmf_element_get_state(element, &st);
        if (st == ESP_GMF_EVENT_STATE_INITIALIZED) {
            ret = esp_gmf_element_process_open(element, NULL);
            if (ret < 0) {
                break;
            }
        }
        MEASURE_BEGIN("Proc", "%s", OBJ_GET_TAG(element));
        ret = esp_gmf_element_process_running(element, NULL);
        MEASURE_END("Proc", "%s", OBJ_GET_TAG(element));
        if (ret < 0) {
            break;
        }
        return ret;
    } while (0);
    proc->is_error = true;
    return ret;
}

static esp_gmf_job_err_t run_post_pipeline(video_proc_t *proc, esp_gmf_element_handle_t element)
{
    esp_gmf_pipeline_get_next_el(proc->pipeline, element, &element);
    esp_gmf_job_err_t ret = ESP_GMF_JOB_ERR_OK;
    for (; element;) {
        ret = run_one_element(proc, element);
        // Job fail to run
        if (ret < 0) {
            return ret;
        }
        if (ret == ESP_GMF_JOB_ERR_CONTINUE) {
            break;
        }
        if (ret == ESP_GMF_JOB_ERR_TRUNCATE) {
            // Element still have input data
            ret = run_post_pipeline(proc, element);
            if (ret != ESP_GMF_JOB_ERR_OK) {
                break;
            }
            continue;
        }
        // Need run again
        esp_gmf_pipeline_get_next_el(proc->pipeline, element, &element);
    }
    return ret;
}

esp_video_render_err_t video_render_proc_create(esp_gmf_pool_handle_t pool, video_render_proc_handle_t *handle)
{
    if (pool == NULL || handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_proc_t *proc = (video_proc_t *)video_render_calloc(1, sizeof(video_proc_t));
    if (proc == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    proc->pool = pool;
    *handle = (video_render_proc_handle_t)proc;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_proc_add(video_render_proc_handle_t handle, esp_video_render_proc_type_t *procs,
                                             uint8_t proc_num)
{
    if (handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    // Allow to add proc even when running
    video_proc_t *proc = (video_proc_t *)handle;
    if (proc_num == 0) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    // We always add proc and never remove util destroy
    uint8_t new_proc_num = proc->proc_num + proc_num;
    esp_gmf_element_handle_t *new_element = video_render_realloc(proc->proc_elements,
                                                                 new_proc_num * sizeof(esp_gmf_element_handle_t));
    if (new_element == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    // Set new elements to NULL
    memset(new_element + proc->proc_num, 0, proc_num * sizeof(esp_gmf_element_handle_t));
    proc->proc_elements = new_element;
    new_proc_num = proc->proc_num;
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    for (int i = 0; i < proc_num; i++) {
        esp_gmf_element_handle_t element = video_render_proc_get_element(handle, procs[i]);
        // Already added
        if (element) {
            continue;
        }
        proc->proc_elements[new_proc_num] = video_render_create_element(proc->pool, procs[i]);
        if (proc->proc_elements[new_proc_num] == NULL) {
            ESP_LOGE(TAG, "Fail to create element %d", i);
            ret = ESP_VIDEO_RENDER_ERR_NO_RESOURCE;
            break;
        }
        new_proc_num++;
    }
    proc->proc_num = new_proc_num;
    video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "add proc %d\n", new_proc_num);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        free_procs(proc);
    }
    return ret;
}

esp_video_render_err_t video_render_proc_set_writer(video_render_proc_handle_t handle, esp_video_render_write_cb_t writer, void *ctx)
{
    video_proc_t *proc = (video_proc_t *)handle;
    if (proc == NULL || writer == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    proc->writer = writer;
    proc->ctx = ctx;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_proc_set_out_port(video_render_proc_handle_t handle, port_acquire acquire, port_release release, void *ctx)
{
    video_proc_t *proc = (video_proc_t *)handle;
    if (proc == NULL || acquire == NULL || release == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (proc->is_writing) {
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }
    proc->out_acquire = acquire;
    proc->out_release = release;
    proc->ctx = ctx;
    // Allow to reset out port when not writing yet
    if (proc->pipeline) {
        esp_gmf_element_handle_t tail_element = ESP_GMF_PIPELINE_GET_LAST_ELEMENT(proc->pipeline);
        if (tail_element == NULL) {
            proc->is_error = true;
            return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
        }
        esp_gmf_element_unregister_out_port(tail_element, NULL);
        esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BLOCK(proc->out_acquire, proc->out_release, NULL, proc->ctx, 0, ESP_GMF_MAX_DELAY);
        if (out_port == NULL) {
            proc->is_error = true;
            return ESP_VIDEO_RENDER_ERR_NO_RESOURCE;
        }
        esp_gmf_element_register_out_port(tail_element, out_port);
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_proc_open(video_render_proc_handle_t handle,
                                              esp_video_render_frame_info_t *in_frame_info,
                                              esp_video_render_frame_info_t *out_frame_info)
{
    if (handle == NULL || in_frame_info == NULL || out_frame_info == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_proc_t *proc = (video_proc_t *)handle;
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    esp_gmf_port_handle_t in_port = NULL;
    esp_gmf_port_handle_t out_port = NULL;
    if (proc->is_opened) {
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }
    do {
        if (proc->proc_num > 0 || IS_SAME_FRAME_INFO(in_frame_info, out_frame_info) == false) {
            in_port = NEW_ESP_GMF_PORT_IN_BLOCK(src_acquire, src_release, NULL, proc, 0, ESP_GMF_MAX_DELAY);
            if (proc->out_acquire && proc->out_release) {
                out_port = NEW_ESP_GMF_PORT_OUT_BLOCK(proc->out_acquire, proc->out_release, NULL, proc->ctx, 0, ESP_GMF_MAX_DELAY);
            } else {
                out_port = NEW_ESP_GMF_PORT_OUT_BLOCK(sink_acquire, sink_release, NULL, proc, 0, ESP_GMF_MAX_DELAY);
            }
            if (in_port == NULL || out_port == NULL) {
                ESP_LOGE(TAG, "Fail to create port");
                if (in_port) {
                    esp_gmf_port_deinit(in_port);
                }
                break;
            }
            video_render_pipeline_cfg_t pipeline_cfg = {
                .in_frame_info = *in_frame_info,
                .out_frame_info = *out_frame_info,
                .in_port = in_port,
                .out_port = out_port,
                .pool = proc->pool,
                .proc_elements = proc->proc_elements,
                .proc_num = proc->proc_num,
                .cb = pipeline_event_hdlr,
                .ctx = proc,
            };
            ret = video_render_pipeline_open(&pipeline_cfg, &proc->pipeline);
            if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                ESP_LOGE(TAG, "Fail to create pipeline");
                break;
            }
        }
        proc->is_opened = true;
        return ret;
    } while (0);
    video_render_proc_close((video_render_proc_handle_t)proc);
    return ESP_VIDEO_RENDER_ERR_NO_RESOURCE;
}

esp_gmf_element_handle_t video_render_proc_get_element(video_render_proc_handle_t handle, esp_video_render_proc_type_t type)
{
    video_proc_t *proc = (video_proc_t *)handle;
    if (proc == NULL) {
        return NULL;
    }
    // Try to get from added elements
    for (int i = 0; i < proc->proc_num; i++) {
        const esp_gmf_cap_t *caps = NULL;
        esp_gmf_element_get_caps(proc->proc_elements[i], &caps);
        for (; caps; caps = caps->next) {
            if (caps->cap_eightcc == type) {
                return proc->proc_elements[i];
            }
        }
    }
    if (proc->pipeline == NULL) {
        return NULL;
    }
    // Get element from pipeline
    return video_render_pipeline_get_element(proc->pipeline, type);
}

esp_video_render_err_t video_render_proc_get_out_frame_info(video_render_proc_handle_t handle, esp_video_render_frame_info_t *info)
{
    video_proc_t *proc = (video_proc_t *)handle;
    if (proc == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (proc->out_frame.format == ESP_VIDEO_RENDER_FORMAT_NONE) {
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }
    info->format = proc->out_frame.format;
    info->width = proc->out_frame.width;
    info->height = proc->out_frame.height;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_proc_write(video_render_proc_handle_t handle, esp_video_render_frame_t *frame)
{
    video_proc_t *proc = (video_proc_t *)handle;
    if (proc == NULL || frame == NULL || frame->data == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (proc->is_opened == false || proc->is_error) {
        ESP_LOGE(TAG, "Proc not opened or error open:%d", proc->is_opened);
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }
    proc->is_writing = true;
    // Return if no need further process
    if (proc->pipeline == NULL) {
        if (proc->writer) {
            return proc->writer(frame, proc->ctx);
        }
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    proc->frame_buffer = frame->data;
    proc->frame_size = frame->size;

    // Exit only when all input data consumed
    while (proc->frame_size > 0) {
        esp_gmf_element_handle_t element = NULL;
        esp_gmf_pipeline_get_head_el(proc->pipeline, &element);
        for (; element;) {
            esp_gmf_job_err_t ret = run_one_element(proc, element);
            if (ret < 0) {
                return ESP_VIDEO_RENDER_ERR_FAIL;
            }
            // Input buffer not enough
            if (ret == ESP_GMF_JOB_ERR_CONTINUE) {
                break;
            }
            if (ret == ESP_GMF_JOB_ERR_TRUNCATE) {
                // Element still have input data
                ret = run_post_pipeline(proc, element);
                if (ret != ESP_GMF_JOB_ERR_OK) {
                    proc->is_error = true;
                    return ESP_VIDEO_RENDER_ERR_FAIL;
                }
                // Continue to run this element
                continue;
            }
            // Need run again
            esp_gmf_pipeline_get_next_el(proc->pipeline, element, &element);
            if (element == NULL && ret == ESP_GMF_JOB_ERR_DONE) {
                break;
            }
        }
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_proc_close(video_render_proc_handle_t handle)
{
    video_proc_t *proc = (video_proc_t *)handle;
    if (proc == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (proc->is_opened == false) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    proc->is_opened = false;
    proc->is_error = false;
    proc->is_writing = false;
    if (proc->pipeline) {
        video_render_pipeline_close(proc->pipeline, proc->proc_elements, proc->proc_num);
        proc->pipeline = NULL;
    }
    for (int i = 0; i < MAX_FRAME_COUNT; i++) {
        if (proc->out_frame_buf[i] != NULL && proc->out_frame_buf[i] != proc->borrow_frame_buf) {
            video_render_free(proc->out_frame_buf[i]);
        }
        proc->out_frame_buf[i] = NULL;
    }
    proc->borrow_frame_buf = NULL;
    proc->out_frame_size = 0;
    return ESP_VIDEO_RENDER_ERR_OK;
}

void video_render_proc_destroy(video_render_proc_handle_t handle)
{
    if (handle == NULL) {
        return;
    }
    video_proc_t *proc = (video_proc_t *)handle;
    video_render_proc_close(handle);
    free_procs(proc);
    video_render_free(handle);
}

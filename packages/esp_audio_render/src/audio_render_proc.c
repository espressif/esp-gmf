/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "audio_render_proc.h"
#include "audio_render_mem.h"
#include "audio_render_pipeline.h"
#include "esp_log.h"

#define TAG "AUD_RENDER_PROC"

typedef struct {
    esp_gmf_pool_handle_t          pool;
    esp_audio_render_proc_type_t  *procs;
    esp_gmf_element_handle_t      *proc_elements;
    uint8_t                        proc_num;
    uint8_t                        buf_align;
    esp_gmf_pipeline_handle_t      pipeline;
    bool                           is_opened;
    bool                           is_error;
    esp_audio_render_write_cb_t    writer;
    void*                          ctx;
    uint8_t*                       pcm_buffer;
    uint32_t                       pcm_size;
    uint8_t*                       out_pcm;
    uint32_t                       out_pcm_size;
} audio_proc_t;

static esp_gmf_err_t pipeline_event_hdlr(esp_gmf_event_pkt_t *pkt, void *ctx)
{
    audio_proc_t *proc = (audio_proc_t*)ctx;
    if (pkt == NULL || pkt->type != ESP_GMF_EVT_TYPE_CHANGE_STATE) {
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
    audio_proc_t *proc = (audio_proc_t*)handle;
    if (proc->pcm_buffer) {
        load->buf = proc->pcm_buffer;
        int valid_size = wanted_size > proc->pcm_size ? proc->pcm_size : wanted_size;
        load->buf_length = valid_size;
        load->valid_size = valid_size;
        return ESP_GMF_IO_OK;
    }
    return ESP_GMF_IO_FAIL;
}

static esp_gmf_err_io_t src_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    audio_proc_t *proc = (audio_proc_t*)handle;
    if (proc->pcm_buffer && load->valid_size <= proc->pcm_size) {
        // Consume for input buffer
        proc->pcm_buffer += load->valid_size;
        proc->pcm_size -= load->valid_size;
        return ESP_GMF_IO_OK;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t sink_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    audio_proc_t *proc = (audio_proc_t*)handle;
    if (load->buf && load->buf != proc->out_pcm) {
        // Already has buffer
        return ESP_GMF_IO_OK;
    }
    if (proc->out_pcm_size < wanted_size) {
        if (proc->buf_align) {
            if (proc->out_pcm) {
                audio_render_free(proc->out_pcm);
                proc->out_pcm_size = 0;
            }
            proc->out_pcm = audio_render_malloc_align(wanted_size, proc->buf_align);
            if (proc->out_pcm == NULL) {
                // Not enough memory
                return ESP_GMF_IO_FAIL;
            }
            proc->out_pcm_size = wanted_size;
        }
        uint8_t *out_pcm = audio_render_realloc(proc->out_pcm, wanted_size);
        if (out_pcm == NULL) {
            // Not enough memory
            return ESP_GMF_IO_FAIL;
        }
        proc->out_pcm = out_pcm;
        proc->out_pcm_size = wanted_size;
    }
    load->buf = proc->out_pcm;
    load->buf_length = proc->out_pcm_size;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t sink_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    audio_proc_t *proc = (audio_proc_t*)handle;
    if (load->buf) {
        int ret = proc->writer(load->buf, load->valid_size, proc->ctx);
        if (proc->out_pcm) {
            load->buf = NULL;
        }
        if (ret != ESP_AUDIO_RENDER_ERR_OK) {
            return ESP_GMF_IO_FAIL;
        }
    }
    return ESP_GMF_IO_OK;
}

static void free_procs(audio_proc_t *proc)
{
    if (proc->proc_elements) {
        for (int i = 0; i < proc->proc_num; i++) {
            if (proc->proc_elements[i]) {
                esp_gmf_obj_delete(proc->proc_elements[i]);
                proc->proc_elements[i] = NULL;
            }
        }
    }
    audio_render_free(proc->procs);
    audio_render_free(proc->proc_elements);
    proc->proc_elements = NULL;
    proc->procs = NULL;
    proc->proc_num = 0;
}

static esp_gmf_job_err_t run_post_pipeline(audio_proc_t *proc, esp_gmf_element_handle_t element)
{
    esp_gmf_pipeline_get_next_el(proc->pipeline, element, &element);
    for (; element; ) {
        esp_gmf_job_err_t ret = esp_gmf_element_process_running(element, NULL);
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
    return ESP_GMF_JOB_ERR_OK;
}

esp_audio_render_err_t audio_render_proc_create(esp_gmf_pool_handle_t pool, audio_render_proc_handle_t *handle)
{
    if (pool == NULL || handle == NULL) {
        ESP_LOGE(TAG, "Invalid arg for pool:%p handle:%p", pool, handle);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_proc_t *proc = (audio_proc_t*)audio_render_calloc(1, sizeof(audio_proc_t));
    if (proc == NULL) {
        ESP_LOGE(TAG, "No memory for audio processor");
        return ESP_AUDIO_RENDER_ERR_NO_MEM;
    }
    proc->pool = pool;
    *handle = (audio_render_proc_handle_t)proc;
    return ESP_AUDIO_RENDER_ERR_OK;
}

esp_audio_render_err_t audio_render_proc_set_buf_align(audio_render_proc_handle_t handle, uint8_t buf_align)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "Invalid arg for handle:%p", handle);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_proc_t *proc = (audio_proc_t*)handle;
    proc->buf_align = buf_align;
    return ESP_AUDIO_RENDER_ERR_OK;
}

esp_audio_render_err_t audio_render_proc_add(audio_render_proc_handle_t handle, esp_audio_render_proc_type_t *procs,
                                             uint8_t proc_num)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "Invalid arg for handle:%p", handle);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_proc_t *proc = (audio_proc_t*)handle;
    if (proc->is_opened) {
        ESP_LOGE(TAG, "Not allow add processor after running");
        return ESP_AUDIO_RENDER_ERR_INVALID_STATE;
    }
    free_procs(proc);
    if (proc_num == 0) {
        return ESP_AUDIO_RENDER_ERR_OK;
    }
    do {
        proc->proc_num = proc_num;
        int size = sizeof(esp_audio_render_proc_type_t) * proc_num;;
        proc->procs = (esp_audio_render_proc_type_t*)audio_render_calloc(1, size);
        if (proc->procs == NULL) {
            break;
        }
        memcpy(proc->procs, procs, size);
        proc->proc_elements = audio_render_calloc(proc_num, sizeof(esp_gmf_element_handle_t));
        if (proc->procs == NULL) {
            break;
        }
        int i = 0;
        for (; i < proc_num; i++) {
            proc->proc_elements[i] = audio_render_create_element(proc->pool, procs[i]);
            if (proc->proc_elements[i] == NULL) {
                ESP_LOGE(TAG, "Fail to create element %d", i);
                break;
            }
        }
        if (i < proc_num) {
            break;
        }
        return ESP_AUDIO_RENDER_ERR_OK;
    } while (0);
    free_procs(proc);
    return ESP_AUDIO_RENDER_ERR_NO_MEM;
}

esp_audio_render_err_t audio_render_proc_open(audio_render_proc_handle_t handle,
                                              esp_audio_render_sample_info_t *in_sample_info,
                                              esp_audio_render_sample_info_t *out_sample_info)
{
    if (handle == NULL || in_sample_info == NULL || out_sample_info == NULL) {
        ESP_LOGE(TAG, "Invalid arg for handle:%p in_sample_info:%p out_sample_info:%p", handle,
                 in_sample_info, out_sample_info);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_proc_t *proc = (audio_proc_t *)handle;
    esp_audio_render_err_t ret = ESP_AUDIO_RENDER_ERR_OK;
    esp_gmf_port_handle_t in_port = NULL;
    esp_gmf_port_handle_t out_port = NULL;
    if (proc->is_opened) {
        ESP_LOGW(TAG, "Already opened for handle:%p", handle);
        return ESP_AUDIO_RENDER_ERR_INVALID_STATE;
    }
    do {
        if (proc->proc_num > 0 || false == SAME_SAMPLE_INFO(*in_sample_info, *out_sample_info)) {
            in_port = NEW_ESP_GMF_PORT_IN_BLOCK(src_acquire, src_release, NULL, proc, 0, ESP_GMF_MAX_DELAY);
            out_port = NEW_ESP_GMF_PORT_OUT_BLOCK(sink_acquire, sink_release, NULL, proc, 0, ESP_GMF_MAX_DELAY);
            if (in_port == NULL || out_port == NULL) {
                ESP_LOGE(TAG, "Fail to create port");
                if (in_port) {
                    esp_gmf_port_deinit(in_port);
                }
                if (out_port) {
                    esp_gmf_port_deinit(out_port);
                }
                break;
            }
            audio_render_pipeline_cfg_t pipeline_cfg = {
                .in_sample_info = *in_sample_info,
                .out_sample_info = *out_sample_info,
                .in_port = in_port,
                .out_port = out_port,
                .pool = proc->pool,
                .proc_elements = proc->proc_elements,
                .proc_num = proc->proc_num,
            };
            // When create pipeline success port will takeover by pipeline
            ret = audio_render_pipeline_open(&pipeline_cfg, &proc->pipeline);
            if (ret != ESP_AUDIO_RENDER_ERR_OK) {
                ESP_LOGE(TAG, "Fail to create pipeline");
                break;
            }
        }
        if (proc->pipeline) {
            esp_gmf_pipeline_set_event(proc->pipeline, pipeline_event_hdlr, proc);
        }
        proc->is_opened = true;
        return ret;
    } while (0);
    audio_render_proc_close((audio_render_proc_handle_t) proc);
    return ESP_AUDIO_RENDER_ERR_NO_RESOURCE;
}

esp_gmf_element_handle_t audio_render_proc_get_element(audio_render_proc_handle_t handle, esp_audio_render_proc_type_t type)
{
    audio_proc_t *proc = (audio_proc_t*)handle;
    if (proc == NULL) {
        ESP_LOGE(TAG, "Invalid arg for handle:%p", handle);
        return NULL;
    }
    if (proc->pipeline == NULL) {
        // Try to get from added elements
        for (int i = 0; i < proc->proc_num; i++) {
            if (proc->procs[i] == type) {
                return proc->proc_elements[i];
            }
        }
        return NULL;
    }
    // Get element from pipeline
    return audio_render_pipeline_get_element(proc->pipeline, type);
}

esp_audio_render_err_t audio_render_proc_set_writer(audio_render_proc_handle_t handle, esp_audio_render_write_cb_t writer, void *ctx)
{
    audio_proc_t *proc = (audio_proc_t*)handle;
    if (proc == NULL || writer == NULL) {
        ESP_LOGE(TAG, "Invalid arg for handle:%p writer:%p", handle, writer);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    proc->writer = writer;
    proc->ctx = ctx;
    return ESP_AUDIO_RENDER_ERR_OK;
}

esp_audio_render_err_t audio_render_proc_write(audio_render_proc_handle_t handle, uint8_t *pcm_data, uint32_t pcm_size)
{
    audio_proc_t *proc = (audio_proc_t*)handle;
    if (proc == NULL || pcm_data == NULL || pcm_size == 0) {
        ESP_LOGE(TAG, "Invalid arg for handle:%p data:%p size:%d", handle, pcm_data, (int)pcm_size);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    if (proc->is_opened == false || proc->is_error || proc->writer == NULL) {
        ESP_LOGE(TAG, "Invalid state for open:%d error:%d writer:%p", proc->is_opened, proc->is_error, proc->writer);
        return ESP_AUDIO_RENDER_ERR_INVALID_STATE;
    }
    // Return if no need further process
    if (proc->pipeline == NULL) {
        return proc->writer(pcm_data, pcm_size, proc->ctx);
    }
    proc->pcm_buffer = pcm_data;
    proc->pcm_size = pcm_size;

    // Exit only when all input data consumed
    while (proc->pcm_size > 0) {
        esp_gmf_element_handle_t element = NULL;
        esp_gmf_pipeline_get_head_el(proc->pipeline, &element);
        for (; element; ) {
            esp_gmf_job_err_t ret = esp_gmf_element_process_running(element, NULL);
            // Job fail to run
            if (ret < 0) {
                ESP_LOGE(TAG, "Fail to run element %s:%p ret %d", OBJ_GET_TAG(element), element, ret);
                proc->is_error = true;
                return ESP_AUDIO_RENDER_ERR_FAIL;
            }
            // Input buffer not enough
            if (ret == ESP_GMF_JOB_ERR_CONTINUE) {
                break;
            }
            if (ret == ESP_GMF_JOB_ERR_TRUNCATE) {
                // Element still have input data
                ret = run_post_pipeline(proc, element);
                if (ret != ESP_GMF_JOB_ERR_OK) {
                    ESP_LOGE(TAG, "Fail to re-run post pipeline ret %d", ret);
                    proc->is_error = true;
                    return ESP_AUDIO_RENDER_ERR_FAIL;
                }
                // Continue to run this element
                continue;
            }
            // Need run again
            esp_gmf_pipeline_get_next_el(proc->pipeline, element, &element);
        }
    }
    return ESP_AUDIO_RENDER_ERR_OK;
}

esp_audio_render_err_t audio_render_proc_close(audio_render_proc_handle_t handle)
{
    audio_proc_t *proc = (audio_proc_t*)handle;
    if (proc == NULL) {
        ESP_LOGE(TAG, "Invalid arg for handle:%p", handle);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    if (proc->is_opened == false) {
        return ESP_AUDIO_RENDER_ERR_OK;
    }
    proc->is_opened = false;
    proc->is_error = false;
    if (proc->pipeline) {
        audio_render_pipeline_close(proc->pipeline, proc->proc_elements, proc->proc_num);
        proc->pipeline = NULL;

    }
    if (proc->out_pcm) {
        audio_render_free(proc->out_pcm);
        proc->out_pcm = NULL;
    }
     proc->out_pcm_size = 0;
    return ESP_AUDIO_RENDER_ERR_OK;
}

void audio_render_proc_destroy(audio_render_proc_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "Invalid arg for handle:%p", handle);
        return;
    }
    audio_proc_t *proc = (audio_proc_t*)handle;
    audio_render_proc_close(handle);
    free_procs(proc);
    if (handle) {
        audio_render_free(handle);
    }
}

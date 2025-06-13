/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief  Common Test Items for Audio Elements
 *
 *         This test suite covers the following common test scenarios for audio elements:
 *         1. Element Run State Testing:
 *         - Tests element behavior in different runtime scenarios such as:
 *           - normal run to finish
 *           - normal run to stop
 *           - error on open
 *           - error on process
 *           - run with multiple tasks
 *
 *         2. Method Interface Testing:
 *         - Tests element method interfaces in different elementstates
 *
 *         3. Lifecycle Interface Testing:
 *         - Tests init, open, process, close, and destroy interfaces
 *         - Verifies memory leak prevention
 *
 *         4. Acquire/Release Consistency Check:
 *         - Ensures that every acquire call is matched with a corresponding release, preventing resource leaks or unexpected deadlocks
 *
 *         5. Capability Testing:
 *         - Tests retrieving element tags through capabilities
 *
 *         6. Runtime Report Info Update Test:
 *         - Tests the functionality of updating sound info dynamically during runtime via report info
 *
 *         7. Decoder Testing:
 *         - Tests different input sizes to ensure coverage of all process branches
 *
 *         8. Encoder Testing:
 *         - Tests various input sizes and types to ensures complete coverage of encoder process branches
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "unity.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_method_helper.h"
#include "gmf_loader_setup_defaults.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_audio_dec.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_eq.h"
#include "esp_gmf_alc.h"
#include "esp_gmf_fade.h"
#include "esp_gmf_mixer.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_sonic.h"
#include "esp_gmf_interleave.h"
#include "esp_gmf_deinterleave.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_audio_dec.h"
#include "esp_gmf_audio_methods_def.h"
#include "gmf_audio_el_com.h"
#include "gmf_audio_play_com.h"
#include "gmf_audio_pattern.h"

#define TAG                     "AUD_EL_TEST"
#define TEST_AUDIO_SAMPLE_RATE  (48000)
#define TEST_AUDIO_CHANNELS     (2)
#define TEST_AUDIO_BITS         (16)
#define TEST_AUDIO_BITRATE      (96000)
#define TEST_AUDIO_ALIGNMENT    (16)
#define TEST_AUDIO_FRAME_SIZE   (1024)
#define GET_EL_NUM(arr)         (sizeof(arr) / sizeof(char *))
#define AUDIO_EL_MAX_STACK_SIZE (40 * 1024)
#define SWEEP_QUEUE_SIZE        4
#define PIPELINE_BLOCK_BIT      BIT(0)

extern const uint8_t test_flac_start[] asm("_binary_test_48000hz_16bit_2ch_5000ms_flac_start");
extern const uint8_t test_flac_end[] asm("_binary_test_48000hz_16bit_2ch_5000ms_flac_end");

#define DEFAULT_SINGLE_IN_SINGLE_OUT_CONFIG() {  \
    .in_port_num  = 1,                           \
    .out_port_num = 1,                           \
    .el_cnt       = 1,                           \
    .src_size     = 1024,                        \
}

#define DEFAULT_SINGLE_IN_MULTI_OUT_CONFIG() {  \
    .in_port_num  = 1,                          \
    .out_port_num = 2,                          \
    .el_cnt       = 1,                          \
    .src_size     = 1024,                       \
}

#define DEFAULT_MULTI_IN_SINGLE_OUT_CONFIG() {  \
    .in_port_num  = 2,                          \
    .out_port_num = 1,                          \
    .el_cnt       = 1,                          \
    .src_size     = 1024,                       \
}

typedef struct {
    uint32_t  in_port_num;
    uint32_t  out_port_num;
    uint64_t *caps_cc;
    uint8_t   el_cnt;
    uint32_t  src_size;
    void      (*config_func)(esp_gmf_element_handle_t, void *);
} audio_el_res_cfg_t;

static void SAFE_FREE(void *ptr)
{
    if (ptr) {
        free(ptr);
        ptr = NULL;
    }
}

static esp_err_t audio_el_pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    // The warning messages are used to make the content more noticeable.
    ESP_LOGW(TAG, "CB: RECV Pipeline EVT: el:%s-%p, type:%x, sub:%s, payload:%p, size:%d,%p",
             "OBJ_GET_TAG(event->from)", event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    if ((event->sub == ESP_GMF_EVENT_STATE_STOPPED)
        || (event->sub == ESP_GMF_EVENT_STATE_FINISHED)
        || (event->sub == ESP_GMF_EVENT_STATE_ERROR)) {
        if (ctx) {
            xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_BLOCK_BIT);
        }
    }
    return 0;
}

// Test different audio elements configuration in different states
static void audio_el_cfg_task(void *pvParameters)
{
    audio_el_res_t *res = (audio_el_res_t *)pvParameters;
    while (!res->is_end) {
        if (res->config_func) {
            res->config_func(res->current_hd[0], res);
        }
        vTaskDelay(500 / portTICK_RATE_MS);
    }
    ESP_LOGI(TAG, "Cfg task end");
    res->cfg_task_hd = NULL;
    vTaskDelete(NULL);
}

static void audio_el_data_gene_task(void *pvParameters)
{
    audio_el_in_test_t *in_inst = pvParameters;
    bool change_params = in_inst->change_info;
    esp_gmf_info_sound_t *sound_info = &(in_inst->src_info);
    bool is_done = false;
    sweep_data_t data_packet = {0};
    data_packet.data = in_inst->src_data;
    data_packet.buf_length = in_inst->src_size;
    if (in_inst->is_sweep_signal == false) {
        const uint8_t *flac_data = test_flac_start;
        const size_t flac_size = test_flac_end - test_flac_start;
        size_t current_pos = 0;
        while (!is_done) {
            size_t remaining = flac_size - current_pos;
            size_t copy_size = (remaining < in_inst->src_size) ? remaining : in_inst->src_size;
            if (copy_size == 0) {
                is_done = true;
                break;
            }
            memcpy(data_packet.data, flac_data + current_pos, copy_size);
            current_pos += copy_size;
            data_packet.size = copy_size;
            data_packet.is_released = false;
            data_packet.is_done = (current_pos >= flac_size);
            if (xQueueSend(in_inst->sweep_queue1, &data_packet, portMAX_DELAY) != pdPASS) {
                ESP_LOGE(TAG, "Failed to send FLAC data to queue");
                continue;
            }
            sweep_data_t release_signal;
            if (xQueueReceive(in_inst->sweep_queue2, &release_signal, portMAX_DELAY) == pdPASS) {
                if (release_signal.is_released) {
                    memset(in_inst->src_data, 0, in_inst->src_size);
                }
            }
        }
    } else {
        const float duration = SWEEP_DURATION_MS / 1000.0f;
        uint32_t total_samples = sound_info->sample_rates * duration;
        uint32_t samples_per_chunk = in_inst->src_size / (sound_info->channels * (sound_info->bits / 8));
        uint32_t current_sample = 0;
        while (!is_done) {
            audio_generate_sweep_signal(&data_packet, sound_info, current_sample, total_samples, samples_per_chunk, &is_done);
            if (change_params && current_sample == total_samples / 2) {
                xQueueSend(in_inst->sweep_queue1, &data_packet, portMAX_DELAY);
                current_sample += data_packet.size / (sound_info->channels * (sound_info->bits / 8));
                sweep_data_t release_signal;
                xQueueReceive(in_inst->sweep_queue2, &release_signal, portMAX_DELAY);
                if (release_signal.is_released) {
                    memset(in_inst->src_data, 0, in_inst->src_size);
                }
                sound_info->sample_rates = 44100;
                sound_info->channels = 2;
                sound_info->bits = 16;
                sound_info->bitrate = 80000;
                esp_gmf_pipeline_report_info(in_inst->pipeline, ESP_GMF_INFO_SOUND, sound_info, sizeof(*sound_info));
                total_samples = sound_info->sample_rates * duration;
                samples_per_chunk = in_inst->src_size / (sound_info->channels * (sound_info->bits / 8));
                ESP_LOGI(TAG, "Changed params: sr=%d, ch=%d, bits=%d", sound_info->sample_rates, sound_info->channels, sound_info->bits);
                continue;
            }
            xQueueSend(in_inst->sweep_queue1, &data_packet, portMAX_DELAY);
            sweep_data_t release_signal;
            xQueueReceive(in_inst->sweep_queue2, &release_signal, portMAX_DELAY);
            if (release_signal.is_released) {
                memset(in_inst->src_data, 0, in_inst->src_size);
            }
            current_sample += data_packet.size / (sound_info->channels * (sound_info->bits / 8));
        }
    }
    in_inst->is_done = true;
    ESP_LOGI(TAG, "Data gene task end");
    in_inst->task_hd = NULL;
    vTaskDelete(NULL);
}

static esp_gmf_err_io_t audio_el_in_acquire_check_proc_err(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    return ESP_GMF_IO_FAIL;
}

static esp_gmf_err_io_t audio_el_in_release_check_proc_err(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t audio_el_in_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    audio_el_in_test_t *in_inst = (audio_el_in_test_t *)handle;
    load->pts = in_inst->in_frame_count * 100;
    sweep_data_t sweep_data;
    esp_gmf_err_io_t ret = ESP_GMF_IO_OK;
    if (in_inst->is_done) {
        in_inst->in_acquire_count++;
        goto __acq_exit;
    }
    if (xQueueReceive(in_inst->sweep_queue1, &sweep_data, pdMS_TO_TICKS(wait_ticks)) == pdPASS) {
        in_inst->in_acquire_count++;
        ESP_LOGD(TAG, "In frame %p size %d", sweep_data.data, sweep_data.size);
        load->buf = sweep_data.data;
        load->valid_size = sweep_data.size;
        load->buf_length = sweep_data.buf_length;
        load->is_done = sweep_data.is_done;
        return ESP_GMF_IO_OK;
    }
    ret = ESP_GMF_IO_TIMEOUT;
__acq_exit:
    load->buf = in_inst->src_data;
    load->valid_size = 0;
    load->buf_length = sweep_data.buf_length;
    return ret;
}

static esp_gmf_err_io_t audio_el_in_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    audio_el_in_test_t *in_inst = (audio_el_in_test_t *)handle;
    in_inst->in_release_count++;
    in_inst->in_frame_count++;
    ESP_LOGD(TAG, "In frame %ld size %d", in_inst->in_release_count, load->valid_size);
    sweep_data_t release_signal;
    release_signal.data = load->buf;
    release_signal.is_released = true;
    xQueueSend(in_inst->sweep_queue2, &release_signal, 300);
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t audio_el_out_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    audio_el_out_test_t *out_inst = (audio_el_out_test_t *)handle;
    out_inst->out_acquire_count++;
    if (load->buf) {
        out_inst->no_need_free = true;
    } else {
        if (wanted_size > out_inst->out_max_size) {
            SAFE_FREE(out_inst->out_data);
            out_inst->out_data = NULL;
            uint8_t *new_buf = esp_gmf_oal_malloc_align(TEST_AUDIO_ALIGNMENT, wanted_size + TEST_AUDIO_ALIGNMENT);
            if (new_buf == NULL) {
                ESP_LOGE(TAG, "Fail to allocate %d bytes for output buffer", (int)wanted_size + TEST_AUDIO_ALIGNMENT);
                return ESP_GMF_IO_FAIL;
            }
            out_inst->out_data = new_buf;
            out_inst->out_max_size = wanted_size + TEST_AUDIO_ALIGNMENT;
        }
        load->buf = out_inst->out_data;
        load->buf_length = out_inst->out_max_size;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t audio_el_out_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    audio_el_out_test_t *out_inst = (audio_el_out_test_t *)handle;
    out_inst->out_release_count++;
    ESP_LOGD(TAG, "Out frame %ld size %ld", out_inst->out_release_count, out_inst->out_size);
    out_inst->out_frame_count++;
    if (out_inst->no_need_free == false) {
        load->buf = NULL;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_element_handle_t audio_el_get_element_by_caps_from_pool(esp_gmf_pool_handle_t pool, uint64_t caps_cc)
{
    const void *iter = NULL;
    esp_gmf_element_handle_t element = NULL;
    while (esp_gmf_pool_iterate_element(pool, &iter, &element) == ESP_GMF_ERR_OK) {
        const esp_gmf_cap_t *caps = NULL;
        esp_gmf_element_get_caps(element, &caps);
        while (caps) {
            if (caps->cap_eightcc == caps_cc) {
                return element;
            }
            caps = caps->next;
        }
    }
    return NULL;
}

static const char *audio_el_get_element_name_by_caps(audio_el_res_t *res, uint64_t caps_cc)
{
    // Test get element name by caps
    esp_gmf_element_handle_t element;
    element = audio_el_get_element_by_caps_from_pool(res->pool, caps_cc);
    const char *name = OBJ_GET_TAG(element);
    return name;
}

static void audio_el_set_audio_info(audio_el_res_t *res)
{
    for (uint32_t i = 0; i < res->in_port_num; i++) {
        res->in_inst[i].src_info.sample_rates = TEST_AUDIO_SAMPLE_RATE;
        res->in_inst[i].src_info.channels = TEST_AUDIO_CHANNELS;
        res->in_inst[i].src_info.bits = TEST_AUDIO_BITS;
        res->in_inst[i].src_info.format_id = ESP_AUDIO_TYPE_PCM;
        res->in_inst[i].src_info.bitrate = TEST_AUDIO_BITRATE;
        res->in_inst[i].src_size = 1024;
        res->in_inst[i].is_done = false;
        res->in_inst[i].change_info = true;
        res->in_inst[i].is_sweep_signal = true;
    }
}

static void audio_el_pool_init(audio_el_res_t *res)
{
    esp_gmf_pool_init(&res->pool);
    TEST_ASSERT_NOT_EQUAL(NULL, res->pool);
    gmf_register_audio_all(res->pool);
    ESP_GMF_POOL_SHOW_ITEMS(res->pool);
}

static void audio_el_pool_deinit(audio_el_res_t *res)
{
    if (res->pool) {
        gmf_unregister_audio_all(res->pool);
        esp_gmf_pool_deinit(res->pool);
        res->pool = NULL;
    }
}

static void audio_el_prepare_audio_pipeline(audio_el_res_t *res, char **elements)
{
    res->pipe_sync_evt = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL(res->pipe_sync_evt);
    esp_gmf_pool_new_pipeline(res->pool, NULL, (const char **)elements, res->el_cnt, NULL, &res->pipe);
    TEST_ASSERT_NOT_EQUAL(NULL, res->pipe);
    // Create and register for each input port
    for (uint32_t i = 0; i < res->in_port_num; i++) {
        esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BLOCK(audio_el_in_acquire, audio_el_in_release, NULL, &res->in_inst[i], i, 300);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reg_el_port(res->pipe, elements[0], ESP_GMF_IO_DIR_READER, in_port));
    }
    // Create and register output ports
    for (uint32_t i = 0; i < res->out_port_num; i++) {
        esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BLOCK(audio_el_out_acquire, audio_el_out_release, NULL, &res->out_inst[i], i, 300);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reg_el_port(res->pipe, elements[res->el_cnt - 1], ESP_GMF_IO_DIR_WRITER, out_port));
    }
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.thread.stack_in_ext = true;
    cfg.thread.stack = AUDIO_EL_MAX_STACK_SIZE;
    cfg.thread.core = 1;
    esp_gmf_task_init(&cfg, &res->task);
    TEST_ASSERT_NOT_EQUAL(NULL, res->task);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(res->pipe, res->task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(res->pipe, audio_el_pipeline_event, res->pipe_sync_evt));
    for (int i = 0; i < res->el_cnt; i++) {
        esp_gmf_pipeline_get_el_by_name(res->pipe, elements[i], &res->current_hd[i]);
    }
}

static void audio_el_release_audio_pipeline(audio_el_res_t *res)
{
    if (res->pipe) {
        esp_gmf_pipeline_stop(res->pipe);
    }
    if (res->task) {
        esp_gmf_task_deinit(res->task);
        res->task = NULL;
    }
    if (res->pipe) {
        esp_gmf_pipeline_destroy(res->pipe);
        res->pipe = NULL;
    }
    if (res->pipe_sync_evt) {
        vEventGroupDelete(res->pipe_sync_evt);
        res->pipe_sync_evt = NULL;
    }
}

static void audio_el_res_init(audio_el_res_cfg_t *cfg, audio_el_res_t **res)
{
    *res = (audio_el_res_t *)heap_caps_calloc(1, sizeof(audio_el_res_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    TEST_ASSERT_NOT_EQUAL(NULL, *res);
    (*res)->in_port_num = cfg->in_port_num;
    (*res)->out_port_num = cfg->out_port_num;
    (*res)->el_cnt = cfg->el_cnt;
    audio_el_pool_init(*res);
    char **elements = NULL;
    if (cfg->caps_cc) {
        elements = (char **)heap_caps_calloc(cfg->el_cnt, sizeof(char *), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        TEST_ASSERT_NOT_EQUAL(NULL, elements);
        for (int i = 0; i < cfg->el_cnt; i++) {
            elements[i] = audio_el_get_element_name_by_caps(*res, cfg->caps_cc[i]);
            TEST_ASSERT_NOT_EQUAL(NULL, elements[i]);
        }
    }
    (*res)->in_inst = (audio_el_in_test_t *)heap_caps_calloc((*res)->in_port_num, sizeof(audio_el_in_test_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    TEST_ASSERT_NOT_EQUAL(NULL, (*res)->in_inst);
    (*res)->out_inst = (audio_el_out_test_t *)heap_caps_calloc((*res)->out_port_num, sizeof(audio_el_out_test_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    TEST_ASSERT_NOT_EQUAL(NULL, (*res)->out_inst);
    audio_el_prepare_audio_pipeline(*res, elements);
    SAFE_FREE(elements);
}

static void audio_el_res_deinit(audio_el_res_t *res)
{
    SAFE_FREE(res->in_inst);
    res->in_inst = NULL;
    SAFE_FREE(res->out_inst);
    res->out_inst = NULL;
    audio_el_release_audio_pipeline(res);
    audio_el_pool_deinit(res);
    SAFE_FREE(res);
}

static void audio_el_inst_init(audio_el_res_t *res)
{
    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    UBaseType_t priority = uxTaskPriorityGet(current_task);
    for (uint32_t i = 0; i < res->in_port_num; i++) {
        res->in_inst[i].pipeline = res->pipe;
        res->in_inst[i].sweep_queue1 = xQueueCreate(SWEEP_QUEUE_SIZE, sizeof(sweep_data_t));
        TEST_ASSERT_NOT_EQUAL(NULL, res->in_inst[i].sweep_queue1);
        res->in_inst[i].sweep_queue2 = xQueueCreate(SWEEP_QUEUE_SIZE, sizeof(sweep_data_t));
        TEST_ASSERT_NOT_EQUAL(NULL, res->in_inst[i].sweep_queue2);
        res->in_inst[i].src_data = heap_caps_malloc(res->in_inst[i].src_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        TEST_ASSERT_NOT_EQUAL(NULL, res->in_inst[i].src_data);
        char task_name[32] = {0};
        sprintf(task_name, "sweep_task_%ld", i);
        xTaskCreatePinnedToCore(audio_el_data_gene_task, task_name, 10240, &res->in_inst[i], priority + i + 3, &(res->in_inst[i].task_hd), 1);
        TEST_ASSERT_NOT_EQUAL(NULL, res->in_inst[i].task_hd);
    }
    res->is_end = false;
}

static void audio_el_inst_deinit(audio_el_res_t *res)
{
    if (res->in_inst) {
        for (uint32_t i = 0; i < res->in_port_num; i++) {
            if (res->in_inst[i].task_hd) {
                vTaskDelete(res->in_inst[i].task_hd);
                res->in_inst[i].task_hd = NULL;
            }
            if (res->in_inst[i].sweep_queue1) {
                vQueueDelete(res->in_inst[i].sweep_queue1);
                res->in_inst[i].sweep_queue1 = NULL;
            }
            if (res->in_inst[i].sweep_queue2) {
                vQueueDelete(res->in_inst[i].sweep_queue2);
                res->in_inst[i].sweep_queue2 = NULL;
            }
            SAFE_FREE(res->in_inst[i].src_data);
            res->in_inst[i].src_data = NULL;
        }
        memset(res->in_inst, 0, sizeof(audio_el_in_test_t) * res->in_port_num);
    }
    if (res->out_inst) {
        for (uint32_t i = 0; i < res->out_port_num; i++) {
            SAFE_FREE(res->out_inst[i].out_data);
            res->out_inst[i].out_data = NULL;
        }
        memset(res->out_inst, 0, sizeof(audio_el_out_test_t) * res->out_port_num);
    }
    if (res->cfg_task_hd) {
        vTaskDelete(res->cfg_task_hd);
        res->cfg_task_hd = NULL;
    }
}

static void test_element_run_stop(audio_el_res_t *res)
{
    ESP_LOGI(TAG, "Test_element_run_stop");
    audio_el_inst_init(res);
    xTaskCreate(audio_el_cfg_task, "cfg_task", 10240, res, 6, &(res->cfg_task_hd));
    TEST_ASSERT_NOT_EQUAL(NULL, res->cfg_task_hd);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reset(res->pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(res->pipe));
    for (uint32_t i = 0; i < res->in_port_num; i++) {
        esp_gmf_pipeline_report_info(res->pipe, ESP_GMF_INFO_SOUND, &res->in_inst[i].src_info, sizeof(res->in_inst[i].src_info));
    }
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res->pipe));
    vTaskDelay(2000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res->pipe));
    xEventGroupWaitBits(res->pipe_sync_evt, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    res->is_end = true;
    // Test in acquire and in release count should be equal
    for (uint32_t i = 0; i < res->in_port_num; i++) {
        ESP_LOGI(TAG, "In port %ld acquire count %ld, release count %ld", i, res->in_inst[i].in_acquire_count, res->in_inst[i].in_release_count);
        TEST_ASSERT_EQUAL(res->in_inst[i].in_acquire_count, res->in_inst[i].in_release_count);
    }
    // Test out acquire and out release count should be equal
    for (uint32_t i = 0; i < res->out_port_num; i++) {
        ESP_LOGI(TAG, "Out port %ld acquire count %ld, release count %ld", i, res->out_inst[i].out_acquire_count, res->out_inst[i].out_release_count);
        TEST_ASSERT_EQUAL(res->out_inst[i].out_acquire_count, res->out_inst[i].out_release_count);
    }
    audio_el_inst_deinit(res);
}

static void test_element_run_finish(audio_el_res_t *res)
{
    ESP_LOGI(TAG, "Test_element_run_finish");
    audio_el_inst_init(res);
    xTaskCreate(audio_el_cfg_task, "cfg_task", 10240, res, 6, &(res->cfg_task_hd));
    TEST_ASSERT_NOT_EQUAL(NULL, res->cfg_task_hd);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reset(res->pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(res->pipe));
    for (uint32_t i = 0; i < res->in_port_num; i++) {
        esp_gmf_pipeline_report_info(res->pipe, ESP_GMF_INFO_SOUND, &res->in_inst[i].src_info, sizeof(res->in_inst[i].src_info));
    }
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res->pipe));
    char *el_name = OBJ_GET_TAG(res->current_hd[0]);
    // Mixer need to stop by user
    if (strcmp(el_name, "aud_mixer") == 0) {
        vTaskDelay(6000 / portTICK_RATE_MS);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res->pipe));
    } else {
        xEventGroupWaitBits(res->pipe_sync_evt, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res->pipe));
    }
    res->is_end = true;
    // Test in acquire and in release count should be equal
    for (uint32_t i = 0; i < res->in_port_num; i++) {
        ESP_LOGI(TAG, "In port %ld acquire count %ld, release count %ld", i, res->in_inst[i].in_acquire_count, res->in_inst[i].in_release_count);
        TEST_ASSERT_EQUAL(res->in_inst[i].in_acquire_count, res->in_inst[i].in_release_count);
    }
    // Test out acquire and out release count should be equal
    for (uint32_t i = 0; i < res->out_port_num; i++) {
        ESP_LOGI(TAG, "Out port %ld acquire count %ld, release count %ld", i, res->out_inst[i].out_acquire_count, res->out_inst[i].out_release_count);
        TEST_ASSERT_EQUAL(res->out_inst[i].out_acquire_count, res->out_inst[i].out_release_count);
    }
    audio_el_inst_deinit(res);
}

static void test_element_run_error_open(audio_el_res_t *res)
{
    ESP_LOGI(TAG, "Test_element_run_error_open");
    audio_el_set_audio_info(res);
    audio_el_inst_init(res);
    xTaskCreate(audio_el_cfg_task, "cfg_task", 10240, res, 6, &(res->cfg_task_hd));
    TEST_ASSERT_NOT_EQUAL(NULL, res->cfg_task_hd);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reset(res->pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(res->pipe));
    for (uint32_t i = 0; i < res->in_port_num; i++) {
        res->in_inst[i].src_info.bits = 20;
        res->in_inst[i].src_info.channels = 0;
        esp_gmf_pipeline_report_info(res->pipe, ESP_GMF_INFO_SOUND, &res->in_inst[i].src_info, sizeof(res->in_inst[i].src_info));
    }
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res->pipe));
    vTaskDelay(2000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res->pipe));
    res->is_end = true;
    audio_el_inst_deinit(res);
}

static void test_element_run_error_process(audio_el_res_t *res)
{
    ESP_LOGI(TAG, "Test_element_run_error_process");
    esp_gmf_element_unregister_in_port(res->pipe->head_el, NULL);
    // Create and register for each input port
    for (uint32_t i = 0; i < res->in_port_num; i++) {
        esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BLOCK(audio_el_in_acquire_check_proc_err, audio_el_in_release_check_proc_err, NULL, &res->in_inst[i], i, 300);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reg_el_port(res->pipe, OBJ_GET_TAG(res->current_hd[0]), ESP_GMF_IO_DIR_READER, in_port));
    }
    audio_el_set_audio_info(res);
    audio_el_inst_init(res);
    xTaskCreate(audio_el_cfg_task, "cfg_task", 10240, res, 6, &(res->cfg_task_hd));
    TEST_ASSERT_NOT_EQUAL(NULL, res->cfg_task_hd);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reset(res->pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(res->pipe));
    for (uint32_t i = 0; i < res->in_port_num; i++) {
        esp_gmf_pipeline_report_info(res->pipe, ESP_GMF_INFO_SOUND, &res->in_inst[i].src_info, sizeof(res->in_inst[i].src_info));
    }
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res->pipe));
    vTaskDelay(2000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res->pipe));
    res->is_end = true;
    audio_el_inst_deinit(res);
}

static void test_element_run_with_multi_task(audio_el_res_cfg_t *cfg, void (*config_func)(esp_gmf_element_handle_t, void *))
{
    ESP_LOGI(TAG, "Test_element_run_with_multi_task");
    audio_el_res_t *res1 = NULL;
    audio_el_res_t *res2 = NULL;
    audio_el_res_init(cfg, &res1);
    audio_el_res_init(cfg, &res2);
    res1->config_func = config_func;
    res2->config_func = config_func;
    audio_el_set_audio_info(res1);
    audio_el_set_audio_info(res2);
    for (uint32_t i = 0; i < res1->in_port_num; i++) {
        res1->in_inst[i].src_size = cfg->src_size;
    }
    for (uint32_t i = 0; i < res2->in_port_num; i++) {
        res2->in_inst[i].src_size = cfg->src_size;
    }
    audio_el_inst_init(res1);
    audio_el_inst_init(res2);
    xTaskCreate(audio_el_cfg_task, "cfg_task1", 10240, res1, 6, &(res1->cfg_task_hd));
    xTaskCreate(audio_el_cfg_task, "cfg_task2", 10240, res2, 7, &(res2->cfg_task_hd));
    TEST_ASSERT_NOT_EQUAL(NULL, res1->cfg_task_hd);
    TEST_ASSERT_NOT_EQUAL(NULL, res2->cfg_task_hd);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reset(res1->pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reset(res2->pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(res1->pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(res2->pipe));
    for (uint32_t i = 0; i < res1->in_port_num; i++) {
        esp_gmf_pipeline_report_info(res1->pipe, ESP_GMF_INFO_SOUND, &res1->in_inst[i].src_info, sizeof(res1->in_inst[i].src_info));
    }
    for (uint32_t i = 0; i < res2->in_port_num; i++) {
        esp_gmf_pipeline_report_info(res2->pipe, ESP_GMF_INFO_SOUND, &res2->in_inst[i].src_info, sizeof(res2->in_inst[i].src_info));
    }
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res1->pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res2->pipe));
    vTaskDelay(2000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res1->pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res2->pipe));
    res1->is_end = true;
    res2->is_end = true;
    audio_el_inst_deinit(res1);
    audio_el_inst_deinit(res2);
    audio_el_res_deinit(res1);
    audio_el_res_deinit(res2);
}

TEST_CASE("Audio ENCODER Element Test", "[ESP_GMF_AUDIO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
    audio_el_res_cfg_t cfg = DEFAULT_SINGLE_IN_SINGLE_OUT_CONFIG();
    cfg.caps_cc = (uint64_t[]) {ESP_GMF_CAPS_AUDIO_ENCODER};
    audio_el_res_t *res = NULL;
    audio_el_res_init(&cfg, &res);
    res->config_func = encoder_config_callback;
    // Test encoder with different types
    uint32_t enc_types[] = {ESP_AUDIO_TYPE_AAC, ESP_AUDIO_TYPE_LC3, ESP_AUDIO_TYPE_OPUS, ESP_AUDIO_TYPE_SBC};
    // Test different input sizes
    uint32_t test_sizes[] = {120, 256, 512, 4096};
    // Test for run and stop
    for (int i = 0; i < sizeof(enc_types) / sizeof(enc_types[0]); i++) {
        for (int j = 0; j < sizeof(test_sizes) / sizeof(test_sizes[0]); j++) {
            audio_el_set_audio_info(res);
            res->in_inst[0].src_info.format_id = enc_types[i];
            res->in_inst[0].src_size = test_sizes[j];
            test_element_run_stop(res);
        }
    }
    // Test for run and finish
    for (int i = 0; i < sizeof(enc_types) / sizeof(enc_types[0]); i++) {
        for (int j = 0; j < sizeof(test_sizes) / sizeof(test_sizes[0]); j++) {
            audio_el_set_audio_info(res);
            res->in_inst[0].src_info.format_id = enc_types[i];
            res->in_inst[0].src_size = test_sizes[j];
            test_element_run_finish(res);
        }
    }
    // Test for run on process error
    test_element_run_error_process(res);
    // Test for run on open error
    audio_el_set_audio_info(res);
    res->config_func = NULL;
    esp_audio_enc_config_t *enc_cfg = OBJ_GET_CFG(res->current_hd[0]);
    enc_cfg->type = ESP_AUDIO_TYPE_UNSUPPORT;
    test_element_run_error_open(res);
    audio_el_res_deinit(res);
    // Test for run with multi task
    test_element_run_with_multi_task(&cfg, encoder_config_callback);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio DECODER Element Test", "[ESP_GMF_AUDIO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
    audio_el_res_cfg_t cfg = DEFAULT_SINGLE_IN_SINGLE_OUT_CONFIG();
    cfg.caps_cc = (uint64_t[]) {ESP_GMF_CAPS_AUDIO_DECODER};
    audio_el_res_t *res = NULL;
    audio_el_res_init(&cfg, &res);
    res->config_func = decoder_config_callback;
    // Test different source sizes
    uint32_t test_sizes[] = {119, 256, 512, 4096};
    // Test for run and stop
    for (int i = 0; i < sizeof(test_sizes) / sizeof(test_sizes[0]); i++) {
        audio_el_set_audio_info(res);
        res->in_inst[0].src_size = test_sizes[i];
        res->in_inst[0].is_sweep_signal = false;
        test_element_run_stop(res);
    }
    // Test for run and finish
    for (int i = 0; i < sizeof(test_sizes) / sizeof(test_sizes[0]); i++) {
        audio_el_set_audio_info(res);
        res->in_inst[0].src_size = test_sizes[i];
        res->in_inst[0].is_sweep_signal = false;
        test_element_run_finish(res);
    }
    // Test for run on process error
    test_element_run_error_process(res);
    // Test for run on open error
    res->config_func = NULL;
    esp_audio_simple_dec_cfg_t *dec_cfg = OBJ_GET_CFG(res->current_hd[0]);
    dec_cfg->dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
    test_element_run_error_open(res);
    audio_el_res_deinit(res);
    // Test for run with multi task
    test_element_run_with_multi_task(&cfg, decoder_config_callback);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio ALC Element Test", "[ESP_GMF_AUDIO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
    audio_el_res_cfg_t cfg = DEFAULT_SINGLE_IN_SINGLE_OUT_CONFIG();
    cfg.caps_cc = (uint64_t[]) {ESP_GMF_CAPS_AUDIO_ALC};
    audio_el_res_t *res = NULL;
    audio_el_res_init(&cfg, &res);
    res->config_func = alc_config_callback;
    // Test for run and stop
    audio_el_set_audio_info(res);
    test_element_run_stop(res);
    // Test for run and finish
    audio_el_set_audio_info(res);
    test_element_run_finish(res);
    // Test for run on open error
    test_element_run_error_open(res);
    // Test for run on process error
    test_element_run_error_process(res);
    audio_el_res_deinit(res);
    // Test for run with multi task
    test_element_run_with_multi_task(&cfg, alc_config_callback);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio EQ Element Test", "[ESP_GMF_AUDIO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
    audio_el_res_cfg_t cfg = DEFAULT_SINGLE_IN_SINGLE_OUT_CONFIG();
    cfg.caps_cc = (uint64_t[]) {ESP_GMF_CAPS_AUDIO_EQUALIZER};
    audio_el_res_t *res = NULL;
    audio_el_res_init(&cfg, &res);
    res->config_func = eq_config_callback;
    // Test for run and stop
    audio_el_set_audio_info(res);
    test_element_run_stop(res);
    // Test for run and finish
    audio_el_set_audio_info(res);
    test_element_run_finish(res);
    // Test for run on open error
    test_element_run_error_open(res);
    // Test for run on process error
    test_element_run_error_process(res);
    audio_el_res_deinit(res);
    // Test for run with multi task
    test_element_run_with_multi_task(&cfg, eq_config_callback);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio FADE Element Test", "[ESP_GMF_AUDIO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
    audio_el_res_cfg_t cfg = DEFAULT_SINGLE_IN_SINGLE_OUT_CONFIG();
    cfg.caps_cc = (uint64_t[]) {ESP_GMF_CAPS_AUDIO_FADE};
    audio_el_res_t *res = NULL;
    audio_el_res_init(&cfg, &res);
    res->config_func = fade_config_callback;
    // Test for run and stop
    audio_el_set_audio_info(res);
    test_element_run_stop(res);
    // Test for run and finish
    audio_el_set_audio_info(res);
    test_element_run_finish(res);
    // Test for run on open error
    test_element_run_error_open(res);
    // Test for run on process error
    test_element_run_error_process(res);
    audio_el_res_deinit(res);
    // Test for run with multi task
    test_element_run_with_multi_task(&cfg, fade_config_callback);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio SONIC Element Test", "[ESP_GMF_AUDIO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
    audio_el_res_cfg_t cfg = DEFAULT_SINGLE_IN_SINGLE_OUT_CONFIG();
    cfg.caps_cc = (uint64_t[]) {ESP_GMF_CAPS_AUDIO_SONIC};
    audio_el_res_t *res = NULL;
    audio_el_res_init(&cfg, &res);
    res->config_func = sonic_config_callback;
    // Test for run and stop
    audio_el_set_audio_info(res);
    test_element_run_stop(res);
    // Test for run and finish
    audio_el_set_audio_info(res);
    test_element_run_finish(res);
    // Test for run on open error
    test_element_run_error_open(res);
    // Test for run on process error
    test_element_run_error_process(res);
    audio_el_res_deinit(res);
    // Test for run with multi task
    test_element_run_with_multi_task(&cfg, sonic_config_callback);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio BIT_CVT Element Test", "[ESP_GMF_AUDIO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
    audio_el_res_cfg_t cfg = DEFAULT_SINGLE_IN_SINGLE_OUT_CONFIG();
    cfg.caps_cc = (uint64_t[]) {ESP_GMF_CAPS_AUDIO_BIT_CONVERT};
    audio_el_res_t *res = NULL;
    audio_el_res_init(&cfg, &res);
    res->config_func = bit_cvt_config_callback;
    // Test different source and destination bits
    uint32_t bit_pairs[][2] = {{16, 24}, {24, 16}, {16, 32}, {32, 16}};
    // Test for run and stop
    for (int i = 0; i < sizeof(bit_pairs) / sizeof(bit_pairs[0]); i++) {
        audio_el_set_audio_info(res);
        res->in_inst[0].src_info.bits = bit_pairs[i][0];
        res->out_inst[0].out_info.bits = bit_pairs[i][1];
        test_element_run_stop(res);
    }
    // Test for run and finish
    for (int i = 0; i < sizeof(bit_pairs) / sizeof(bit_pairs[0]); i++) {
        audio_el_set_audio_info(res);
        res->in_inst[0].src_info.bits = bit_pairs[i][0];
        res->out_inst[0].out_info.bits = bit_pairs[i][1];
        test_element_run_finish(res);
    }
    // Test for run on open error
    test_element_run_error_open(res);
    // Test for run on process error
    test_element_run_error_process(res);
    audio_el_res_deinit(res);
    // Test for run with multi task
    test_element_run_with_multi_task(&cfg, bit_cvt_config_callback);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio CH_CVT Element Test", "[ESP_GMF_AUDIO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
    audio_el_res_cfg_t cfg = DEFAULT_SINGLE_IN_SINGLE_OUT_CONFIG();
    cfg.caps_cc = (uint64_t[]) {ESP_GMF_CAPS_AUDIO_CHANNEL_CONVERT};
    audio_el_res_t *res = NULL;
    audio_el_res_init(&cfg, &res);
    res->config_func = ch_cvt_config_callback;
    // Test different source and destination channels
    uint32_t channel_pairs[][2] = {{1, 2}, {2, 1}, {2, 4}, {4, 2}};
    // Test for run and stop
    for (int i = 0; i < sizeof(channel_pairs) / sizeof(channel_pairs[0]); i++) {
        audio_el_set_audio_info(res);
        res->in_inst[0].src_info.channels = channel_pairs[i][0];
        res->out_inst[0].out_info.channels = channel_pairs[i][1];
        test_element_run_stop(res);
    }
    // Test for run and finish
    for (int i = 0; i < sizeof(channel_pairs) / sizeof(channel_pairs[0]); i++) {
        audio_el_set_audio_info(res);
        res->in_inst[0].src_info.channels = channel_pairs[i][0];
        res->out_inst[0].out_info.channels = channel_pairs[i][1];
        test_element_run_finish(res);
    }
    // Test for run on open error
    test_element_run_error_open(res);
    // Test for run on process error
    test_element_run_error_process(res);
    audio_el_res_deinit(res);
    // Test for run with multi task
    test_element_run_with_multi_task(&cfg, ch_cvt_config_callback);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio RATE_CVT Element Test", "[ESP_GMF_AUDIO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
    audio_el_res_cfg_t cfg = DEFAULT_SINGLE_IN_SINGLE_OUT_CONFIG();
    cfg.caps_cc = (uint64_t[]) {ESP_GMF_CAPS_AUDIO_RATE_CONVERT};
    audio_el_res_t *res = NULL;
    audio_el_res_init(&cfg, &res);
    res->config_func = rate_cvt_config_callback;
    // Test different source and destination rates
    uint32_t rate_pairs[][2] = {{44100, 48000}, {48000, 44100}, {44100, 16000}, {16000, 44100}};
    // Test for run and stop
    for (int i = 0; i < sizeof(rate_pairs) / sizeof(rate_pairs[0]); i++) {
        audio_el_set_audio_info(res);
        res->in_inst[0].src_info.sample_rates = rate_pairs[i][0];
        res->out_inst[0].out_info.sample_rates = rate_pairs[i][1];
        test_element_run_stop(res);
    }
    // Test for run and finish
    for (int i = 0; i < sizeof(rate_pairs) / sizeof(rate_pairs[0]); i++) {
        audio_el_set_audio_info(res);
        res->in_inst[0].src_info.sample_rates = rate_pairs[i][0];
        res->out_inst[0].out_info.sample_rates = rate_pairs[i][1];
        test_element_run_finish(res);
    }
    // Test for run on open error
    test_element_run_error_open(res);
    // Test for run on process error
    test_element_run_error_process(res);
    audio_el_res_deinit(res);
    // Test for run with multi task
    test_element_run_with_multi_task(&cfg, rate_cvt_config_callback);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio MIXER Element Test", "[ESP_GMF_AUDIO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
    audio_el_res_cfg_t cfg = DEFAULT_MULTI_IN_SINGLE_OUT_CONFIG();
    cfg.caps_cc = (uint64_t[]) {ESP_GMF_CAPS_AUDIO_MIXER};
    audio_el_res_t *res = NULL;
    audio_el_res_init(&cfg, &res);
    uint32_t src_size = 10 * TEST_AUDIO_SAMPLE_RATE * TEST_AUDIO_BITS * TEST_AUDIO_CHANNELS / 8000;
    res->config_func = mixer_config_callback;
    // Test for run and stop
    audio_el_set_audio_info(res);
    res->in_inst[0].src_size = src_size;
    res->in_inst[1].src_size = src_size;
    test_element_run_stop(res);
    // Test for run and finish
    audio_el_set_audio_info(res);
    res->in_inst[0].src_size = src_size;
    res->in_inst[1].src_size = src_size;
    test_element_run_finish(res);
    // Test for run on process error
    test_element_run_error_process(res);
    // Test for run on open error
    res->config_func = NULL;
    esp_ae_mixer_cfg_t *mix_cfg = OBJ_GET_CFG(res->current_hd[0]);
    mix_cfg->src_num = 0;
    test_element_run_error_open(res);
    audio_el_res_deinit(res);
    // Test for run with multi task
    cfg.src_size = src_size;
    test_element_run_with_multi_task(&cfg, mixer_config_callback);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio INTERLEAVE Element Test", "[ESP_GMF_AUDIO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
    audio_el_res_cfg_t cfg = DEFAULT_MULTI_IN_SINGLE_OUT_CONFIG();
    cfg.caps_cc = (uint64_t[]) {ESP_GMF_CAPS_AUDIO_INTERLEAVE};
    audio_el_res_t *res = NULL;
    audio_el_res_init(&cfg, &res);
    res->config_func = NULL;
    // Test for run and stop
    audio_el_set_audio_info(res);
    test_element_run_stop(res);
    // Test for run and finish
    audio_el_set_audio_info(res);
    test_element_run_finish(res);
    // Test for run on open error
    esp_gmf_interleave_cfg *interleave_cfg = OBJ_GET_CFG(res->current_hd[0]);
    interleave_cfg->src_num = 0;
    test_element_run_error_open(res);
    // Test for run on process error
    test_element_run_error_process(res);
    audio_el_res_deinit(res);
    // Test for run with multi task
    test_element_run_with_multi_task(&cfg, NULL);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio DEINTERLEAVE Element Test", "[ESP_GMF_AUDIO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
    audio_el_res_cfg_t cfg = DEFAULT_SINGLE_IN_MULTI_OUT_CONFIG();
    cfg.caps_cc = (uint64_t[]) {ESP_GMF_CAPS_AUDIO_DEINTERLEAVE};
    audio_el_res_t *res = NULL;
    audio_el_res_init(&cfg, &res);
    res->config_func = NULL;
    // Test for run and stop
    audio_el_set_audio_info(res);
    test_element_run_stop(res);
    // Test for run and finish
    audio_el_set_audio_info(res);
    test_element_run_finish(res);
    // Test for run on open error
    test_element_run_error_open(res);
    // Test for run on process error
    test_element_run_error_process(res);
    audio_el_res_deinit(res);
    // Test for run with multi task
    test_element_run_with_multi_task(&cfg, NULL);
    ESP_GMF_MEM_SHOW(TAG);
}

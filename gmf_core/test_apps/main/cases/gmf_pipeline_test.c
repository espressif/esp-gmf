/**
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_task.h"
#include "esp_gmf_audio_element.h"
#include "esp_gmf_pool.h"
#include "esp_fourcc.h"
#include "gmf_fake_io.h"

static const char *TAG = "TEST_ESP_GMF_PIPELINE";

#define DEFAULT_RUN_LOOPS (200)

typedef bool (*dependency_res_checker)(esp_gmf_pipeline_handle_t pipeline);

typedef struct {
    uint8_t open_order;
} pipeline_state_t;

typedef struct {
    esp_gmf_audio_element_t  parent;
    uint8_t                  open_order;
    uint8_t                  open_count;
    uint8_t                  close_count;
    int                      running_count;
    esp_gmf_info_sound_t     snd_info;
} general_el_t;

typedef struct {
    bool               is_dependent;
    bool               report_in_process;
    int                report_pos;
    pipeline_state_t  *state;
} general_el_cfg_t;

static esp_gmf_err_t general_el_init(general_el_cfg_t *config, esp_gmf_element_handle_t *handle);

static esp_gmf_err_t general_el_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return general_el_init(cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_job_err_t general_el_open(esp_gmf_element_handle_t self, void *para)
{
    general_el_t *el = (general_el_t *)self;
    general_el_cfg_t *cfg = (general_el_cfg_t *)OBJ_GET_CFG(self);
    el->open_order = cfg->state->open_order++;
    el->open_count++;
    if (cfg->is_dependent && cfg->report_in_process == false) {
        // Notify dependency to next
        esp_gmf_element_notify_snd_info(self, &el->snd_info);
    }
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t general_el_close(esp_gmf_element_handle_t self, void *para)
{
    general_el_t *el = (general_el_t *)self;
    el->close_count++;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t general_el_process(esp_gmf_element_handle_t self, void *para)
{
    general_el_t *el = (general_el_t *)self;
    general_el_cfg_t *cfg = (general_el_cfg_t *)OBJ_GET_CFG(self);
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    int bytes = ESP_GMF_ELEMENT_GET(el)->in_attr.data_size;
    esp_gmf_err_io_t load_ret = esp_gmf_port_acquire_in(in_port, &in_load, bytes, ESP_GMF_MAX_DELAY);
    do {
        ESP_GMF_PORT_CHECK(TAG, load_ret, out_len, break, "Failed to acquire in, ret: %d", load_ret);

        int running_count = el->running_count;
        el->running_count++;
        if ((el->open_order == 0) && (el->running_count >= DEFAULT_RUN_LOOPS)) {
            in_load->is_done = true;
        }
        if (cfg->is_dependent && cfg->report_in_process) {
            if (running_count < cfg->report_pos) {
                // No need report for all data consumed
                out_len = ESP_GMF_JOB_ERR_CONTINUE;
                break;
            }
            if (running_count == cfg->report_pos) {
                // Report info to next
                esp_gmf_element_notify_snd_info(self, &el->snd_info);
            }
        }
        load_ret = esp_gmf_port_acquire_out(out_port, &out_load, in_load->buf_length, ESP_GMF_MAX_DELAY);
        ESP_GMF_PORT_CHECK(TAG, load_ret, out_len, break, "Failed to acquire out, ret: %d", load_ret);
        out_load->pts = in_load->pts;
        out_load->is_done = in_load->is_done;
        if (in_load->is_done) {
            out_len = ESP_GMF_JOB_ERR_DONE;
        }
    } while (0);
    if (out_load != NULL) {
        esp_gmf_port_release_out(out_port, out_load, ESP_GMF_MAX_DELAY);
    }
    if (in_load != NULL) {
        esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY);
    }
    return out_len;
}

static esp_gmf_err_t general_el_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
{
    ESP_GMF_NULL_CHECK(TAG, ctx, { return ESP_GMF_ERR_INVALID_ARG; });
    ESP_GMF_NULL_CHECK(TAG, evt, { return ESP_GMF_ERR_INVALID_ARG; });
    if ((evt->type != ESP_GMF_EVT_TYPE_REPORT_INFO)
        || (evt->sub != ESP_GMF_INFO_SOUND)
        || (evt->payload == NULL)) {
        return ESP_GMF_ERR_OK;
    }
    esp_gmf_element_handle_t self = (esp_gmf_element_handle_t)ctx;
    esp_gmf_event_state_t state = ESP_GMF_EVENT_STATE_NONE;
    esp_gmf_element_get_state(self, &state);
    general_el_t *el = (general_el_t *)self;
    memcpy(&el->snd_info, evt->payload, sizeof(esp_gmf_info_sound_t));
    if (state == ESP_GMF_EVENT_STATE_NONE) {
        esp_gmf_element_set_state(self, ESP_GMF_EVENT_STATE_INITIALIZED);
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t general_el_destroy(esp_gmf_element_handle_t self)
{
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(self);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t general_el_init(general_el_cfg_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG; });
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    general_el_t *el = esp_gmf_oal_calloc(1, sizeof(general_el_t));
    ESP_GMF_MEM_VERIFY(
        TAG, el, { return ESP_GMF_ERR_MEMORY_LACK; }, "General element", sizeof(general_el_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)el;
    obj->new_obj = general_el_new;
    obj->del_obj = general_el_destroy;
    esp_gmf_obj_set_tag(obj, "general");
    esp_gmf_element_cfg_t el_cfg = { 0 };
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = config->is_dependent;
    general_el_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(general_el_cfg_t));
    ESP_GMF_MEM_VERIFY(
        TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto GENERAL_EL_FAIL; }, "general element configuration", sizeof(general_el_cfg_t));
    esp_gmf_obj_set_config(obj, cfg, sizeof(general_el_cfg_t));
    if (config) {
        memcpy(cfg, config, sizeof(general_el_cfg_t));
    }
    ret = esp_gmf_audio_el_init(el, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto GENERAL_EL_FAIL, "Failed to initialize general element");
    ESP_GMF_ELEMENT_GET(el)->ops.open = general_el_open;
    ESP_GMF_ELEMENT_GET(el)->ops.process = general_el_process;
    ESP_GMF_ELEMENT_GET(el)->ops.close = general_el_close;
    ESP_GMF_ELEMENT_GET(el)->ops.event_receiver = general_el_event_handler;
    *handle = obj;
    return ESP_GMF_ERR_OK;
GENERAL_EL_FAIL:
    general_el_destroy(obj);
    return ret;
}

esp_gmf_err_t pipeline_event(esp_gmf_event_pkt_t *pkt, void *ctx)
{
    if (pkt->type == ESP_GMF_EVT_TYPE_CHANGE_STATE && (pkt->sub == ESP_GMF_EVENT_STATE_FINISHED || pkt->sub == ESP_GMF_EVENT_STATE_ERROR)) {
        bool *running = (bool *)ctx;
        *running = false;
    }
    return ESP_GMF_ERR_OK;
}

static int pipeline_dependency_test(general_el_cfg_t *cfg, int n, dependency_res_checker checker)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);

    // Init for pool
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);
    char el_name[n][2];
    for (int i = 0; i < n; i++) {
        esp_gmf_element_handle_t el = NULL;
        general_el_init(cfg + i, &el);
        TEST_ASSERT_NOT_NULL(el);
        snprintf(el_name[i], 2, "%c", 'A' + i);
        esp_gmf_pool_register_element(pool, el, el_name[i]);
    }
    fake_io_cfg_t io_cfg = FAKE_IO_CFG_DEFAULT();
    io_cfg.dir = ESP_GMF_IO_DIR_READER;
    esp_gmf_io_handle_t reader = NULL;
    fake_io_init(&io_cfg, &reader);
    TEST_ASSERT_NOT_NULL(reader);
    esp_gmf_pool_register_io(pool, reader, "io_in");

    esp_gmf_io_handle_t writer = NULL;
    io_cfg.dir = ESP_GMF_IO_DIR_WRITER;
    fake_io_init(&io_cfg, &writer);
    TEST_ASSERT_NOT_NULL(writer);
    esp_gmf_pool_register_io(pool, writer, "io_out");

    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *el_name_array[n];
    for (int i = 0; i < n; i++) {
        el_name_array[i] = el_name[i];
    }
    esp_gmf_pool_new_pipeline(pool, "io_in", el_name_array, n, "io_out", &pipe);
    bool running = true;

    esp_gmf_pipeline_set_event(pipe, pipeline_event, &running);

    esp_gmf_task_cfg_t task_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    task_cfg.ctx = NULL;
    task_cfg.cb = NULL;
    esp_gmf_task_handle_t work_task = NULL;
    esp_gmf_task_init(&task_cfg, &work_task);
    TEST_ASSERT_NOT_NULL(work_task);
    esp_gmf_pipeline_bind_task(pipe, work_task);

    esp_gmf_info_sound_t info = {
        .sample_rates = 16000,
        .channels = 2,
        .bits = 16,
        .format_id = ESP_FOURCC_PCM,
    };
    esp_gmf_pipeline_report_info(pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));
    int timeout = 10;
    while (timeout-- > 0 && running) {
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe));
    if (checker) {
        TEST_ASSERT_TRUE(checker(pipe));
    }
    esp_gmf_task_deinit(work_task);
    esp_gmf_pipeline_destroy(pipe);
    esp_gmf_pool_deinit(pool);
    ESP_GMF_MEM_SHOW(TAG);
    return 0;
}

static bool general_dependency_check(esp_gmf_pipeline_handle_t pipeline)
{
    esp_gmf_element_handle_t iter = NULL;
    esp_gmf_pipeline_get_head_el(pipeline, &iter);
    general_el_t *head = (general_el_t *)iter;
    esp_gmf_element_handle_t next = NULL;
    uint8_t open_order = 0;
    if (head->open_order != open_order) {
        ESP_LOGE(TAG, "Head order wrong as %d", head->open_order);
        return false;
    }
    general_el_cfg_t *cfg = (general_el_cfg_t *)OBJ_GET_CFG(head);
    ESP_LOGI(TAG, "%s open_count %d close_count %d running_count %d order %d", OBJ_GET_TAG(head),
             head->open_count, head->close_count, head->running_count,
             head->open_order);
    int skip_count = cfg->report_pos;
    while (esp_gmf_pipeline_get_next_el(pipeline, iter, &next) == ESP_GMF_ERR_OK) {
        // Verify results all same
        general_el_t *el = (general_el_t *)next;
        cfg = (general_el_cfg_t *)OBJ_GET_CFG(next);
        ESP_LOGI(TAG, "%s open_count %d close_count %d running_count %d order %d", OBJ_GET_TAG(el),
                 el->open_count, el->close_count, el->running_count,
                 el->open_order);
        open_order++;
        general_el_t *next_el = (general_el_t *)next;
        if (next_el->open_order != open_order) {
            ESP_LOGE(TAG, "%s order wrong as %d", OBJ_GET_TAG(next), next_el->open_order);
            return false;
        }
        if ((el->open_count != 1) ||
            (el->close_count != 1) ||
            (el->running_count + skip_count != head->running_count)) {
            ESP_LOGE(TAG, "Failed check for %s", OBJ_GET_TAG(el));
            return false;
        }
        skip_count += cfg->report_pos;
        iter = next;
    }
    return true;
}

TEST_CASE("A - B - C - D (no dependency)", "[ESP_GMF_PIPELINE]")
{
    pipeline_state_t state = {};
    general_el_cfg_t cfg[4] = {};
    for (int i = 0; i < 4; i++) {
        cfg[i].state = &state;
    }
    pipeline_dependency_test(cfg, 4, general_dependency_check);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("A - B - C - D (all has dependency)", "[ESP_GMF_PIPELINE]")
{
    pipeline_state_t state = {};
    general_el_cfg_t cfg[4] = {};
    for (int i = 0; i < 4; i++) {
        cfg[i].is_dependent = true;
        cfg[i].state = &state;
    }
    pipeline_dependency_test(cfg, 4, general_dependency_check);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("A(Y) - B(Y) - C(N) - D(Y) (C no dependency)", "[ESP_GMF_PIPELINE]")
{
    pipeline_state_t state = {};
    general_el_cfg_t cfg[4] = {};
    for (int i = 0; i < 4; i++) {
        if ('A' + i == 'C') {
            cfg[i].is_dependent = false;
        } else {
            cfg[i].is_dependent = true;
        }
        cfg[i].state = &state;
    }
    pipeline_dependency_test(cfg, 4, general_dependency_check);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("A(Y) - B(N) - C(Y) - D(N) (B,D no dependency)", "[ESP_GMF_PIPELINE]")
{
    pipeline_state_t state = {};
    general_el_cfg_t cfg[4] = {};
    for (int i = 0; i < 4; i++) {
        if ('A' + i == 'B' || 'A' + i == 'D') {
            cfg[i].is_dependent = false;
        } else {
            cfg[i].is_dependent = true;
        }
        cfg[i].state = &state;
    }
    pipeline_dependency_test(cfg, 4, general_dependency_check);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("A(N) - B(Y) - C(N) - D(Y) (A,C no dependency)", "[ESP_GMF_PIPELINE]")
{
    pipeline_state_t state = {};
    general_el_cfg_t cfg[4] = {};
    for (int i = 0; i < 4; i++) {
        if ('A' + i == 'B' || 'A' + i == 'D') {
            cfg[i].is_dependent = true;
        } else {
            cfg[i].is_dependent = false;
        }
        cfg[i].state = &state;
    }
    pipeline_dependency_test(cfg, 4, general_dependency_check);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("A(Y) - B(N) - C(Y) - D(N) (Report in middle)", "[ESP_GMF_PIPELINE]")
{
    pipeline_state_t state = {};
    general_el_cfg_t cfg[4] = {};
    for (int i = 0; i < 4; i++) {
        if ('A' + i == 'A') {
            cfg[i].is_dependent = true;
            cfg[i].report_in_process = true;
            cfg[i].report_pos = 2;
        } else if ('A' + i == 'C') {
            cfg[i].is_dependent = true;
            cfg[i].report_in_process = true;
            cfg[i].report_pos = 3;
        }
        cfg[i].state = &state;
    }
    pipeline_dependency_test(cfg, 4, general_dependency_check);
    ESP_GMF_MEM_SHOW(TAG);
}

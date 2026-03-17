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
#include "gmf_general_el.h"
#include "esp_gmf_event.h"

static const char *TAG = "TEST_ESP_GMF_PIPELINE";

typedef struct {
    esp_gmf_pool_handle_t      pool;
    esp_gmf_pipeline_handle_t  pipe;
    esp_gmf_task_handle_t      work_task;
    esp_gmf_io_handle_t        reader;
    esp_gmf_io_handle_t        writer;
    bool                       running;
} pipeline_test_ctx_t;

typedef struct {
    uint8_t  abort_action;
    uint8_t  finish_action;
} pipeline_strategy_ctx_t;

esp_gmf_err_t pipeline_event(esp_gmf_event_pkt_t *pkt, void *ctx)
{
    ESP_LOGI(TAG, "pipeline_event, type: %d, sub: %s", pkt->type, esp_gmf_event_get_state_str(pkt->sub));
    if (pkt->type == ESP_GMF_EVT_TYPE_CHANGE_STATE &&
        (pkt->sub == ESP_GMF_EVENT_STATE_FINISHED ||
         pkt->sub == ESP_GMF_EVENT_STATE_STOPPED ||
         pkt->sub == ESP_GMF_EVENT_STATE_ERROR)) {
        bool *running = (bool *)ctx;
        *running = false;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t pipeline_strategy_func(esp_gmf_task_handle_t handle, uint8_t strategy_type, void *ctx, uint8_t *out_action)
{
    pipeline_strategy_ctx_t *strategy_ctx = (pipeline_strategy_ctx_t *)ctx;
    if (strategy_type == GMF_TASK_STRATEGY_TYPE_ABORT) {
        *out_action = strategy_ctx->abort_action;
        if (strategy_ctx->abort_action != GMF_TASK_STRATEGY_ACTION_DEFAULT) {
            strategy_ctx->abort_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
        }
    } else if (strategy_type == GMF_TASK_STRATEGY_TYPE_FINISH) {
        *out_action = strategy_ctx->finish_action;
        if (strategy_ctx->finish_action != GMF_TASK_STRATEGY_ACTION_DEFAULT) {
            strategy_ctx->finish_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
        }
    } else {
        *out_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
    }
    return ESP_GMF_ERR_OK;
}

static bool pipeline_api_count_check(esp_gmf_pipeline_handle_t pipeline, int expected_reset_count, int expected_running_count)
{
    esp_gmf_element_handle_t iter = NULL;
    esp_gmf_pipeline_get_head_el(pipeline, &iter);
    while (iter) {
        general_el_t *el = (general_el_t *)iter;
        ESP_LOGI(TAG, "%s open_count %d close_count %d reset_count %d running_count %d", OBJ_GET_TAG(el),
                 el->open_count, el->close_count, el->reset_count, el->running_count);
        if (el->reset_count < expected_reset_count) {
            ESP_LOGE(TAG, "%s reset_count wrong, expected >= %d, got %d", OBJ_GET_TAG(el), expected_reset_count, el->reset_count);
            return false;
        }
        if (el->open_count != 1) {
            ESP_LOGE(TAG, "%s open_count wrong, expected 1, got %d", OBJ_GET_TAG(el), el->open_count);
            return false;
        }
        if (el->close_count != 1) {
            ESP_LOGE(TAG, "%s close_count wrong, expected 1, got %d", OBJ_GET_TAG(el), el->close_count);
            return false;
        }
        if (el->running_count < expected_running_count) {
            ESP_LOGE(TAG, "%s running_count wrong, expected >= %d, got %d", OBJ_GET_TAG(el), expected_running_count, el->running_count);
            return false;
        }
        esp_gmf_element_handle_t next = NULL;
        if (esp_gmf_pipeline_get_next_el(pipeline, iter, &next) != ESP_GMF_ERR_OK) {
            break;
        }
        iter = next;
    }
    return true;
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

static void pipeline_test_setup(pipeline_test_ctx_t *ctx, general_el_cfg_t *cfg, int el_count,
                                esp_gmf_event_cb event_cb, pipeline_strategy_ctx_t *strategy_ctx)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);

    esp_gmf_pool_init(&ctx->pool);
    TEST_ASSERT_NOT_NULL(ctx->pool);
    char el_name[el_count][2];
    for (int i = 0; i < el_count; i++) {
        esp_gmf_element_handle_t el = NULL;
        general_el_init(cfg + i, &el);
        TEST_ASSERT_NOT_NULL(el);
        snprintf(el_name[i], 2, "%c", 'A' + i);
        esp_gmf_pool_register_element(ctx->pool, el, el_name[i]);
    }
    fake_io_cfg_t io_cfg = FAKE_IO_CFG_DEFAULT();
    io_cfg.dir = ESP_GMF_IO_DIR_READER;
    esp_gmf_io_handle_t reader = NULL;
    esp_gmf_err_t ret = fake_io_init(&io_cfg, &reader);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);
    TEST_ASSERT_NOT_NULL(reader);
    esp_gmf_pool_register_io(ctx->pool, reader, "io_in");

    io_cfg.dir = ESP_GMF_IO_DIR_WRITER;
    esp_gmf_io_handle_t writer = NULL;
    ret = fake_io_init(&io_cfg, &writer);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);
    TEST_ASSERT_NOT_NULL(writer);
    esp_gmf_pool_register_io(ctx->pool, writer, "io_out");

    const char *el_name_array[el_count];
    for (int i = 0; i < el_count; i++) {
        el_name_array[i] = el_name[i];
    }
    esp_gmf_pool_new_pipeline(ctx->pool, "io_in", el_name_array, el_count, "io_out", &ctx->pipe);
    ctx->running = true;

    esp_gmf_pipeline_get_in(ctx->pipe, &ctx->reader);
    esp_gmf_pipeline_get_out(ctx->pipe, &ctx->writer);

    esp_gmf_pipeline_set_event(ctx->pipe, event_cb, &ctx->running);

    esp_gmf_task_cfg_t task_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    task_cfg.ctx = NULL;
    task_cfg.cb = NULL;
    esp_gmf_task_init(&task_cfg, &ctx->work_task);
    TEST_ASSERT_NOT_NULL(ctx->work_task);
    esp_gmf_pipeline_bind_task(ctx->pipe, ctx->work_task);
    esp_gmf_pipeline_loading_jobs(ctx->pipe);

    // Set strategy function if provided
    if (strategy_ctx) {
        esp_gmf_task_set_strategy_func(ctx->work_task, pipeline_strategy_func, strategy_ctx);
    }

    esp_gmf_info_sound_t info = {
        .sample_rates = 16000,
        .channels = 2,
        .bits = 16,
        .format_id = ESP_FOURCC_PCM,
    };
    esp_gmf_pipeline_report_info(ctx->pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));
}

static void pipeline_test_cleanup(pipeline_test_ctx_t *ctx)
{
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(ctx->pipe));
    esp_gmf_task_deinit(ctx->work_task);
    esp_gmf_pipeline_destroy(ctx->pipe);
    esp_gmf_pool_deinit(ctx->pool);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("A - B - C - D (no dependency)", "[ESP_GMF_PIPELINE]")
{
    pipeline_state_t state = {};
    general_el_cfg_t cfg[4] = {};
    for (int i = 0; i < 4; i++) {
        cfg[i].state = &state;
    }
    pipeline_test_ctx_t ctx = {};
    pipeline_test_setup(&ctx, cfg, 4, pipeline_event, NULL);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(ctx.pipe));
    int timeout = 10;
    while (timeout-- > 0 && ctx.running) {
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(ctx.pipe));
    TEST_ASSERT_TRUE(general_dependency_check(ctx.pipe));

    pipeline_test_cleanup(&ctx);
}

TEST_CASE("A - B - C - D (all has dependency)", "[ESP_GMF_PIPELINE]")
{
    pipeline_state_t state = {};
    general_el_cfg_t cfg[4] = {};
    for (int i = 0; i < 4; i++) {
        cfg[i].is_dependent = true;
        cfg[i].state = &state;
    }
    pipeline_test_ctx_t ctx = {};
    pipeline_test_setup(&ctx, cfg, 4, pipeline_event, NULL);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(ctx.pipe));
    int timeout = 10;
    while (timeout-- > 0 && ctx.running) {
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(ctx.pipe));
    TEST_ASSERT_TRUE(general_dependency_check(ctx.pipe));

    pipeline_test_cleanup(&ctx);
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
    pipeline_test_ctx_t ctx = {};
    pipeline_test_setup(&ctx, cfg, 4, pipeline_event, NULL);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(ctx.pipe));
    int timeout = 10;
    while (timeout-- > 0 && ctx.running) {
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(ctx.pipe));
    TEST_ASSERT_TRUE(general_dependency_check(ctx.pipe));

    pipeline_test_cleanup(&ctx);
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
    pipeline_test_ctx_t ctx = {};
    pipeline_test_setup(&ctx, cfg, 4, pipeline_event, NULL);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(ctx.pipe));
    int timeout = 10;
    while (timeout-- > 0 && ctx.running) {
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(ctx.pipe));
    TEST_ASSERT_TRUE(general_dependency_check(ctx.pipe));

    pipeline_test_cleanup(&ctx);
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
    pipeline_test_ctx_t ctx = {};
    pipeline_test_setup(&ctx, cfg, 4, pipeline_event, NULL);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(ctx.pipe));
    int timeout = 10;
    while (timeout-- > 0 && ctx.running) {
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(ctx.pipe));
    TEST_ASSERT_TRUE(general_dependency_check(ctx.pipe));

    pipeline_test_cleanup(&ctx);
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
    pipeline_test_ctx_t ctx = {};
    pipeline_test_setup(&ctx, cfg, 4, pipeline_event, NULL);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(ctx.pipe));
    int timeout = 10;
    while (timeout-- > 0 && ctx.running) {
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(ctx.pipe));
    TEST_ASSERT_TRUE(general_dependency_check(ctx.pipe));

    pipeline_test_cleanup(&ctx);
}

TEST_CASE("Pipeline finish with strategy RESET action", "[ESP_GMF_PIPELINE]")
{
    pipeline_state_t state = {};
    general_el_cfg_t cfg[4] = {};
    for (int i = 0; i < 4; i++) {
        cfg[i].state = &state;
    }
    pipeline_strategy_ctx_t strategy_ctx = {
        .abort_action = GMF_TASK_STRATEGY_ACTION_DEFAULT,
        .finish_action = GMF_TASK_STRATEGY_ACTION_RESET,
    };
    pipeline_test_ctx_t ctx = {};
    pipeline_test_setup(&ctx, cfg, 4, pipeline_event, &strategy_ctx);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(ctx.pipe));
    int timeout = 15;
    while (timeout-- > 0) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        // Check if reset has been called
        esp_gmf_element_handle_t iter = NULL;
        esp_gmf_pipeline_get_head_el(ctx.pipe, &iter);
        if (iter) {
            general_el_t *el = (general_el_t *)iter;
            if (el->reset_count > 0) {
                break;
            }
        }
    }
    // Wait a bit more for reset jobs to complete
    vTaskDelay(500 / portTICK_PERIOD_MS);
    pipeline_api_count_check(ctx.pipe, 1, 1);

    pipeline_test_cleanup(&ctx);
}

TEST_CASE("Pipeline abort with strategy RESET action", "[ESP_GMF_PIPELINE]")
{
    pipeline_state_t state = {};
    general_el_cfg_t cfg[4] = {};
    for (int i = 0; i < 4; i++) {
        cfg[i].state = &state;
    }
    pipeline_strategy_ctx_t strategy_ctx = {
        .abort_action = GMF_TASK_STRATEGY_ACTION_RESET,
        .finish_action = GMF_TASK_STRATEGY_ACTION_DEFAULT,
    };

    pipeline_test_ctx_t ctx = {};
    pipeline_test_setup(&ctx, cfg, 4, pipeline_event, &strategy_ctx);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(ctx.pipe));

    vTaskDelay(50 / portTICK_PERIOD_MS);

    // Set fake_io to return ABORT to trigger abort strategy
    fake_io_t *fake_reader = (fake_io_t *)ctx.reader;
    fake_reader->acquire_read_return = ESP_GMF_IO_ABORT;
    ESP_LOGI(TAG, "Set fake_io to return ABORT");

    int timeout = 15;
    esp_gmf_element_handle_t head_el = NULL;
    esp_gmf_pipeline_get_head_el(ctx.pipe, &head_el);
    while (timeout-- > 0) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        if (head_el) {
            general_el_t *el = (general_el_t *)head_el;
            if (el->reset_count >= 1) {
                break;
            }
        }
    }
    fake_reader->acquire_read_return = ESP_GMF_IO_OK;
    vTaskDelay(200 / portTICK_PERIOD_MS);
    pipeline_api_count_check(ctx.pipe, 1, 1);

    pipeline_test_cleanup(&ctx);
}

TEST_CASE("Pipeline finish with strategy DEFAULT action", "[ESP_GMF_PIPELINE]")
{
    pipeline_state_t state = {};
    general_el_cfg_t cfg[4] = {};
    for (int i = 0; i < 4; i++) {
        cfg[i].state = &state;
    }
    pipeline_strategy_ctx_t strategy_ctx = {
        .abort_action = GMF_TASK_STRATEGY_ACTION_DEFAULT,
        .finish_action = GMF_TASK_STRATEGY_ACTION_DEFAULT,
    };
    pipeline_test_ctx_t ctx = {};
    pipeline_test_setup(&ctx, cfg, 4, pipeline_event, &strategy_ctx);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(ctx.pipe));
    int timeout = 15;
    while (timeout-- > 0 && ctx.running) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    // Wait a bit more for state transition to complete
    vTaskDelay(200 / portTICK_PERIOD_MS);

    pipeline_api_count_check(ctx.pipe, 0, 0);

    pipeline_test_cleanup(&ctx);
}

TEST_CASE("Pipeline abort with strategy DEFAULT action", "[ESP_GMF_PIPELINE]")
{
    pipeline_state_t state = {};
    general_el_cfg_t cfg[4] = {};
    for (int i = 0; i < 4; i++) {
        cfg[i].state = &state;
    }
    pipeline_strategy_ctx_t strategy_ctx = {
        .abort_action = GMF_TASK_STRATEGY_ACTION_DEFAULT,
        .finish_action = GMF_TASK_STRATEGY_ACTION_DEFAULT,
    };
    pipeline_test_ctx_t ctx = {};
    pipeline_test_setup(&ctx, cfg, 4, pipeline_event, &strategy_ctx);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(ctx.pipe));

    vTaskDelay(50 / portTICK_PERIOD_MS);

    // Set fake_io to return ABORT to trigger abort strategy
    fake_io_t *fake_reader = (fake_io_t *)ctx.reader;
    fake_reader->acquire_read_return = ESP_GMF_IO_ABORT;

    // Wait for abort to trigger DEFAULT action (should stop pipeline)
    int timeout = 15;
    while (timeout-- > 0 && ctx.running) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    // Wait a bit more for state transition to complete
    vTaskDelay(200 / portTICK_PERIOD_MS);
    pipeline_api_count_check(ctx.pipe, 0, 0);

    pipeline_test_cleanup(&ctx);
}

TEST_CASE("Pipeline failed on IO OPEN the reuse the pipeline", "[ESP_GMF_PIPELINE]")
{
    pipeline_state_t state = {};
    general_el_cfg_t cfg[2] = {};
    for (int i = 0; i < 2; i++) {
        cfg[i].state = &state;
    }
    pipeline_test_ctx_t ctx = {};
    pipeline_test_setup(&ctx, cfg, 2, pipeline_event, NULL);
    fake_io_t *fake_reader = (fake_io_t *)ctx.reader;
    fake_reader->open_return = ESP_GMF_ERR_FAIL;

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(ctx.pipe));
    vTaskDelay(50 / portTICK_PERIOD_MS);

    esp_gmf_element_handle_t head_el = NULL;
    esp_gmf_pipeline_get_head_el(ctx.pipe, &head_el);
    general_el_t *el = (general_el_t *)head_el;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, el->open_count, "Element should not be opened if IO open fails");

    esp_gmf_pipeline_reset(ctx.pipe);
    fake_reader->open_return = ESP_GMF_JOB_ERR_OK;

    // Loading jobs again
    // It's due to user not easy to distinguish the pipeline is failed or not
    // If it's not failed, the loading jobs is must be called to make the pipeline have jobs to run
    // So we need to loading jobs again to make the pipeline running whatever the pipeline is failed or not
    esp_gmf_pipeline_loading_jobs(ctx.pipe);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(ctx.pipe));

    int timeout = 15;
    while (timeout-- > 0 && ctx.running) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    vTaskDelay(200 / portTICK_PERIOD_MS);
    pipeline_api_count_check(ctx.pipe, 0, 1);

    pipeline_test_cleanup(&ctx);
}

TEST_CASE("GMF Pipeline Iterate Element test", "[ESP_GMF_PIPELINE]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);

    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);

    general_el_cfg_t cfg = {};
    esp_gmf_element_handle_t el1 = NULL, el2 = NULL, el3 = NULL, el4 = NULL;
    general_el_init(&cfg, &el1);
    general_el_init(&cfg, &el2);
    general_el_init(&cfg, &el3);
    general_el_init(&cfg, &el4);
    esp_gmf_pool_register_element(pool, el1, "aud_dec");
    esp_gmf_pool_register_element(pool, el2, "aud_rate_cvt");
    esp_gmf_pool_register_element(pool, el3, "aud_rate_cvt");
    esp_gmf_pool_register_element(pool, el4, "aud_bit_cvt");

    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *names[] = {"aud_dec", "aud_rate_cvt", "aud_rate_cvt", "aud_bit_cvt"};
    esp_gmf_pool_new_pipeline(pool, NULL, names, 4, NULL, &pipe);
    TEST_ASSERT_NOT_NULL(pipe);

    // iterate all elements
    int count_all = 0;
    const void *iterator = NULL;
    esp_gmf_element_handle_t cur_el = NULL;
    while (esp_gmf_pipeline_iterate_element(pipe, &iterator, &cur_el) == ESP_GMF_ERR_OK) {
        count_all++;
        TEST_ASSERT_NOT_NULL(cur_el);
    }
    TEST_ASSERT_EQUAL(4, count_all);

    // iterate and filter by tag in application layer
    int count_tag = 0;
    iterator = NULL;
    cur_el = NULL;
    while (esp_gmf_pipeline_iterate_element(pipe, &iterator, &cur_el) == ESP_GMF_ERR_OK) {
        TEST_ASSERT_NOT_NULL(cur_el);
        char *el_tag = NULL;
        esp_gmf_obj_get_tag((esp_gmf_obj_handle_t)cur_el, &el_tag);
        if (el_tag && strcasecmp(el_tag, "aud_rate_cvt") == 0) {
            count_tag++;
        }
    }
    TEST_ASSERT_EQUAL(2, count_tag);

    esp_gmf_pipeline_destroy(pipe);
    esp_gmf_pool_deinit(pool);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Pipeline pause on start test", "[ESP_GMF_PIPELINE]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);

    pipeline_state_t state = {};
    general_el_cfg_t cfg[2] = {};
    for (int i = 0; i < 2; i++) {
        cfg[i].state = &state;
    }

    // 1. Verify normal run (pipeline runs normally)
    pipeline_test_ctx_t ctx1 = {};
    pipeline_test_setup(&ctx1, cfg, 2, pipeline_event, NULL);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(ctx1.pipe));

    vTaskDelay(100 / portTICK_PERIOD_MS);
    esp_gmf_element_handle_t iter1 = NULL;
    esp_gmf_pipeline_get_head_el(ctx1.pipe, &iter1);
    general_el_t *el1 = (general_el_t *)iter1;
    TEST_ASSERT_GREATER_THAN(0, el1->running_count);
    pipeline_test_cleanup(&ctx1);

    // 2. Verify pause on start
    pipeline_test_ctx_t ctx2 = {};
    pipeline_test_setup(&ctx2, cfg, 2, pipeline_event, NULL);
    // Set pause on start
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_pause_on_start(ctx2.pipe, true));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(ctx2.pipe));

    vTaskDelay(100 / portTICK_PERIOD_MS);
    esp_gmf_element_handle_t iter2 = NULL;
    esp_gmf_pipeline_get_head_el(ctx2.pipe, &iter2);
    general_el_t *el2 = (general_el_t *)iter2;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, el2->running_count, "Element shouldn't be running when pause_on_start is set");

    // Verify it resumes normally
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_resume(ctx2.pipe));
    // Wait for the pipeline to start running
    vTaskDelay(100 / portTICK_PERIOD_MS);
    TEST_ASSERT_GREATER_THAN(0, el2->running_count);
    pipeline_test_cleanup(&ctx2);
}

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_random.h"
#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_thread.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_audio_dec.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_dec_reg.h"
#include "esp_gmf_new_databus.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_app_unit_test.h"
#include "esp_gmf_audio_helper.h"
#include "gmf_audio_play_com.h"
#include "esp_gmf_io_http.h"
#include "esp_gmf_port.h"
#include "esp_gmf_audio_param.h"
#include "esp_gmf_io_file.h"
#include "esp_gmf_io_codec_dev.h"
#include "esp_gmf_payload.h"
#include "esp_fourcc.h"

#define PIPELINE_BLOCK_BIT           BIT(0)
#define PIPELINE_STRATEGY_ABORT_BIT  BIT(1)
#define PIPELINE_STRATEGY_FINISH_BIT BIT(2)

/**
 * @brief  Message structure to communicate pipeline strategy events
 */
typedef struct {
    uint8_t  strategy_type;  /*!< GMF_TASK_STRATEGY_TYPE_ABORT or GMF_TASK_STRATEGY_TYPE_FINISH */
    uint8_t  pipeline_id;    /*!< Pipeline identifier */
} pipeline_strategy_msg_t;

/**
 * @brief  Message structure for actions sent from the strategy management task to pipelines
 */
typedef struct {
    uint8_t  action;  /*!< GMF_TASK_STRATEGY_ACTION_DEFAULT or GMF_TASK_STRATEGY_ACTION_RESET */
} pipeline_strategy_action_msg_t;

/**
 * @brief  Context structure for a single pipeline managed by the strategy manager
 */
typedef struct {
    esp_gmf_io_handle_t        io;            /*!< IO handle */
    esp_gmf_db_handle_t        db;            /*!< Databus handle */
    QueueHandle_t              action_queue;  /*!< Queue to receive action from manage task */
    uint8_t                    pipeline_id;   /*!< Pipeline identifier */
    esp_gmf_pipeline_handle_t  pipeline;      /*!< Pipeline to pause/resume */
} pipeline_strategy_ctx_t;

/**
 * @brief  Strategy management context to coordinate multiple pipelines
 */
typedef struct {
    QueueHandle_t  *strategy_queues;  /*!< Array of queues to receive strategy type from each pipeline */
    QueueHandle_t  *action_queues;    /*!< Array of queues to send action to each pipeline */
    uint8_t         pipeline_count;   /*!< Number of pipelines */
    TaskHandle_t    task_handle;      /*!< Manage task handle */
} pipeline_strategy_manage_ctx_t;

/**
 * @brief  Structure for audio input process context
 */
typedef struct {
    esp_gmf_db_handle_t  data_bus;  /*!< GMF databus handle */
    esp_gmf_io_handle_t  io_hd;     /*!< IO handle */
    char                *url;       /*!< Pointer to URL */
} audio_indata_process_t;

/**
 * @brief  Structure for pausing/resuming audio input during processing
 */
typedef struct {
    esp_gmf_pipeline_handle_t  pipeline;        /*!< Pipeline to pause/resume */
    EventGroupHandle_t         event_group;     /*!< Event group to check if playback finished */
    uint32_t                   pause_delay_ms;  /*!< Delay between pause and resume (ms) */
} audio_indata_process_pause_t;

static const char                     *TAG                   = "AUDIO_ABORT_RESET_TEST";
static pipeline_strategy_manage_ctx_t *g_strategy_manage_ctx = NULL;

static void pipeline_strategy_manage_task(void *param)
{
    pipeline_strategy_manage_ctx_t *manage_ctx = (pipeline_strategy_manage_ctx_t *)param;
    pipeline_strategy_msg_t strategy_msg;
    pipeline_strategy_action_msg_t action_msg;
    uint8_t *pipeline_states = NULL;
    bool all_finish = true;
    int i;
    pipeline_states = (uint8_t *)calloc(manage_ctx->pipeline_count, sizeof(uint8_t));
    if (!pipeline_states) {
        ESP_LOGE(TAG, "Failed to allocate pipeline_states");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Pipeline strategy manage task started, pipeline_count: %d", manage_ctx->pipeline_count);
    while (1) {
        memset(pipeline_states, 0, manage_ctx->pipeline_count * sizeof(uint8_t));
        all_finish = true;
        for (i = 0; i < manage_ctx->pipeline_count; i++) {
            if (xQueueReceive(manage_ctx->strategy_queues[i], &strategy_msg, portMAX_DELAY) == pdTRUE) {
                pipeline_states[strategy_msg.pipeline_id] = strategy_msg.strategy_type;
                ESP_LOGD(TAG, "Received strategy_type: %d from pipeline_id: %d",
                         strategy_msg.strategy_type, strategy_msg.pipeline_id);
                if (strategy_msg.strategy_type != GMF_TASK_STRATEGY_TYPE_FINISH) {
                    all_finish = false;
                }
            } else {
                all_finish = false;
            }
        }
        if (all_finish) {
            action_msg.action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
            ESP_LOGI(TAG, "All pipelines finished, sending DEFAULT action");
        } else {
            action_msg.action = GMF_TASK_STRATEGY_ACTION_RESET;
            ESP_LOGI(TAG, "Some pipeline aborted, sending RESET action");
        }
        for (i = 0; i < manage_ctx->pipeline_count; i++) {
            if (xQueueSend(manage_ctx->action_queues[i], &action_msg, portMAX_DELAY) != pdTRUE) {
                ESP_LOGE(TAG, "Failed to send action to pipeline %d", i);
            }
        }
    }
    free(pipeline_states);
    vTaskDelete(NULL);
}

static esp_gmf_err_t pipeline_strategy_abort_func1(esp_gmf_task_handle_t handle, uint8_t strategy_type, void *ctx, uint8_t *out_action)
{
    pipeline_strategy_ctx_t *strategy_ctx = (pipeline_strategy_ctx_t *)ctx;
    esp_gmf_io_handle_t io = strategy_ctx->io;
    esp_gmf_db_handle_t db = strategy_ctx->db;
    pipeline_strategy_msg_t strategy_msg;
    pipeline_strategy_action_msg_t action_msg;
    QueueHandle_t strategy_queue = NULL;
    BaseType_t ret;
    if (g_strategy_manage_ctx && strategy_ctx->pipeline_id < g_strategy_manage_ctx->pipeline_count) {
        strategy_queue = g_strategy_manage_ctx->strategy_queues[strategy_ctx->pipeline_id];
    }
    if (strategy_queue) {
        strategy_msg.strategy_type = strategy_type;
        strategy_msg.pipeline_id = strategy_ctx->pipeline_id;
        ret = xQueueSend(strategy_queue, &strategy_msg, portMAX_DELAY);
        if (ret != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send strategy type to manage task");
            *out_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
            return ESP_GMF_ERR_OK;
        }
        ESP_LOGD(TAG, "Sent strategy_type: %d from pipeline_id: %d", strategy_type, strategy_ctx->pipeline_id);
    }
    if (strategy_ctx->action_queue) {
        ret = xQueueReceive(strategy_ctx->action_queue, &action_msg, portMAX_DELAY);
        if (ret == pdTRUE) {
            *out_action = action_msg.action;
            ESP_LOGD(TAG, "Received action: %d for pipeline_id: %d", action_msg.action, strategy_ctx->pipeline_id);
        } else {
            *out_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
        }
    } else {
        if (strategy_type == GMF_TASK_STRATEGY_TYPE_ABORT) {
            *out_action = GMF_TASK_STRATEGY_ACTION_RESET;
        } else if (strategy_type == GMF_TASK_STRATEGY_TYPE_FINISH) {
            *out_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
        } else {
            *out_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
        }
    }
    if (*out_action == GMF_TASK_STRATEGY_ACTION_RESET) {
        if (io) {
            ESP_LOGI(TAG, "Reset IO");
            esp_gmf_io_reset(io);
        }
        if (db) {
            ESP_LOGI(TAG, "Reset DB");
            esp_gmf_db_reset(db);
        }
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t pipeline_strategy_manage_task_create(pipeline_strategy_manage_ctx_t *manage_ctx)
{
    if (!manage_ctx || manage_ctx->pipeline_count == 0) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    manage_ctx->strategy_queues = (QueueHandle_t *)calloc(manage_ctx->pipeline_count, sizeof(QueueHandle_t));
    TEST_ASSERT_NOT_NULL(manage_ctx->strategy_queues);
    manage_ctx->action_queues = (QueueHandle_t *)calloc(manage_ctx->pipeline_count, sizeof(QueueHandle_t));
    TEST_ASSERT_NOT_NULL(manage_ctx->action_queues);
    for (int i = 0; i < manage_ctx->pipeline_count; i++) {
        manage_ctx->strategy_queues[i] = xQueueCreate(1, sizeof(pipeline_strategy_msg_t));
        TEST_ASSERT_NOT_NULL(manage_ctx->strategy_queues[i]);
        manage_ctx->action_queues[i] = xQueueCreate(1, sizeof(pipeline_strategy_action_msg_t));
        TEST_ASSERT_NOT_NULL(manage_ctx->action_queues[i]);
    }
    BaseType_t ret = xTaskCreate(pipeline_strategy_manage_task, "strategy_mgr", 4096, manage_ctx, 6, &manage_ctx->task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create manage task");
        // Cleanup queues
        for (int i = 0; i < manage_ctx->pipeline_count; i++) {
            if (manage_ctx->strategy_queues[i]) {
                vQueueDelete(manage_ctx->strategy_queues[i]);
            }
            if (manage_ctx->action_queues[i]) {
                vQueueDelete(manage_ctx->action_queues[i]);
            }
        }
        free(manage_ctx->strategy_queues);
        free(manage_ctx->action_queues);
        return ESP_GMF_ERR_FAIL;
    }
    g_strategy_manage_ctx = manage_ctx;
    ESP_LOGI(TAG, "Pipeline strategy manage task created successfully");
    return ESP_GMF_ERR_OK;
}

static void pipeline_strategy_manage_task_destroy(pipeline_strategy_manage_ctx_t *manage_ctx)
{
    if (!manage_ctx) {
        return;
    }
    if (manage_ctx->task_handle) {
        vTaskDelete(manage_ctx->task_handle);
        manage_ctx->task_handle = NULL;
    }
    if (manage_ctx->strategy_queues) {
        for (int i = 0; i < manage_ctx->pipeline_count; i++) {
            if (manage_ctx->strategy_queues[i]) {
                vQueueDelete(manage_ctx->strategy_queues[i]);
            }
        }
        free(manage_ctx->strategy_queues);
        manage_ctx->strategy_queues = NULL;
    }
    if (manage_ctx->action_queues) {
        for (int i = 0; i < manage_ctx->pipeline_count; i++) {
            if (manage_ctx->action_queues[i]) {
                vQueueDelete(manage_ctx->action_queues[i]);
            }
        }
        free(manage_ctx->action_queues);
        manage_ctx->action_queues = NULL;
    }
    g_strategy_manage_ctx = NULL;
    ESP_LOGI(TAG, "Pipeline strategy manage task destroyed");
}

static esp_err_t _pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
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

static void audio_indata_process_pause(void *param)
{
    audio_indata_process_pause_t *handle = (audio_indata_process_pause_t *)param;
    esp_gmf_pipeline_handle_t pipeline = handle->pipeline;
    EventGroupHandle_t event_group = handle->event_group;
    uint32_t pause_delay_ms = handle->pause_delay_ms;
    esp_gmf_err_t ret;
    bool is_paused = false;
    if (!pipeline || !event_group) {
        ESP_LOGE(TAG, "Invalid parameters for audio_indata_process_pause");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "audio_indata_process_pause task started, pause_delay_ms: %lu", pause_delay_ms);
    vTaskDelay(1000 / portTICK_RATE_MS);
    while (1) {
        EventBits_t bits = xEventGroupGetBits(event_group);
        if (bits & PIPELINE_BLOCK_BIT) {
            ESP_LOGI(TAG, "Playback finished, exiting pause/resume loop");
            break;
        }
        if (!is_paused) {
            ret = esp_gmf_pipeline_pause(pipeline);
            if (ret == ESP_GMF_ERR_OK) {
                is_paused = true;
                ESP_LOGI(TAG, "Pipeline paused");
            } else {
                ESP_LOGW(TAG, "Failed to pause pipeline, ret: %d", ret);
                vTaskDelay(pause_delay_ms / 2 / portTICK_RATE_MS);
                continue;
            }
        } else {
            ret = esp_gmf_pipeline_resume(pipeline);
            if (ret == ESP_GMF_ERR_OK) {
                is_paused = false;
                ESP_LOGI(TAG, "Pipeline resumed");
            } else {
                ESP_LOGW(TAG, "Failed to resume pipeline, ret: %d", ret);
                vTaskDelay(pause_delay_ms / 2 / portTICK_RATE_MS);
                continue;
            }
        }
        vTaskDelay(pause_delay_ms / portTICK_RATE_MS);
    }
    ESP_LOGI(TAG, "audio_indata_process_pause task exited");
    vTaskDelete(NULL);
}

static void audio_indata_process(void *param)
{
    audio_indata_process_t *handle = (audio_indata_process_t *)param;
    esp_gmf_io_handle_t io = handle->io_hd;
    esp_gmf_db_handle_t db_handle = handle->data_bus;
    esp_gmf_err_io_t io_ret = ESP_GMF_IO_OK;
    if (handle->url) {
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_io_set_uri(io, handle->url));
    }
    esp_gmf_io_t *io_base = (esp_gmf_io_t *)io;
    if (io_base->open) {
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, io_base->open(io));
    }
    while (1) {
        esp_gmf_data_bus_block_t blk = {0};
        io_ret = esp_gmf_db_acquire_write(db_handle, &blk, 4096, portMAX_DELAY);
        if (io_ret != ESP_GMF_IO_OK) {
            if (io_ret == ESP_GMF_IO_ABORT) {
                continue;
            }
            ESP_LOGE(TAG, "Failed to acquire write buffer, ret: %d", io_ret);
            break;
        }
        esp_gmf_payload_t payload = {0};
        payload.buf = blk.buf;
        payload.buf_length = blk.buf_length;
        payload.valid_size = 0;
        payload.is_done = false;
        io_ret = io_base->acquire_read(io, &payload, blk.buf_length, portMAX_DELAY);
        if (io_ret != ESP_GMF_IO_OK) {
            ESP_LOGE(TAG, "Failed to read from file, ret: %d", io_ret);
            esp_gmf_db_release_write(db_handle, &blk, portMAX_DELAY);
            break;
        }
        blk.valid_size = payload.valid_size;
        blk.is_last = payload.is_done;
        io_ret = esp_gmf_db_release_write(db_handle, &blk, portMAX_DELAY);
        if (io_ret != ESP_GMF_IO_OK) {
            if (io_ret == ESP_GMF_IO_ABORT) {
                continue;
            }
            ESP_LOGE(TAG, "Failed to release write buffer, ret: %d", io_ret);
            break;
        }
        if (io_base->release_read) {
            io_ret = io_base->release_read(io, &payload, portMAX_DELAY);
            if (io_ret != ESP_GMF_IO_OK) {
                ESP_LOGE(TAG, "Failed to release read from file, ret: %d", io_ret);
                break;
            }
        }
        if (payload.is_done) {
            ESP_LOGI(TAG, "File read completed");
            break;
        }
    }
    ESP_LOGI(TAG, "audio_indata_process exited");
    vTaskDelete(NULL);
}

static esp_gmf_err_io_t audio_acquire_read(void *handle, void *payload, uint32_t wanted_size, int block_ticks)
{
    esp_gmf_db_handle_t data_bus = (esp_gmf_db_handle_t)handle;
    esp_gmf_data_bus_block_t *blk = (esp_gmf_data_bus_block_t *)payload;
    esp_gmf_err_io_t ret = esp_gmf_db_acquire_read(data_bus, payload, wanted_size, block_ticks);
    ESP_LOGD(TAG, "acq_rd: %ld, vld: %d, done: %d, %p, %d", wanted_size, blk->valid_size, blk->is_last, blk->buf, blk->buf_length);
    return ret;
}

static esp_gmf_err_io_t audio_release_read(void *handle, void *payload, int block_ticks)
{
    esp_gmf_db_handle_t data_bus = (esp_gmf_db_handle_t)handle;
    esp_gmf_data_bus_block_t *blk = (esp_gmf_data_bus_block_t *)payload;
    ESP_LOGD(TAG, "rel_rd: %p, vld: %d, len: %d done: %d", blk->buf, blk->valid_size, blk->buf_length, blk->is_last);
    return esp_gmf_db_release_read(data_bus, payload, block_ticks);
}

TEST_CASE("Audio File Stream Play Abort to Start, One pipeline", "[ESP_GMF_POOL]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_ELEMENT", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_IO", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_codec_info_t codec_info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
    codec_info.play_info.sample_rate = 44100;
    codec_info.play_info.channel = 2;
    codec_info.play_info.bits_per_sample = 16;
    codec_info.record_info = codec_info.play_info;
    esp_gmf_app_setup_codec_dev(&codec_info);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    EventGroupHandle_t pipe_sync_evt = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt, return);

    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);
    // Create pipeline1
    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_dec", "aud_fade", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt"};
    esp_gmf_pool_new_pipeline(pool, NULL, name, sizeof(name) / sizeof(char *), "io_codec_dev", &pipe);
    TEST_ASSERT_NOT_NULL(pipe);
    gmf_setup_pipeline_out_dev(pipe);

    esp_gmf_io_handle_t io = NULL;
    file_io_cfg_t file_cfg = FILE_IO_CFG_DEFAULT();
    file_cfg.dir = ESP_GMF_IO_DIR_READER;
    file_cfg.name = "audio_indata_file";
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_io_file_init(&file_cfg, &io));

    esp_gmf_db_handle_t db_handle = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_block(4096, 10, &db_handle));
    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BLOCK(audio_acquire_read, audio_release_read, NULL, db_handle, 4096, portMAX_DELAY);
    esp_gmf_pipeline_reg_el_port(pipe, "aud_dec", ESP_GMF_IO_DIR_READER, in_port);
    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_element_handle_t rate_cvt_el = NULL;
    esp_gmf_element_handle_t ch_cvt_el = NULL;
    esp_gmf_element_handle_t bit_cvt_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_rate_cvt", &rate_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_ch_cvt", &ch_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_bit_cvt", &bit_cvt_el);
    esp_gmf_info_sound_t info = {
        .format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_rate(rate_cvt_el, 44100));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_ch(ch_cvt_el, 2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_bits(bit_cvt_el, 16));

    // Create manage task for strategy control
    static pipeline_strategy_manage_ctx_t manage_ctx = {
        .pipeline_count = 1,
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, pipeline_strategy_manage_task_create(&manage_ctx));

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    cfg.thread.prio = 9;
    cfg.thread.core = 1;
    esp_gmf_task_handle_t work_task = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task));
    pipeline_strategy_ctx_t strategy_ctx = {
        .io = io,
        .db = db_handle,
        .action_queue = manage_ctx.action_queues[0],
        .pipeline_id = 0,
    };
    esp_gmf_task_set_strategy_func(work_task, pipeline_strategy_abort_func1, &strategy_ctx);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe, work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt));
    ESP_GMF_MEM_SHOW(TAG);

    static char url[100] = "/sdcard/112_taohualuo_30s_320kbps.mp3";
    // Create audio_indata_process task
    static audio_indata_process_t audio_indata_ctx = {0};
    audio_indata_ctx.data_bus = db_handle;
    audio_indata_ctx.url = url;
    audio_indata_ctx.io_hd = io;
    TaskHandle_t audio_indata_task = NULL;
    xTaskCreate(audio_indata_process, "audio_indata", 4096, &audio_indata_ctx, 6, &audio_indata_task);
    TEST_ASSERT_NOT_NULL(audio_indata_task);

    // Run pipelines
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));
    vTaskDelay(10000 / portTICK_RATE_MS);
    esp_gmf_db_abort(audio_indata_ctx.data_bus);
    vTaskDelay(15000 / portTICK_RATE_MS);
    esp_gmf_db_abort(audio_indata_ctx.data_bus);
    xEventGroupWaitBits(pipe_sync_evt, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe));
    if (io) {
        esp_gmf_io_t *io_base = (esp_gmf_io_t *)io;
        if (io_base->close) {
            io_base->close(io);
        }
        esp_gmf_obj_delete((esp_gmf_obj_t *)io);
        io = NULL;
    }

    if (db_handle) {
        esp_gmf_db_deinit(db_handle);
        db_handle = NULL;
    }
    gmf_unregister_audio_all(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_deinit(pool));
    pipeline_strategy_manage_task_destroy(&manage_ctx);
    vEventGroupDelete(pipe_sync_evt);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio File Stream Play Abort to Start, Two pipelines", "[ESP_GMF_POOL]")
{
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_codec_info_t codec_info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
    codec_info.play_info.sample_rate = 44100;
    codec_info.play_info.channel = 2;
    codec_info.play_info.bits_per_sample = 16;
    codec_info.record_info = codec_info.play_info;
    esp_gmf_app_setup_codec_dev(&codec_info);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    EventGroupHandle_t pipe_sync_evt1 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt1, return);
    EventGroupHandle_t pipe_sync_evt2 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt2, return);
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);
    // Create pipeline1
    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_dec", "aud_fade", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt"};
    esp_gmf_pool_new_pipeline(pool, NULL, name, sizeof(name) / sizeof(char *), NULL, &pipe);
    TEST_ASSERT_NOT_NULL(pipe);

    esp_gmf_io_handle_t io = NULL;
    file_io_cfg_t file_cfg = FILE_IO_CFG_DEFAULT();
    file_cfg.dir = ESP_GMF_IO_DIR_READER;
    file_cfg.name = "audio_indata_file";
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_io_file_init(&file_cfg, &io));

    esp_gmf_db_handle_t db_handle = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_block(5, 1024, &db_handle));
    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BLOCK(audio_acquire_read, audio_release_read, NULL, db_handle, 1024, portMAX_DELAY);
    esp_gmf_pipeline_reg_el_port(pipe, "aud_dec", ESP_GMF_IO_DIR_READER, in_port);
    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_element_handle_t rate_cvt_el = NULL;
    esp_gmf_element_handle_t ch_cvt_el = NULL;
    esp_gmf_element_handle_t bit_cvt_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_rate_cvt", &rate_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_ch_cvt", &ch_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_bit_cvt", &bit_cvt_el);
    esp_gmf_info_sound_t info = {
        .format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_rate(rate_cvt_el, 44100));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_ch(ch_cvt_el, 2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_bits(bit_cvt_el, 16));
    // Create manage task for strategy control
    static pipeline_strategy_manage_ctx_t manage_ctx = {
        .pipeline_count = 2,
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, pipeline_strategy_manage_task_create(&manage_ctx));

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    cfg.thread.prio = 9;
    esp_gmf_task_handle_t work_task = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task));
    pipeline_strategy_ctx_t strategy_ctx = {
        .io = io,
        .db = db_handle,
        .action_queue = manage_ctx.action_queues[0],
        .pipeline_id = 0,
    };
    esp_gmf_task_set_strategy_func(work_task, pipeline_strategy_abort_func1, &strategy_ctx);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe, work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt1));
    ESP_GMF_MEM_SHOW(TAG);

    // Create audio effects pipeline
    esp_gmf_pipeline_handle_t pipe_effects = NULL;
    const char *effects_name[] = {"aud_alc", "aud_sonic", "aud_eq", "aud_drc", "aud_mbc"};
    esp_gmf_pool_new_pipeline(pool, NULL, effects_name, sizeof(effects_name) / sizeof(char *), "io_codec_dev", &pipe_effects);
    TEST_ASSERT_NOT_NULL(pipe_effects);
    gmf_setup_pipeline_out_dev(pipe_effects);

    esp_gmf_element_handle_t alc_el = NULL;
    esp_gmf_element_handle_t sonic_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe_effects, "aud_alc", &alc_el);
    esp_gmf_pipeline_get_el_by_name(pipe_effects, "aud_sonic", &sonic_el);
    esp_gmf_audio_param_set_alc_channel_gain(alc_el, 0xff, -10);
    esp_gmf_audio_param_set_speed(sonic_el, 1.2);
    esp_gmf_audio_param_set_pitch(sonic_el, 1.2);

    esp_gmf_task_cfg_t effects_task_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    effects_task_cfg.ctx = NULL;
    effects_task_cfg.cb = NULL;
    effects_task_cfg.thread.prio = 10;
    effects_task_cfg.name = "effects_task";
    esp_gmf_task_handle_t effects_task = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&effects_task_cfg, &effects_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe_effects, effects_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe_effects));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe_effects, _pipeline_event, pipe_sync_evt2));

    // Connect pipeline1 to effects pipeline
    esp_gmf_db_handle_t rb_handle = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(5, 1024, &rb_handle));
    TEST_ASSERT_NOT_NULL(rb_handle);
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write,
                                                               esp_gmf_db_deinit, rb_handle, 1024, ESP_GMF_MAX_DELAY);
    esp_gmf_port_handle_t in_port_effects = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read,
                                                                     esp_gmf_db_deinit, rb_handle, 1024, ESP_GMF_MAX_DELAY);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe, "aud_bit_cvt", out_port, pipe_effects, "aud_alc", in_port_effects));

    pipeline_strategy_ctx_t strategy_ctx_effects = {
        .io = NULL,
        .db = rb_handle,
        .action_queue = manage_ctx.action_queues[1],
        .pipeline_id = 1,
    };
    esp_gmf_task_set_strategy_func(effects_task, pipeline_strategy_abort_func1, &strategy_ctx_effects);
    static char url[100] = "/sdcard/112_taohualuo_30s_320kbps.mp3";
    // Create audio_indata_process task
    static audio_indata_process_t audio_indata_ctx = {0};
    audio_indata_ctx.data_bus = db_handle;
    audio_indata_ctx.url = url;
    audio_indata_ctx.io_hd = io;
    TaskHandle_t audio_indata_task = NULL;
    xTaskCreate(audio_indata_process, "audio_indata", 4096, &audio_indata_ctx, 5, &audio_indata_task);
    TEST_ASSERT_NOT_NULL(audio_indata_task);
    // Run pipelines
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe_effects));
    vTaskDelay(10000 / portTICK_RATE_MS);
    esp_gmf_db_abort(audio_indata_ctx.data_bus);
    esp_gmf_db_abort(rb_handle);
    vTaskDelay(15000 / portTICK_RATE_MS);
    esp_gmf_db_abort(audio_indata_ctx.data_bus);
    esp_gmf_db_abort(rb_handle);
    vTaskDelay(19000 / portTICK_RATE_MS);
    esp_gmf_db_abort(audio_indata_ctx.data_bus);
    esp_gmf_db_abort(rb_handle);
    xEventGroupWaitBits(pipe_sync_evt1, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    xEventGroupWaitBits(pipe_sync_evt2, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe_effects));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(effects_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe_effects));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe));
    if (io) {
        esp_gmf_io_t *io_base = (esp_gmf_io_t *)io;
        if (io_base->close) {
            io_base->close(io);
        }
        esp_gmf_obj_delete((esp_gmf_obj_t *)io);
        io = NULL;
    }
    if (db_handle) {
        esp_gmf_db_deinit(db_handle);
        db_handle = NULL;
    }
    gmf_unregister_audio_all(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_deinit(pool));
    pipeline_strategy_manage_task_destroy(&manage_ctx);
    vEventGroupDelete(pipe_sync_evt1);
    vEventGroupDelete(pipe_sync_evt2);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio File Stream Play Abort to Start, Two pipelines, with Pause and Resume", "[ESP_GMF_POOL]")
{
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_codec_info_t codec_info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
    codec_info.play_info.sample_rate = 44100;
    codec_info.play_info.channel = 2;
    codec_info.play_info.bits_per_sample = 16;
    codec_info.record_info = codec_info.play_info;
    esp_gmf_app_setup_codec_dev(&codec_info);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    EventGroupHandle_t pipe_sync_evt1 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt1, return);
    EventGroupHandle_t pipe_sync_evt2 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt2, return);
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);
    // Create pipeline1
    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt"};
    esp_gmf_pool_new_pipeline(pool, NULL, name, sizeof(name) / sizeof(char *), NULL, &pipe);
    TEST_ASSERT_NOT_NULL(pipe);

    esp_gmf_io_handle_t io = NULL;
    file_io_cfg_t file_cfg = FILE_IO_CFG_DEFAULT();
    file_cfg.dir = ESP_GMF_IO_DIR_READER;
    file_cfg.name = "audio_indata_file";
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_io_file_init(&file_cfg, &io));

    esp_gmf_db_handle_t db_handle = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_block(5, 1024, &db_handle));
    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BLOCK(audio_acquire_read, audio_release_read, NULL, db_handle, 1024, portMAX_DELAY);
    esp_gmf_pipeline_reg_el_port(pipe, "aud_dec", ESP_GMF_IO_DIR_READER, in_port);
    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_element_handle_t rate_cvt_el = NULL;
    esp_gmf_element_handle_t ch_cvt_el = NULL;
    esp_gmf_element_handle_t bit_cvt_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_rate_cvt", &rate_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_ch_cvt", &ch_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_bit_cvt", &bit_cvt_el);
    esp_gmf_info_sound_t info = {
        .format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_rate(rate_cvt_el, 44100));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_ch(ch_cvt_el, 2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_bits(bit_cvt_el, 16));

    // Create manage task for strategy control
    static pipeline_strategy_manage_ctx_t manage_ctx = {
        .pipeline_count = 2,
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, pipeline_strategy_manage_task_create(&manage_ctx));

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    cfg.thread.prio = 9;
    esp_gmf_task_handle_t work_task = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task));
    pipeline_strategy_ctx_t strategy_ctx = {
        .io = io,
        .db = db_handle,
        .action_queue = manage_ctx.action_queues[0],
        .pipeline_id = 0,
    };
    esp_gmf_task_set_strategy_func(work_task, pipeline_strategy_abort_func1, &strategy_ctx);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe, work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt1));
    ESP_GMF_MEM_SHOW(TAG);

    // Create audio effects pipeline
    esp_gmf_pipeline_handle_t pipe_effects = NULL;
    const char *effects_name[] = {"aud_alc", "aud_sonic", "aud_eq", "aud_drc", "aud_mbc"};
    esp_gmf_pool_new_pipeline(pool, NULL, effects_name, sizeof(effects_name) / sizeof(char *), "io_codec_dev", &pipe_effects);
    TEST_ASSERT_NOT_NULL(pipe_effects);
    gmf_setup_pipeline_out_dev(pipe_effects);

    esp_gmf_element_handle_t alc_el = NULL;
    esp_gmf_element_handle_t sonic_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe_effects, "aud_alc", &alc_el);
    esp_gmf_pipeline_get_el_by_name(pipe_effects, "aud_sonic", &sonic_el);
    esp_gmf_audio_param_set_alc_channel_gain(alc_el, 0xff, -10);
    esp_gmf_audio_param_set_speed(sonic_el, 1.2);
    esp_gmf_audio_param_set_pitch(sonic_el, 1.2);

    esp_gmf_task_cfg_t effects_task_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    effects_task_cfg.ctx = NULL;
    effects_task_cfg.cb = NULL;
    effects_task_cfg.thread.prio = 10;
    effects_task_cfg.name = "effects_task";
    esp_gmf_task_handle_t effects_task = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&effects_task_cfg, &effects_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe_effects, effects_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe_effects));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe_effects, _pipeline_event, pipe_sync_evt2));

    // Connect pipeline1 to effects pipeline
    esp_gmf_db_handle_t rb_handle = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(5, 4096, &rb_handle));
    TEST_ASSERT_NOT_NULL(rb_handle);
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write,
                                                               esp_gmf_db_deinit, rb_handle, 1024, ESP_GMF_MAX_DELAY);
    esp_gmf_port_handle_t in_port_effects = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read,
                                                                     esp_gmf_db_deinit, rb_handle, 1024, ESP_GMF_MAX_DELAY);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe, "aud_bit_cvt", out_port, pipe_effects, "aud_alc", in_port_effects));

    pipeline_strategy_ctx_t strategy_ctx_effects = {
        .io = NULL,
        .db = rb_handle,
        .action_queue = manage_ctx.action_queues[1],
        .pipeline_id = 1,
        .pipeline = pipe_effects,
    };
    esp_gmf_task_set_strategy_func(effects_task, pipeline_strategy_abort_func1, &strategy_ctx_effects);
    static char url[100] = "/sdcard/112_taohualuo_30s_320kbps.mp3";
    // Create audio_indata_process task
    static audio_indata_process_t audio_indata_ctx = {0};
    audio_indata_ctx.data_bus = db_handle;
    audio_indata_ctx.url = url;
    audio_indata_ctx.io_hd = io;
    TaskHandle_t audio_indata_task = NULL;
    xTaskCreate(audio_indata_process, "audio_indata", 4096, &audio_indata_ctx, 5, &audio_indata_task);
    TEST_ASSERT_NOT_NULL(audio_indata_task);

    // Run pipelines
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe_effects));

    // Create audio_indata_process_pause task to continuously pause/resume pipe_effects
    audio_indata_process_pause_t pause_ctx = {
        .pipeline = pipe_effects,
        .event_group = pipe_sync_evt2,
        .pause_delay_ms = 2000,
    };
    TaskHandle_t pause_task = NULL;
    xTaskCreate(audio_indata_process_pause, "pause_resume", 4096, &pause_ctx, 5, &pause_task);
    TEST_ASSERT_NOT_NULL(pause_task);

    // Wait for pipelines to finish (pause/resume task will automatically stop when playback ends)
    xEventGroupWaitBits(pipe_sync_evt1, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    xEventGroupWaitBits(pipe_sync_evt2, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

    // Stop and cleanup pause task
    if (pause_task) {
        vTaskDelete(pause_task);
        pause_task = NULL;
    }
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe_effects));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(effects_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe_effects));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe));
    if (io) {
        esp_gmf_io_t *io_base = (esp_gmf_io_t *)io;
        if (io_base->close) {
            io_base->close(io);
        }
        esp_gmf_obj_delete((esp_gmf_obj_t *)io);
        io = NULL;
    }
    if (db_handle) {
        esp_gmf_db_deinit(db_handle);
        db_handle = NULL;
    }
    gmf_unregister_audio_all(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_deinit(pool));
    pipeline_strategy_manage_task_destroy(&manage_ctx);
    vEventGroupDelete(pipe_sync_evt1);
    vEventGroupDelete(pipe_sync_evt2);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

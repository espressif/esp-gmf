/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_audio_enc_default.h"
#include "esp_audio_enc_reg.h"
#include "esp_err.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_data_bus.h"
#include "esp_gmf_element.h"
#include "esp_gmf_new_databus.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_thread.h"
#include "esp_gmf_obj.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_port.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "unity.h"
#include <string.h>
#include "gmf_audio_play_com.h"

static const char *TAG = "CONCURRENT_ENCODER_TEST";

#define PIPELINE_BLOCK_BIT               BIT(0)
#define ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT (1920)

typedef struct {
    uint8_t *buf;
    int32_t  buf_len;
} queue_data_t;

typedef struct {
    QueueHandle_t queue1;
    QueueHandle_t queue2;
} ae_data_hd_t;

typedef struct {
    queue_data_t  data;
    QueueHandle_t queue1;
    QueueHandle_t queue2;
    QueueHandle_t queue3;
    QueueHandle_t queue4;
} ae_task_hd_t;

typedef struct {
    esp_gmf_pipeline_handle_t pipeline;
    esp_gmf_element_handle_t  encoder;
    esp_gmf_port_handle_t     in_port;
    esp_gmf_port_handle_t     out_port;
    esp_gmf_task_handle_t     task;
    EventGroupHandle_t        sync_evt;
    ae_data_hd_t             *data_ctx;
} encoder_res_t;

static esp_err_t _pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
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

static void queue_sender_task(void *pvParameters)
{
    ae_task_hd_t *ctx  = (ae_task_hd_t *)pvParameters;
    queue_data_t *data = &ctx->data;
    while (1) {
        int received_cnt = 0;
        // Send data to queue1
        if (xQueueSend(ctx->queue1, data, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send to queue1");
            break;
        }
        // Wait for data to be received once
        if (xQueueReceive(ctx->queue2, &received_cnt, 1000) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to receive from queue1");
            break;
        }
        // If data has been received once, send to queue2
        if (received_cnt == 1) {
            if (xQueueSend(ctx->queue3, data, portMAX_DELAY) != pdTRUE) {
                ESP_LOGE(TAG, "Failed to send to queue2");
                break;
            }
            if (xQueueReceive(ctx->queue4, &received_cnt, 1000) != pdTRUE) {
                ESP_LOGE(TAG, "Failed to receive from queue2");
                break;
            }
        }
    }
}

static esp_gmf_err_io_t ae_acquire_read(void *handle, esp_gmf_data_bus_block_t *blk, int wanted_size, int block_ticks)
{
    ae_data_hd_t *ctx = (ae_data_hd_t *)handle;
    if (ctx == NULL || blk == NULL) {
        return ESP_GMF_IO_FAIL;
    }
    queue_data_t data;
    if (xQueueReceive(ctx->queue1, &data, block_ticks) != pdTRUE) {
        return ESP_GMF_IO_FAIL;
    }
    int valid_size = data.buf_len > blk->buf_length ? blk->buf_length : data.buf_len;
    memcpy(blk->buf, data.buf, valid_size);
    blk->valid_size = valid_size;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t ae_release_read(void *handle, esp_gmf_data_bus_block_t *blk, int block_ticks)
{
    ae_data_hd_t *ctx = (ae_data_hd_t *)handle;
    if (ctx == NULL || blk == NULL) {
        return ESP_GMF_IO_FAIL;
    }
    int release_cnt = 1;
    if (xQueueSend(ctx->queue2, &release_cnt, block_ticks) != pdTRUE) {
        return ESP_GMF_IO_FAIL;
    }
    blk->valid_size = 0;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t ae_acquire_write(void *handle, esp_gmf_data_bus_block_t *blk, int wanted_size, int block_ticks)
{
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t ae_release_write(void *handle, esp_gmf_data_bus_block_t *blk, int block_ticks)
{
    ESP_LOGD(TAG, "%s-%d,file_release_write, %d,%p", __func__, __LINE__, blk->valid_size, blk);
    return ESP_GMF_IO_OK;
}

static int prepare_encoder_pipeline(esp_gmf_pool_handle_t pool, esp_gmf_info_sound_t *snd_info, encoder_res_t *res, QueueHandle_t queue1, QueueHandle_t queue2)
{
    if (!pool || !snd_info || !res) {
        return ESP_FAIL;
    }
    // Create pipeline
    const char *name[] = { "aud_enc" };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, NULL, name, sizeof(name) / sizeof(char *), NULL, &res->pipeline));
    TEST_ASSERT_NOT_NULL(res->pipeline);
    // Get encoder element
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(res->pipeline, "aud_enc", &res->encoder));
    // Create event group
    res->sync_evt = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL(res->sync_evt);
    // Create data context
    res->data_ctx = (ae_data_hd_t *)calloc(1, sizeof(ae_data_hd_t));
    TEST_ASSERT_NOT_NULL(res->data_ctx);
    res->data_ctx->queue1 = queue1;
    res->data_ctx->queue2 = queue2;
    // Set up ports
    res->in_port  = NEW_ESP_GMF_PORT_IN_BYTE(ae_acquire_read, ae_release_read, NULL, res->data_ctx, 100, 100);
    res->out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ae_acquire_write, ae_release_write, NULL, res->data_ctx, 100, 100);
    esp_gmf_element_register_in_port(res->encoder, res->in_port);
    esp_gmf_element_register_out_port(res->encoder, res->out_port);
    // Configure encoder
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_enc_reconfig_by_sound_info(res->encoder, snd_info));
    // Create and bind task
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.thread.prio        = 5;
    cfg.name               = "encoder";
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &res->task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(res->pipeline, res->task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(res->pipeline, _pipeline_event, res->sync_evt));
    // Report sound info
    esp_gmf_pipeline_report_info(res->pipeline, ESP_GMF_INFO_SOUND, snd_info, sizeof(*snd_info));
    return ESP_OK;
}

static int release_encoder_pipeline(encoder_res_t *res)
{
    if (!res) {
        return ESP_FAIL;
    }
    // Cleanup resources
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(res->task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(res->pipeline));
    vEventGroupDelete(res->sync_evt);
    free(res->data_ctx);
    return ESP_OK;
}

static int create_shared_handle(ae_task_hd_t **shared_ctx)
{
    *shared_ctx = (ae_task_hd_t *)calloc(1, sizeof(ae_task_hd_t));
    TEST_ASSERT_NOT_NULL(*shared_ctx);
    (*shared_ctx)->data.buf = (uint8_t *)calloc(1, ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT * sizeof(uint8_t));
    TEST_ASSERT_NOT_NULL((*shared_ctx)->data.buf);
    (*shared_ctx)->data.buf_len = ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT;
    (*shared_ctx)->queue1 = xQueueCreate(5, sizeof(queue_data_t));
    (*shared_ctx)->queue2 = xQueueCreate(5, sizeof(queue_data_t));
    (*shared_ctx)->queue3 = xQueueCreate(5, sizeof(queue_data_t));
    (*shared_ctx)->queue4 = xQueueCreate(5, sizeof(queue_data_t));
    TEST_ASSERT_NOT_NULL((*shared_ctx)->queue1);
    TEST_ASSERT_NOT_NULL((*shared_ctx)->queue2);
    TEST_ASSERT_NOT_NULL((*shared_ctx)->queue3);
    TEST_ASSERT_NOT_NULL((*shared_ctx)->queue4);
    return ESP_OK;
}

static int delete_shared_handle(ae_task_hd_t *shared_ctx)
{
    if (!shared_ctx) {
        return ESP_FAIL;
    }
    free(shared_ctx->data.buf);
    vQueueDelete(shared_ctx->queue1);
    vQueueDelete(shared_ctx->queue2);
    vQueueDelete(shared_ctx->queue3);
    vQueueDelete(shared_ctx->queue4);
    free(shared_ctx);
    return ESP_OK;
}

TEST_CASE("Test concurrent encoder pipelines with shared buffer", "[ESP_GMF_Effects]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);
    // Initialize pool and register components
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);
    // Create shared input buffer
    ae_task_hd_t *shared_ctx = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, create_shared_handle(&shared_ctx));
    // Create queue sender task
    TaskHandle_t task_handle = NULL;
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(queue_sender_task, "queue_sender", 10240, shared_ctx, 5, &task_handle));
    // Configure sound info
    esp_gmf_info_sound_t info = {
        .sample_rates = 44100,
        .channels = 1,
        .bits = 16,
        .bitrate = 90000,
        .format_id = ESP_AUDIO_TYPE_AAC,
    };
    // Prepare two encoder pipelines
    encoder_res_t res1 = {0};
    encoder_res_t res2 = {0};
    TEST_ASSERT_EQUAL(ESP_OK, prepare_encoder_pipeline(pool, &info, &res1, shared_ctx->queue1, shared_ctx->queue2));
    TEST_ASSERT_EQUAL(ESP_OK, prepare_encoder_pipeline(pool, &info, &res2, shared_ctx->queue3, shared_ctx->queue4));
    // Start both pipelines
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res2.pipeline));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(res1.pipeline));
    // Wait for some time to let the pipelines process data
    vTaskDelay(5000 / portTICK_RATE_MS);
    // Stop both pipelines
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res1.pipeline));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(res2.pipeline));
    // Release resources
    release_encoder_pipeline(&res1);
    release_encoder_pipeline(&res2);
    vTaskDelete(task_handle);
    TEST_ASSERT_EQUAL(ESP_OK, delete_shared_handle(shared_ctx));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_deinit(pool));
    gmf_unregister_audio_all(pool);
    ESP_GMF_MEM_SHOW(TAG);
}

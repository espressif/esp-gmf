/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "unity.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_data_bus.h"
#include "esp_gmf_new_databus.h"
#include "esp_gmf_io.h"

static const char *TAG = "IO_PRELOAD_TEST";

#define TEST_BUFFER_SIZE             8192
#define TEST_BLOCK_SIZE              1024
#define TEST_BLOCK_COUNT             8
#define TEST_PRELOAD_THRESHOLD       (TEST_BUFFER_SIZE / 4)
#define TEST_TOTAL_DATA_SIZE         (TEST_BUFFER_SIZE * 4)
#define TEST_CONSUMER_RATE_KBP       256
#define TEST_PRODUCER_SLOW_RATE_KBP  128
#define TEST_PRODUCER_FAST_RATE_KBP  512

typedef struct {
    esp_gmf_io_t  base;
    uint8_t      *data_source;
    uint32_t      data_source_size;
    uint32_t      read_position;
    uint32_t      produced_bytes;
    bool          producer_slow_mode;
    bool          test_complete;
} preload_io_t;

typedef struct {
    preload_io_t *test_io;
    uint32_t      consumed_bytes;
    uint32_t      consumer_wait_count;
    uint32_t      consumer_pause_count;
    TaskHandle_t  consumer_task;
} preload_context_t;

static esp_gmf_err_io_t preload_io_acquire_read(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted_size, int wait_ticks)
{
    preload_io_t *io = (preload_io_t *)handle;
    uint32_t available = io->data_source_size - io->read_position;
    uint32_t read_size = (available < wanted_size) ? available : wanted_size;
    esp_gmf_payload_t *load = (esp_gmf_payload_t *)payload;
    if (read_size > 0) {
        if (load->buf && load->buf_length >= read_size) {
            memcpy(load->buf, io->data_source + io->read_position, read_size);
        } else {
            ESP_LOGE(TAG, "Test IO acquire_read: buffer is NULL or too small (buf=%p, buf_len=%u, read_size=%u)",
                     load->buf, load->buf_length, read_size);
            return ESP_GMF_IO_FAIL;
        }
        load->buf_length = read_size;
        load->valid_size = read_size;
        load->is_done = (io->read_position + read_size >= io->data_source_size);
        io->read_position += read_size;
        io->produced_bytes += read_size;
        ESP_LOGD(TAG, "Test IO acquire_read: read %u bytes, position %u/%u",
                 read_size, io->read_position, io->data_source_size);
        if (io->producer_slow_mode) {
            // slow mode (128 Kbps ≈ 16 KB/s ≈ 1KB/62.5ms)
            vTaskDelay(pdMS_TO_TICKS(63));
        } else {
            // fast mode (512 Kbps ≈ 64 KB/s ≈ 1KB/15.6ms)
            vTaskDelay(pdMS_TO_TICKS(16));
        }
        return ESP_GMF_IO_OK;
    } else {
        load->valid_size = 0;
        load->is_done = true;
        return ESP_GMF_IO_OK;
    }
}

static esp_gmf_err_io_t preload_io_release_read(esp_gmf_io_handle_t handle, void *payload, int wait_ticks)
{
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_t preload_io_open(esp_gmf_io_handle_t handle)
{
    preload_io_t *io = (preload_io_t *)handle;
    io->read_position = 0;
    io->produced_bytes = 0;
    ESP_LOGI(TAG, "Preload test IO opened, data source size: %u bytes", io->data_source_size);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t preload_io_close(esp_gmf_io_handle_t handle)
{
    ESP_LOGI(TAG, "Preload test IO closed");
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t preload_io_seek(esp_gmf_io_handle_t handle, uint64_t seek_byte_pos)
{
    preload_io_t *io = (preload_io_t *)handle;
    if (seek_byte_pos < io->data_source_size) {
        io->read_position = (uint32_t)seek_byte_pos;
        ESP_LOGI(TAG, "Preload test IO seek to position %u", io->read_position);
        return ESP_GMF_ERR_OK;
    } else {
        return ESP_GMF_ERR_INVALID_ARG;
    }
}

static esp_gmf_err_t preload_io_create(preload_io_t **io_handle, bool producer_slow_mode)
{
    preload_io_t *io = (preload_io_t *)esp_gmf_oal_calloc(1, sizeof(preload_io_t));
    TEST_ASSERT_NOT_NULL(io);

    io->base.dir = ESP_GMF_IO_DIR_READER;
    io->base.type = ESP_GMF_IO_TYPE_BLOCK;
    io->base.open = preload_io_open;
    io->base.close = preload_io_close;
    io->base.seek = preload_io_seek;
    io->base.acquire_read = preload_io_acquire_read;
    io->base.release_read = preload_io_release_read;
    io->producer_slow_mode = producer_slow_mode;
    io->data_source_size = TEST_TOTAL_DATA_SIZE;
    io->data_source = (uint8_t *)esp_gmf_oal_malloc(io->data_source_size);
    TEST_ASSERT_NOT_NULL(io->data_source);

    for (uint32_t i = 0; i < io->data_source_size; i++) {
        io->data_source[i] = (uint8_t)(i % 256);
    }
    io->base.io_cfg.buffer_cfg.buffer_size = TEST_BUFFER_SIZE;
    io->base.io_cfg.buffer_cfg.io_size = TEST_BLOCK_SIZE;
    io->base.io_cfg.thread.stack = 4096;
    io->base.io_cfg.thread.prio = 5;
    io->base.io_cfg.enable_speed_monitor = true;
    int ret = esp_gmf_io_init(&io->base, &io->base.io_cfg);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);

    esp_gmf_obj_set_tag(&io->base.parent, producer_slow_mode ? "preload_io_slow" : "preload_io_fast");
    ESP_LOGI(TAG, "Preload test IO created, slow mode: %s, data size: %u bytes",
             producer_slow_mode ? "yes" : "no", io->data_source_size);
    *io_handle = io;
    return ESP_GMF_ERR_OK;
}

static void preload_io_destroy(preload_io_t *io)
{
    if (io) {
        if (io->data_source) {
            esp_gmf_oal_free(io->data_source);
        }
        esp_gmf_io_deinit(&io->base);
        esp_gmf_oal_free(io);
    }
}

static void consumer_task(void *arg)
{
    preload_context_t *ctx = (preload_context_t *)arg;
    preload_io_t *io = ctx->test_io;
    uint32_t total_consumed = 0;
    ESP_LOGI(TAG, "Consumer task started with preload logic");
    while (total_consumed < TEST_TOTAL_DATA_SIZE) {
        uint32_t filled_size = 0;
        esp_gmf_db_get_filled_size(io->base.data_bus, &filled_size);
        esp_gmf_io_speed_stats_t producer_stats;
        esp_gmf_io_get_speed_stats(&io->base, &producer_stats);
        uint32_t producer_rate_kbps = producer_stats.current_speed_kbps;
        ESP_LOGD(TAG, "Consumer: filled=%u, need=%u, producer_rate=%u Kbps, consumer_rate=%u Kbps",
                 filled_size, TEST_BLOCK_SIZE, producer_rate_kbps, TEST_CONSUMER_RATE_KBP);
        if (filled_size < TEST_BLOCK_SIZE && producer_rate_kbps > 0 && producer_rate_kbps < TEST_CONSUMER_RATE_KBP) {
            ESP_LOGI(TAG, "Preload condition met: insufficient data (%u < %u) and producer slower (%u < %u Kbps)",
                     filled_size, TEST_BLOCK_SIZE, producer_rate_kbps, TEST_CONSUMER_RATE_KBP);
            ctx->consumer_pause_count++;
            while (filled_size < TEST_PRELOAD_THRESHOLD) {
                ESP_LOGD(TAG, "Consumer paused, waiting for preload: filled=%u, threshold=%u",
                         filled_size, TEST_PRELOAD_THRESHOLD);
                vTaskDelay(pdMS_TO_TICKS(10));
                esp_gmf_db_get_filled_size(io->base.data_bus, &filled_size);
            }
            ESP_LOGI(TAG, "Preload threshold reached (%u >= %u), resuming consumption",
                     filled_size, TEST_PRELOAD_THRESHOLD);
        }
        if (filled_size < TEST_BLOCK_SIZE) {
            ESP_LOGD(TAG, "Insufficient data (%u < %u), waiting...", filled_size, TEST_BLOCK_SIZE);
            ctx->consumer_wait_count++;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        esp_gmf_payload_t payload = {0};
        payload.buf_length = TEST_BLOCK_SIZE;
        esp_gmf_err_io_t ret = esp_gmf_io_acquire_read(&io->base, &payload, TEST_BLOCK_SIZE, 0);
        if (ret == ESP_GMF_IO_OK && payload.valid_size > 0) {
            uint8_t *data_ptr = (uint8_t *)payload.buf;
            for (size_t i = 0; i < payload.valid_size; i++) {
                uint8_t expected = (uint8_t)((total_consumed + i) % 256);
                if (data_ptr[i] != expected) {
                    ESP_LOGE(TAG, "Data verification failed at offset %u: got %u, expected %u",
                             total_consumed + i, data_ptr[i], expected);
                    TEST_FAIL_MESSAGE("Data verification failed");
                }
            }
            esp_gmf_io_release_read(&io->base, &payload, 0);
            total_consumed += payload.valid_size;
            ctx->consumed_bytes = total_consumed;
            ESP_LOGD(TAG, "Consumer: read %u bytes, total %u", payload.valid_size, total_consumed);
            vTaskDelay(pdMS_TO_TICKS(20));
        } else if (ret == ESP_GMF_IO_ABORT) {
            ESP_LOGI(TAG, "Consumer: read aborted");
            break;
        } else if (ret == ESP_GMF_IO_OK && payload.valid_size == 0) {
            ESP_LOGI(TAG, "Consumer: data source exhausted");
            break;
        } else {
            ESP_LOGW(TAG, "Consumer: failed to acquire read data, ret=%d", ret);
            ctx->consumer_wait_count++;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    ESP_LOGI(TAG, "Consumer task finished: consumed %u bytes, waited %u times, paused %u times",
             total_consumed, ctx->consumer_wait_count, ctx->consumer_pause_count);
    io->test_complete = true;
    vTaskDelete(NULL);
}

TEST_CASE("GMF IO preload functionality test - slow producer", "[ESP_GMF_IO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "Starting preload functionality test with slow producer");
    preload_context_t ctx = {0};
    int ret = preload_io_create(&ctx.test_io, true);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);
    TEST_ASSERT_NOT_NULL(ctx.test_io);

    ret = esp_gmf_io_open(&ctx.test_io->base);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);

    vTaskDelay(pdMS_TO_TICKS(50));
    xTaskCreate(consumer_task, "consumer_task", 4096, &ctx, 5, &ctx.consumer_task);
    TEST_ASSERT_NOT_NULL(ctx.consumer_task);

    int wait_count = 0;
    while (!ctx.test_io->test_complete && wait_count < 200) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_count++;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "Test results:");
    ESP_LOGI(TAG, "  Produced bytes: %u", ctx.test_io->produced_bytes);
    ESP_LOGI(TAG, "  Consumed bytes: %u", ctx.consumed_bytes);
    ESP_LOGI(TAG, "  Consumer wait count: %u", ctx.consumer_wait_count);
    ESP_LOGI(TAG, "  Consumer pause count: %u", ctx.consumer_pause_count);
    TEST_ASSERT_GREATER_THAN(0, ctx.consumer_pause_count);
    TEST_ASSERT_EQUAL(ctx.test_io->produced_bytes, ctx.consumed_bytes);

    if (ctx.test_io) {
        esp_gmf_io_close(&ctx.test_io->base);
        preload_io_destroy(ctx.test_io);
    }
    ESP_LOGI(TAG, "Preload functionality test with slow producer completed successfully");
}

TEST_CASE("GMF IO preload functionality test - fast producer", "[ESP_GMF_IO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "Starting preload functionality test with fast producer");
    preload_context_t ctx = {0};
    int ret = preload_io_create(&ctx.test_io, false);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);
    TEST_ASSERT_NOT_NULL(ctx.test_io);

    ret = esp_gmf_io_open(&ctx.test_io->base);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);

    vTaskDelay(pdMS_TO_TICKS(50));
    xTaskCreate(consumer_task, "consumer_task", 4096, &ctx, 5, &ctx.consumer_task);
    TEST_ASSERT_NOT_NULL(ctx.consumer_task);

    int wait_count = 0;
    while (!ctx.test_io->test_complete && wait_count < 200) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_count++;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "Test results:");
    ESP_LOGI(TAG, "  Produced bytes: %u", ctx.test_io->produced_bytes);
    ESP_LOGI(TAG, "  Consumed bytes: %u", ctx.consumed_bytes);
    ESP_LOGI(TAG, "  Consumer wait count: %u", ctx.consumer_wait_count);
    ESP_LOGI(TAG, "  Consumer pause count: %u", ctx.consumer_pause_count);
    TEST_ASSERT_EQUAL(ctx.test_io->produced_bytes, ctx.consumed_bytes);

    if (ctx.test_io) {
        esp_gmf_io_close(&ctx.test_io->base);
        preload_io_destroy(ctx.test_io);
    }
    ESP_LOGI(TAG, "Preload functionality test with fast producer completed successfully");
}

TEST_CASE("GMF IO preload threshold test", "[ESP_GMF_IO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    uint32_t thresholds[] = {
        TEST_BUFFER_SIZE / 8,
        TEST_BUFFER_SIZE / 4,
        TEST_BUFFER_SIZE / 2,
    };
    for (size_t i = 0; i < sizeof(thresholds) / sizeof(thresholds[0]); i++) {
        ESP_LOGI(TAG, "Testing preload with threshold %u bytes (%u%%)",
                 thresholds[i], (thresholds[i] * 100) / TEST_BUFFER_SIZE);
        preload_context_t ctx = {0};
        int ret = preload_io_create(&ctx.test_io, true);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);
        TEST_ASSERT_NOT_NULL(ctx.test_io);

        ret = esp_gmf_io_open(&ctx.test_io->base);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);

        vTaskDelay(pdMS_TO_TICKS(200));
        uint32_t total_consumed = 0;
        uint32_t pause_count = 0;
        while (total_consumed < TEST_TOTAL_DATA_SIZE / 4) {
            uint32_t filled_size = 0;
            esp_gmf_db_get_filled_size(ctx.test_io->base.data_bus, &filled_size);
            esp_gmf_io_speed_stats_t producer_stats;
            esp_gmf_io_get_speed_stats(&ctx.test_io->base, &producer_stats);
            uint32_t producer_rate_kbps = producer_stats.current_speed_kbps;
            if (filled_size < TEST_BLOCK_SIZE && producer_rate_kbps > 0 && producer_rate_kbps < TEST_CONSUMER_RATE_KBP) {
                if (filled_size < thresholds[i]) {
                    pause_count++;
                    ESP_LOGD(TAG, "Preload pause at threshold %u: filled=%u, producer_rate=%u Kbps",
                             thresholds[i], filled_size, producer_rate_kbps);
                    while (filled_size < thresholds[i]) {
                        vTaskDelay(pdMS_TO_TICKS(10));
                        esp_gmf_db_get_filled_size(ctx.test_io->base.data_bus, &filled_size);
                    }
                }
            }
            if (filled_size >= TEST_BLOCK_SIZE) {
                esp_gmf_payload_t payload = {0};
                payload.buf_length = TEST_BLOCK_SIZE;
                esp_gmf_err_io_t ret = esp_gmf_io_acquire_read(&ctx.test_io->base, &payload, TEST_BLOCK_SIZE, 0);
                if (ret == ESP_GMF_IO_OK && payload.valid_size > 0) {
                    esp_gmf_io_release_read(&ctx.test_io->base, &payload, 0);
                    total_consumed += payload.valid_size;
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        ESP_LOGI(TAG, "Threshold %u test results: consumed %u bytes, pause count %u",
                 thresholds[i], total_consumed, pause_count);
        TEST_ASSERT_GREATER_THAN(0, total_consumed);

        if (ctx.test_io) {
            esp_gmf_io_close(&ctx.test_io->base);
            preload_io_destroy(ctx.test_io);
        }
    }
}

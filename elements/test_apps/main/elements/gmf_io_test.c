/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief  GMF IO Test
 *         Test gmf_io interface
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "esp_log.h"
#include "esp_gmf_io_file.h"
#include "esp_gmf_io_http.h"
#include "esp_gmf_event.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_app_unit_test.h"
#include "freertos/FreeRTOS.h"

#define TAG  "GMF_IO_TEST"

#define TEST_HTTP_URL1  "https://dl.espressif.com/dl/audio/ff-16b-1c-44100hz.mp3"
#define TEST_HTTP_URL2  "https://dl.espressif.com/dl/audio/gs-16b-2c-44100hz.mp3"
#define TEST_HTTP_URL3  "http://10.19.4.111:8000/v1/audio_files/ut/gmf_ut_test.mp3"

static esp_gmf_io_handle_t create_test_http_io(bool with_data_bus)
{
    http_io_cfg_t config = HTTP_STREAM_CFG_DEFAULT();
    config.dir = ESP_GMF_IO_DIR_READER;

    if (!with_data_bus) {
        config.io_cfg.buffer_cfg.buffer_size = 0;
        config.io_cfg.buffer_cfg.io_size = 0;
        config.io_cfg.thread.stack = 0;
        config.io_cfg.thread.prio = 0;
    }

    esp_gmf_io_handle_t io = NULL;
    esp_gmf_err_t err = esp_gmf_io_http_init(&config, &io);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    TEST_ASSERT_NOT_NULL(io);

    err = esp_gmf_io_set_uri(io, TEST_HTTP_URL1);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);

    return io;
}

TEST_CASE("HTTP Seek - Basic Operations with DataBus", "[IO_HTTP][leaks=10000]")
{
    esp_log_level_set("*", ESP_LOG_INFO);

    esp_gmf_app_test_case_uses_tcpip();
    esp_gmf_app_wifi_connect();
    esp_gmf_io_handle_t io = create_test_http_io(true);

    esp_gmf_err_t err = esp_gmf_io_open(io);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);

    uint64_t pos = 0;

    err = esp_gmf_io_seek(io, 0);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);

    err = esp_gmf_io_seek(io, 100 * 1024);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    err = esp_gmf_io_get_pos(io, &pos);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    TEST_ASSERT_GREATER_OR_EQUAL(100 * 1024, pos);

    err = esp_gmf_io_get_pos(io, &pos);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    uint64_t current_pos = pos;
    err = esp_gmf_io_seek(io, current_pos);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    err = esp_gmf_io_get_pos(io, &pos);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    TEST_ASSERT_GREATER_OR_EQUAL(current_pos, pos);

    err = esp_gmf_io_close(io);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    err = esp_gmf_obj_delete(io);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);

    esp_gmf_app_wifi_disconnect();
}

TEST_CASE("HTTP Seek - Operations without DataBus", "[IO_HTTP][leaks=10000]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_gmf_app_test_case_uses_tcpip();
    esp_gmf_app_wifi_connect();
    esp_gmf_io_handle_t io = create_test_http_io(false);

    esp_gmf_err_t err = esp_gmf_io_open(io);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);

    uint64_t pos = 0;

    err = esp_gmf_io_seek(io, 50 * 1024);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    err = esp_gmf_io_get_pos(io, &pos);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    TEST_ASSERT_EQUAL(50 * 1024, pos);

    err = esp_gmf_io_seek(io, 25 * 1024);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    err = esp_gmf_io_get_pos(io, &pos);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    TEST_ASSERT_EQUAL(25 * 1024, pos);

    err = esp_gmf_io_seek(io, 300 * 1024);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    err = esp_gmf_io_get_pos(io, &pos);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    TEST_ASSERT_EQUAL(300 * 1024, pos);

    err = esp_gmf_io_close(io);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    err = esp_gmf_obj_delete(io);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);

    esp_gmf_app_wifi_disconnect();
}

TEST_CASE("HTTP Seek - Error Conditions", "[IO_HTTP][leaks=10000]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_gmf_app_test_case_uses_tcpip();
    esp_gmf_app_wifi_connect();

    esp_gmf_err_t err = esp_gmf_io_seek(NULL, 0);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, err);

    esp_gmf_io_handle_t io = create_test_http_io(true);
    err = esp_gmf_io_open(io);
    if (err == ESP_GMF_ERR_OK) {
        err = esp_gmf_io_set_size(io, 100 * 1024);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
        err = esp_gmf_io_seek(io, 200 * 1024);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OUT_OF_RANGE, err);
    }

    if (io) {
        esp_gmf_io_close(io);
        esp_gmf_obj_delete(io);
    }

    esp_gmf_app_wifi_disconnect();
}

static void test_http_read_and_verify(esp_gmf_io_handle_t io, const char *url, esp_gmf_payload_t *payload, size_t buf_size)
{
    esp_gmf_info_file_t info_file = {0};
    esp_gmf_io_get_info(io, &info_file);
    ESP_LOGI(TAG, "%s size: %lld", url, info_file.size);
    TEST_ASSERT_GREATER_THAN(0, info_file.size);

    int total_read = 0;
    while (1) {
        esp_gmf_err_io_t ret = esp_gmf_io_acquire_read(io, payload, buf_size, portMAX_DELAY);
        total_read += payload->valid_size;
        esp_gmf_io_release_read(io, payload, portMAX_DELAY);
        if (ret != ESP_GMF_IO_OK || payload->is_done) {
            break;
        }
    }
    ESP_LOGI(TAG, "Read %d bytes from %s", total_read, url);
    TEST_ASSERT_EQUAL((int)info_file.size, total_read);
}

TEST_CASE("HTTP Reload Test", "[IO_HTTP][leaks=10000]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_gmf_app_test_case_uses_tcpip();
    esp_gmf_app_wifi_connect();
    char *current_uri = NULL;
    uint8_t buf[1024];
    esp_gmf_payload_t payload = {
        .buf = buf,
        .valid_size = 0,
        .buf_length = sizeof(buf),
    };
    http_io_cfg_t config = HTTP_STREAM_CFG_DEFAULT();
    config.dir = ESP_GMF_IO_DIR_READER;
    esp_gmf_io_handle_t io = NULL;
    esp_gmf_err_t err = esp_gmf_io_http_init(&config, &io);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    TEST_ASSERT_NOT_NULL(io);
    err = esp_gmf_io_set_uri(io, TEST_HTTP_URL1);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);

    err = esp_gmf_io_open(io);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    test_http_read_and_verify(io, TEST_HTTP_URL1, &payload, sizeof(buf));

    err = esp_gmf_io_reload(io, TEST_HTTP_URL2);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    test_http_read_and_verify(io, TEST_HTTP_URL2, &payload, sizeof(buf));
    esp_gmf_io_get_uri(io, &current_uri);
    TEST_ASSERT_EQUAL_STRING(TEST_HTTP_URL2, current_uri);

    err = esp_gmf_io_reload(io, TEST_HTTP_URL3);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    test_http_read_and_verify(io, TEST_HTTP_URL3, &payload, sizeof(buf));
    esp_gmf_io_get_uri(io, &current_uri);
    TEST_ASSERT_EQUAL_STRING(TEST_HTTP_URL3, current_uri);

    err = esp_gmf_io_close(io);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    err = esp_gmf_obj_delete(io);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);

    esp_gmf_app_wifi_disconnect();
}

TEST_CASE("File Seek - Seek After Finished", "[IO_FILE]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);

    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);
    TEST_ASSERT_NOT_NULL(sdcard_handle);

    const char *file_path = "/sdcard/test_short.mp3";
    file_io_cfg_t config = FILE_IO_CFG_DEFAULT();
    config.dir = ESP_GMF_IO_DIR_READER;
    // Set buffer size to allow task to finish quickly
    config.io_cfg.buffer_cfg.io_size = 512;
    config.io_cfg.buffer_cfg.buffer_size = 4096;
    config.io_cfg.thread.stack = 4096;
    config.io_cfg.thread.prio = 5;
    config.io_cfg.thread.core = 0;
    esp_gmf_io_handle_t io = NULL;
    esp_gmf_err_t err = esp_gmf_io_file_init(&config, &io);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);
    TEST_ASSERT_NOT_NULL(io);

    err = esp_gmf_io_set_uri(io, file_path);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);

    err = esp_gmf_io_open(io);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, err);

    /* Wait for task to finish reading all data into the data_bus naturally */
    ESP_LOGI(TAG, "Waiting for task to finish reading naturally...");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    esp_gmf_event_state_t st;
    esp_gmf_io_t *io_obj = (esp_gmf_io_t *)io;
    esp_gmf_task_get_state(io_obj->task_hd, &st);
    ESP_LOGI(TAG, "Task state after wait: %p-%s", io_obj->task_hd, esp_gmf_event_get_state_str(st));
    TEST_ASSERT_EQUAL(ESP_GMF_EVENT_STATE_FINISHED, st);

    /* Consume some data to advance 'pos' and create space for backward seek cache miss */
    uint8_t buf[512];
    esp_gmf_payload_t payload = {
        .buf = buf,
        .buf_length = sizeof(buf),
    };
    esp_gmf_io_acquire_read(io, &payload, sizeof(buf), portMAX_DELAY);
    esp_gmf_io_release_read(io, &payload, portMAX_DELAY);
    uint64_t pos = 0;
    esp_gmf_io_get_pos(io, &pos);
    ESP_LOGI(TAG, "Consumed %llu bytes, pos is now %llu", (uint64_t)payload.valid_size, pos);
    TEST_ASSERT_EQUAL(payload.valid_size, (uint32_t)pos);

    /* Perform seek to 0. Since pos=512 and target=0, it's a backward seek
       and it won't be in the cache if we just consumed the start. */
    uint64_t seek_target = 0;
    ESP_LOGI(TAG, "Performing seek to %llu...", seek_target);
    err = esp_gmf_io_seek(io, seek_target);
    TEST_ASSERT_EQUAL(err, ESP_GMF_ERR_OK);

    /* Verify task restarted and state is no longer FINISHED */
    esp_gmf_task_get_state(io_obj->task_hd, &st);
    ESP_LOGI(TAG, "Task state after seek: %s", esp_gmf_event_get_state_str(st));
    TEST_ASSERT_NOT_EQUAL(ESP_GMF_EVENT_STATE_FINISHED, st);

    /* Verify data flows again */
    vTaskDelay(200 / portTICK_PERIOD_MS);
    uint64_t new_pos = 0;
    esp_gmf_io_get_pos(io, &new_pos);
    TEST_ASSERT_EQUAL(0, new_pos);

    esp_gmf_io_close(io);
    esp_gmf_obj_delete(io);

    esp_gmf_app_teardown_sdcard(sdcard_handle);
    ESP_GMF_MEM_SHOW(TAG);
}

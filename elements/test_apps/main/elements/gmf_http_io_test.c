/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief  HTTP IO Test
 *         Test http_io interface using real HTTP connections.
 *         Note: This test requires network connectivity.
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "esp_log.h"
#include "esp_gmf_io.h"
#include "esp_gmf_io_http.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_app_unit_test.h"
#include "freertos/FreeRTOS.h"

#define TAG  "HTTP_IO_TEST"

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

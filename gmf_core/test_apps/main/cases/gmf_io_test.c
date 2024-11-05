/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2024 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "unity.h"
#include "esp_log.h"
#include "esp_gmf_oal_mem.h"
#include "gmf_fake_io.h"

static const char *TAG = "TEST_GMF_FAKE_IO";

TEST_CASE("GMF IO read and write", "ESP_GMF_IO")
{
    esp_log_level_set("*", ESP_LOG_DEBUG);

    ESP_GMF_MEM_SHOW(TAG);
    fake_io_cfg_t cfg = FAKE_IO_CFG_DEFAULT();
    cfg.dir = ESP_GMF_IO_DIR_READER;
    esp_gmf_io_handle_t reader = NULL;
    fake_io_init(&cfg, &reader);
    fake_io_cast(&cfg, reader);
    TEST_ASSERT_NOT_NULL(reader);
    ESP_GMF_MEM_SHOW(TAG);

    esp_gmf_io_set_uri(reader, "test.mp3");
    char *rd_uri = NULL;
    esp_gmf_io_get_uri(reader, &rd_uri);

    TEST_ASSERT_EQUAL_STRING_LEN("test.mp3", rd_uri, strlen("test.mp3"));
    int r_ret = esp_gmf_io_open(reader);

    ESP_GMF_MEM_SHOW(TAG);
    uint64_t reader_total_bytes = 0;
    uint64_t pos = 0;
    esp_gmf_io_get_size(reader, &reader_total_bytes);
    ESP_LOGI(TAG, "READER reader_total_bytes:%lld", reader_total_bytes);

    cfg.dir = ESP_GMF_IO_DIR_WRITER;
    esp_gmf_io_handle_t writer = NULL;
    fake_io_init(&cfg, &writer);
    TEST_ASSERT_NOT_NULL(writer);
    fake_io_cast(&cfg, writer);

    esp_gmf_io_set_uri(writer, "test1.mp3");
    char *wr_uri = NULL;
    esp_gmf_io_get_uri(writer, &wr_uri);
    TEST_ASSERT_EQUAL_STRING_LEN("test1.mp3", wr_uri, strlen("test1.mp3"));

    int w_ret = esp_gmf_io_open(writer);
    TEST_ASSERT_EQUAL(w_ret, ESP_GMF_ERR_OK);

    int read_len = 4 * 1024;
    int k = 0;
    while (k++ < 4) {
        esp_gmf_payload_t in_load = {0};
        esp_gmf_payload_t out_load = {0};
        r_ret = esp_gmf_io_acquire_read(reader, &in_load, read_len, 0);
        if (r_ret == 0) {
            ESP_LOGI(TAG, "Read DONE");
            uint64_t total_bytes = 0;
            esp_gmf_io_get_size(reader, &total_bytes);
            ESP_LOGI(TAG, "w_total:%lld", total_bytes);
            break;
        }
        w_ret = esp_gmf_io_acquire_write(writer, &out_load, r_ret, 0);
        out_load.valid_size = in_load.valid_size;
        esp_gmf_io_release_read(reader, &in_load, 0);
        esp_gmf_io_release_write(writer, &out_load, 0);

        esp_gmf_io_get_pos(reader, &pos);
        ESP_LOGI(TAG, "RD pos:%lld", pos);
        esp_gmf_io_get_pos(writer, &pos);
        ESP_LOGI(TAG, "WR pos:%lld", pos);
    }
    esp_gmf_io_close(reader);
    esp_gmf_obj_delete(reader);

    esp_gmf_io_close(writer);
    esp_gmf_obj_delete(writer);

    ESP_GMF_MEM_SHOW(TAG);
}
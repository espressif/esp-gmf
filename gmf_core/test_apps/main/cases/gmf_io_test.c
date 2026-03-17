/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "unity.h"
#include "esp_log.h"
#include "esp_gmf_oal_mem.h"
#include "gmf_fake_io.h"

static const char *TAG = "TEST_GMF_FAKE_IO";

TEST_CASE("GMF IO read and write", "[ESP_GMF_IO]")
{
    esp_log_level_set("*", ESP_LOG_DEBUG);

    ESP_GMF_MEM_SHOW(TAG);
    fake_io_cfg_t cfg = FAKE_IO_CFG_DEFAULT();
    cfg.dir = ESP_GMF_IO_DIR_READER;
    esp_gmf_io_handle_t reader = NULL;
    fake_io_init(&cfg, &reader);
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

TEST_CASE("GMF IO done and clear_done without task", "[ESP_GMF_IO]")
{
    esp_log_level_set("*", ESP_LOG_DEBUG);

    ESP_GMF_MEM_SHOW(TAG);
    fake_io_cfg_t cfg = FAKE_IO_CFG_DEFAULT();
    cfg.dir = ESP_GMF_IO_DIR_READER;
    esp_gmf_io_handle_t reader = NULL;
    fake_io_init(&cfg, &reader);
    TEST_ASSERT_NOT_NULL(reader);
    ESP_GMF_MEM_SHOW(TAG);

    esp_gmf_io_set_uri(reader, "test.mp3");
    int ret = esp_gmf_io_open(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    ret = esp_gmf_io_done(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    esp_gmf_payload_t load = {0};
    ret = esp_gmf_io_acquire_read(reader, &load, 1024, 0);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_IO_OK);
    TEST_ASSERT_TRUE(load.is_done);

    ret = esp_gmf_io_clear_done(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    memset(&load, 0, sizeof(esp_gmf_payload_t));
    ret = esp_gmf_io_acquire_read(reader, &load, 1024, 0);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_IO_OK);
    TEST_ASSERT_FALSE(load.is_done);

    esp_gmf_io_close(reader);
    esp_gmf_obj_delete(reader);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("GMF IO done and clear_done with task", "[ESP_GMF_IO]")
{
    esp_log_level_set("*", ESP_LOG_DEBUG);

    ESP_GMF_MEM_SHOW(TAG);
    fake_io_cfg_t cfg = FAKE_IO_CFG_DEFAULT();
    cfg.dir = ESP_GMF_IO_DIR_READER;
    esp_gmf_io_handle_t reader = NULL;
    fake_io_init(&cfg, &reader);
    TEST_ASSERT_NOT_NULL(reader);
    ESP_GMF_MEM_SHOW(TAG);

    esp_gmf_io_cfg_t io_cfg = {
        .thread = {
            .stack = 4096,
            .prio = 5,
            .core = 0,
            .stack_in_ext = false,
        },
        .buffer_cfg = {
            .io_size = 4096,
            .buffer_size = 16384,
        },
        .enable_speed_monitor = false,
    };

    fake_io_t *fake_reader = (fake_io_t *)reader;
    memcpy(&fake_reader->base.io_cfg, &io_cfg, sizeof(esp_gmf_io_cfg_t));
    esp_gmf_io_set_uri(reader, "test.mp3");
    int ret = esp_gmf_io_open(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    TEST_ASSERT_NOT_NULL(fake_reader->base.data_bus);
    TEST_ASSERT_NOT_NULL(fake_reader->base.task_hd);

    vTaskDelay(100 / portTICK_PERIOD_MS);

    ret = esp_gmf_io_done(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    esp_gmf_payload_t load = {0};
    ret = esp_gmf_io_acquire_read(reader, &load, 1024, 0);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_IO_OK);
    TEST_ASSERT_TRUE(load.is_done);

    ret = esp_gmf_io_clear_done(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    memset(&load, 0, sizeof(esp_gmf_payload_t));
    ret = esp_gmf_io_acquire_read(reader, &load, 1024, 200 / portTICK_PERIOD_MS);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_IO_OK);
    TEST_ASSERT_FALSE(load.is_done);

    esp_gmf_io_close(reader);
    esp_gmf_obj_delete(reader);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("GMF IO abort and clear_abort without task", "[ESP_GMF_IO]")
{
    esp_log_level_set("*", ESP_LOG_DEBUG);

    ESP_GMF_MEM_SHOW(TAG);
    fake_io_cfg_t cfg = FAKE_IO_CFG_DEFAULT();
    cfg.dir = ESP_GMF_IO_DIR_READER;
    esp_gmf_io_handle_t reader = NULL;
    fake_io_init(&cfg, &reader);
    TEST_ASSERT_NOT_NULL(reader);
    ESP_GMF_MEM_SHOW(TAG);

    esp_gmf_io_set_uri(reader, "test.mp3");
    int ret = esp_gmf_io_open(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    ret = esp_gmf_io_abort(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    esp_gmf_payload_t load = {0};
    ret = esp_gmf_io_acquire_read(reader, &load, 1024, 0);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_IO_ABORT);

    ret = esp_gmf_io_clear_abort(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    memset(&load, 0, sizeof(esp_gmf_payload_t));
    ret = esp_gmf_io_acquire_read(reader, &load, 1024, 0);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_IO_OK);
    TEST_ASSERT_FALSE(load.is_done);

    esp_gmf_io_close(reader);
    esp_gmf_obj_delete(reader);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("GMF IO abort and clear_abort with task", "[ESP_GMF_IO]")
{
    esp_log_level_set("*", ESP_LOG_DEBUG);

    ESP_GMF_MEM_SHOW(TAG);
    fake_io_cfg_t cfg = FAKE_IO_CFG_DEFAULT();
    cfg.dir = ESP_GMF_IO_DIR_READER;
    esp_gmf_io_handle_t reader = NULL;
    fake_io_init(&cfg, &reader);
    TEST_ASSERT_NOT_NULL(reader);
    ESP_GMF_MEM_SHOW(TAG);

    esp_gmf_io_cfg_t io_cfg = {
        .thread = {
            .stack = 4096,
            .prio = 5,
            .core = 0,
            .stack_in_ext = false,
        },
        .buffer_cfg = {
            .io_size = 4096,
            .buffer_size = 16384,
        },
        .enable_speed_monitor = false,
    };

    fake_io_t *fake_reader = (fake_io_t *)reader;
    memcpy(&fake_reader->base.io_cfg, &io_cfg, sizeof(esp_gmf_io_cfg_t));
    esp_gmf_io_set_uri(reader, "test.mp3");
    int ret = esp_gmf_io_open(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    TEST_ASSERT_NOT_NULL(fake_reader->base.data_bus);
    TEST_ASSERT_NOT_NULL(fake_reader->base.task_hd);

    vTaskDelay(100 / portTICK_PERIOD_MS);

    ret = esp_gmf_io_abort(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    esp_gmf_payload_t load = {0};
    ret = esp_gmf_io_acquire_read(reader, &load, 1024, 0);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_IO_ABORT);

    ret = esp_gmf_io_clear_abort(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    memset(&load, 0, sizeof(esp_gmf_payload_t));
    ret = esp_gmf_io_acquire_read(reader, &load, 1024, 0);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_IO_OK);
    TEST_ASSERT_FALSE(load.is_done);

    esp_gmf_io_close(reader);
    esp_gmf_obj_delete(reader);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("GMF IO async read", "[ESP_GMF_IO]")
{
    esp_log_level_set("*", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);

    fake_io_cfg_t cfg = FAKE_IO_CFG_DEFAULT();
    cfg.dir = ESP_GMF_IO_DIR_READER;
    esp_gmf_io_handle_t reader = NULL;
    fake_io_init(&cfg, &reader);
    TEST_ASSERT_NOT_NULL(reader);

    esp_gmf_io_cfg_t io_cfg = {
        .thread = {.stack = 4096, .prio = 5, .core = 0},
        .buffer_cfg = {.io_size = 4096, .buffer_size = 16384},
    };
    fake_io_t *fake_reader = (fake_io_t *)reader;
    memcpy(&fake_reader->base.io_cfg, &io_cfg, sizeof(esp_gmf_io_cfg_t));
    esp_gmf_io_set_uri(reader, "test.mp3");
    int ret = esp_gmf_io_open(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_NULL(fake_reader->base.data_bus);
    TEST_ASSERT_NOT_NULL(fake_reader->base.task_hd);

    vTaskDelay(200 / portTICK_PERIOD_MS);
    int read_count = 0;
    while (read_count < 3) {
        esp_gmf_payload_t load = {0};
        esp_gmf_err_io_t io_ret = esp_gmf_io_acquire_read(reader, &load, 4096, 200 / portTICK_PERIOD_MS);
        if (io_ret != ESP_GMF_IO_OK) {
            break;
        }
        TEST_ASSERT_GREATER_THAN(0, load.valid_size);
        esp_gmf_io_release_read(reader, &load, 0);
        read_count++;
    }
    TEST_ASSERT_GREATER_THAN(0, read_count);

    uint64_t pos = 0;
    esp_gmf_io_get_pos(reader, &pos);
    ESP_LOGI(TAG, "Async reader pos after %d reads: %llu", read_count, pos);
    TEST_ASSERT_GREATER_THAN(0, pos);

    esp_gmf_io_close(reader);
    esp_gmf_obj_delete(reader);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("GMF IO async seek within buffer", "[ESP_GMF_IO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);

    fake_io_cfg_t cfg = FAKE_IO_CFG_DEFAULT();
    cfg.dir = ESP_GMF_IO_DIR_READER;
    esp_gmf_io_handle_t reader = NULL;
    fake_io_init(&cfg, &reader);
    TEST_ASSERT_NOT_NULL(reader);

    esp_gmf_io_cfg_t io_cfg = {
        .thread = {.stack = 4096, .prio = 5, .core = 0},
        .buffer_cfg = {.io_size = 4096, .buffer_size = 16384},
    };
    fake_io_t *fake_reader = (fake_io_t *)reader;
    memcpy(&fake_reader->base.io_cfg, &io_cfg, sizeof(esp_gmf_io_cfg_t));
    esp_gmf_io_set_uri(reader, "test.mp3");
    int ret = esp_gmf_io_open(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    vTaskDelay(300 / portTICK_PERIOD_MS);
    uint64_t pos = 0;
    esp_gmf_io_get_pos(reader, &pos);
    uint32_t filled_size = 0;
    esp_gmf_db_get_filled_size(fake_reader->base.data_bus, &filled_size);
    ESP_LOGI(TAG, "Before seek: pos=%llu, filled_size=%u", pos, filled_size);
    TEST_ASSERT_GREATER_THAN(0, filled_size);

    uint64_t seek_target = pos + filled_size / 2;
    fake_reader->seek_called_count = 0;
    ret = esp_gmf_io_seek(reader, seek_target);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    ESP_LOGI(TAG, "After buffer seek: seek_called_count=%u", fake_reader->seek_called_count);
    TEST_ASSERT_EQUAL(0, fake_reader->seek_called_count);

    esp_gmf_io_close(reader);
    esp_gmf_obj_delete(reader);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("GMF IO async seek full", "[ESP_GMF_IO]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);

    fake_io_cfg_t cfg = FAKE_IO_CFG_DEFAULT();
    cfg.dir = ESP_GMF_IO_DIR_READER;
    esp_gmf_io_handle_t reader = NULL;
    fake_io_init(&cfg, &reader);
    TEST_ASSERT_NOT_NULL(reader);

    esp_gmf_io_cfg_t io_cfg = {
        .thread = {.stack = 4096, .prio = 5, .core = 0},
        .buffer_cfg = {.io_size = 4096, .buffer_size = 16384},
    };
    fake_io_t *fake_reader = (fake_io_t *)reader;
    memcpy(&fake_reader->base.io_cfg, &io_cfg, sizeof(esp_gmf_io_cfg_t));
    esp_gmf_io_set_uri(reader, "test.mp3");
    int ret = esp_gmf_io_open(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    vTaskDelay(200 / portTICK_PERIOD_MS);
    uint64_t pos = 0;
    esp_gmf_io_get_pos(reader, &pos);
    ESP_LOGI(TAG, "Before full seek: pos=%llu", pos);

    uint64_t seek_target = pos + 30000;
    fake_reader->seek_called_count = 0;
    ret = esp_gmf_io_seek(reader, seek_target);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    ESP_LOGI(TAG, "After full seek: seek_called_count=%u", fake_reader->seek_called_count);
    TEST_ASSERT_GREATER_THAN(0, fake_reader->seek_called_count);

    esp_gmf_io_get_pos(reader, &pos);
    TEST_ASSERT_GREATER_OR_EQUAL(seek_target, pos);

    esp_gmf_io_close(reader);
    esp_gmf_obj_delete(reader);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("GMF IO close after done", "[ESP_GMF_IO]")
{
    esp_log_level_set("*", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);

    fake_io_cfg_t cfg = FAKE_IO_CFG_DEFAULT();
    cfg.dir = ESP_GMF_IO_DIR_READER;
    esp_gmf_io_handle_t reader = NULL;
    fake_io_init(&cfg, &reader);
    TEST_ASSERT_NOT_NULL(reader);

    esp_gmf_io_cfg_t io_cfg = {
        .thread = {.stack = 4096, .prio = 5, .core = 0},
        .buffer_cfg = {.io_size = 4096, .buffer_size = 16384},
    };
    fake_io_t *fake_reader = (fake_io_t *)reader;
    memcpy(&fake_reader->base.io_cfg, &io_cfg, sizeof(esp_gmf_io_cfg_t));
    esp_gmf_io_set_uri(reader, "test.mp3");
    int ret = esp_gmf_io_open(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    vTaskDelay(100 / portTICK_PERIOD_MS);
    ret = esp_gmf_io_done(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    ret = esp_gmf_io_close(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    esp_gmf_obj_delete(reader);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("GMF IO reset with running task", "[ESP_GMF_IO]")
{
    esp_log_level_set("*", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);

    fake_io_cfg_t cfg = FAKE_IO_CFG_DEFAULT();
    cfg.dir = ESP_GMF_IO_DIR_READER;
    esp_gmf_io_handle_t reader = NULL;
    fake_io_init(&cfg, &reader);
    TEST_ASSERT_NOT_NULL(reader);

    esp_gmf_io_cfg_t io_cfg = {
        .thread = {.stack = 4096, .prio = 5, .core = 0},
        .buffer_cfg = {.io_size = 4096, .buffer_size = 16384},
    };
    fake_io_t *fake_reader = (fake_io_t *)reader;
    memcpy(&fake_reader->base.io_cfg, &io_cfg, sizeof(esp_gmf_io_cfg_t));

    esp_gmf_io_set_uri(reader, "test.mp3");
    int ret = esp_gmf_io_open(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    vTaskDelay(200 / portTICK_PERIOD_MS);

    uint32_t filled_size = 0;
    esp_gmf_db_get_filled_size(fake_reader->base.data_bus, &filled_size);
    TEST_ASSERT_GREATER_THAN(0, filled_size);
    ESP_LOGI(TAG, "Filled size before close: %u", filled_size);

    ret = esp_gmf_io_close(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    ret = esp_gmf_io_reset(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    esp_gmf_db_get_filled_size(fake_reader->base.data_bus, &filled_size);
    TEST_ASSERT_EQUAL(0, filled_size);
    ESP_LOGI(TAG, "Filled size after reset: %u", filled_size);

    ret = esp_gmf_io_open(reader);
    TEST_ASSERT_EQUAL(ret, ESP_GMF_ERR_OK);

    /* Verify data flows again after reset */
    vTaskDelay(200 / portTICK_PERIOD_MS);
    esp_gmf_db_get_filled_size(fake_reader->base.data_bus, &filled_size);
    TEST_ASSERT_GREATER_THAN(0, filled_size);
    ESP_LOGI(TAG, "Filled size after reset: %u", filled_size);

    esp_gmf_io_close(reader);
    esp_gmf_obj_delete(reader);
    ESP_GMF_MEM_SHOW(TAG);
}

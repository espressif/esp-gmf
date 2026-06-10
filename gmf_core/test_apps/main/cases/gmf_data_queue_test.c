/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_gmf_data_queue.h"
#include "esp_gmf_new_databus.h"

#define DATA_QUEUE_TEST_ALIGN_BYTES  (32)
#define DATA_QUEUE_TEST_ITEM_NUM     (16)
#define DATA_QUEUE_TEST_TIMEOUT_MS   (100)

typedef struct {
    esp_gmf_data_queue_t *queue;
    int                   base;
    int                   count;
    volatile bool        *done;
} data_queue_writer_arg_t;

typedef struct {
    esp_gmf_data_queue_t *queue;
    int                   total;
    volatile int         *read_count;
    volatile bool        *done;
} data_queue_reader_arg_t;

typedef struct {
    esp_gmf_data_queue_t *queue;
    volatile int          ret;
    volatile bool         done;
} data_queue_wait_arg_t;

static void data_queue_writer_task(void *param)
{
    data_queue_writer_arg_t *arg = (data_queue_writer_arg_t *)param;
    for (int i = 0; i < arg->count; i++) {
        void *data = NULL;
        if (esp_gmf_data_queue_acquire_write(arg->queue, &data, sizeof(int), ESP_GMF_DATA_QUEUE_WAIT_FOREVER) != 0 ||
            data == NULL) {
            break;
        }
        *(int *)data = arg->base + i;
        esp_gmf_data_queue_release_write(arg->queue, sizeof(int));
    }
    *arg->done = true;
    vTaskDelete(NULL);
}

static void data_queue_reader_task(void *param)
{
    data_queue_reader_arg_t *arg = (data_queue_reader_arg_t *)param;
    while (*arg->read_count < arg->total) {
        void *data = NULL;
        int size = 0;
        if (esp_gmf_data_queue_acquire_read(arg->queue, &data, &size, ESP_GMF_DATA_QUEUE_WAIT_FOREVER) != 0) {
            break;
        }
        if (data && size == sizeof(int)) {
            (*arg->read_count)++;
        }
        esp_gmf_data_queue_release_read(arg->queue);
    }
    *arg->done = true;
    vTaskDelete(NULL);
}

static void data_queue_wait_read_task(void *param)
{
    data_queue_wait_arg_t *arg = (data_queue_wait_arg_t *)param;
    void *data = NULL;
    int size = 0;
    arg->ret = esp_gmf_data_queue_acquire_read(arg->queue, &data, &size, ESP_GMF_DATA_QUEUE_WAIT_FOREVER);
    arg->done = true;
    vTaskDelete(NULL);
}

TEST_CASE("Data queue keeps variable block sizes", "[ESP_GMF_DATA_QUEUE]")
{
    esp_gmf_data_queue_t *queue = esp_gmf_data_queue_create(128);
    TEST_ASSERT_NOT_NULL(queue);

    void *wr = NULL;
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_write(queue, &wr, 32, ESP_GMF_DATA_QUEUE_WAIT_FOREVER));
    TEST_ASSERT_NOT_NULL(wr);
    memcpy(wr, "hello", 5);
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_release_write(queue, 5));

    wr = NULL;
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_write(queue, &wr, 32, ESP_GMF_DATA_QUEUE_WAIT_FOREVER));
    TEST_ASSERT_NOT_NULL(wr);
    memcpy(wr, "esp-gmf", 7);
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_release_write(queue, 7));

    int q_num = 0;
    int q_size = 0;
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_query(queue, &q_num, &q_size));
    TEST_ASSERT_EQUAL(2, q_num);
    TEST_ASSERT_EQUAL(12, q_size);

    void *rd = NULL;
    int size = 0;
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_read(queue, &rd, &size, ESP_GMF_DATA_QUEUE_WAIT_FOREVER));
    TEST_ASSERT_EQUAL(5, size);
    TEST_ASSERT_EQUAL_MEMORY("hello", rd, 5);
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_release_read(queue));

    rd = NULL;
    size = 0;
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_read(queue, &rd, &size, ESP_GMF_DATA_QUEUE_WAIT_FOREVER));
    TEST_ASSERT_EQUAL(7, size);
    TEST_ASSERT_EQUAL_MEMORY("esp-gmf", rd, 7);
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_release_read(queue));

    esp_gmf_data_queue_destroy(queue);
}

TEST_CASE("Data queue works as block data bus", "[ESP_GMF_DATA_QUEUE]")
{
    esp_gmf_db_handle_t db = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_data_queue(128, 0, &db));
    TEST_ASSERT_NOT_NULL(db);

    esp_gmf_data_bus_block_t blk = {0};
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_acquire_write(db, &blk, 32, 0));
    TEST_ASSERT_NOT_NULL(blk.buf);
    TEST_ASSERT_EQUAL(32, blk.buf_length);

    memcpy(blk.buf, "short", 5);
    blk.valid_size = 5;
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_release_write(db, &blk, 0));

    memset(&blk, 0, sizeof(blk));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_acquire_read(db, &blk, 32, 0));
    TEST_ASSERT_EQUAL(5, blk.valid_size);
    TEST_ASSERT_EQUAL(5, blk.buf_length);
    TEST_ASSERT_EQUAL_MEMORY("short", blk.buf, 5);
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_release_read(db, &blk, 0));

    uint32_t filled = 1;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_get_filled_size(db, &filled));
    TEST_ASSERT_EQUAL(0, filled);

    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_acquire_write(db, &blk, 16, 0));
    memcpy(blk.buf, "frame1", 6);
    blk.valid_size = 6;
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_release_write(db, &blk, 0));

    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_acquire_write(db, &blk, 16, 0));
    memcpy(blk.buf, "frame2", 6);
    blk.valid_size = 6;
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_release_write(db, &blk, 0));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_done_write(db));

    memset(&blk, 0, sizeof(blk));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_acquire_read(db, &blk, 16, 0));
    TEST_ASSERT_EQUAL(6, blk.valid_size);
    TEST_ASSERT_EQUAL_MEMORY("frame1", blk.buf, 6);
    TEST_ASSERT_FALSE(blk.is_last);
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_release_read(db, &blk, 0));

    memset(&blk, 0, sizeof(blk));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_acquire_read(db, &blk, 16, 0));
    TEST_ASSERT_EQUAL(6, blk.valid_size);
    TEST_ASSERT_EQUAL_MEMORY("frame2", blk.buf, 6);
    TEST_ASSERT_TRUE(blk.is_last);
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_release_read(db, &blk, 0));

    memset(&blk, 0, sizeof(blk));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_acquire_read(db, &blk, 16, 0));
    TEST_ASSERT_EQUAL(0, blk.valid_size);
    TEST_ASSERT_TRUE(blk.is_last);
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_release_read(db, &blk, 0));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_reset_done_write(db));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_acquire_write(db, &blk, 16, 0));
    memcpy(blk.buf, "last", 4);
    blk.valid_size = 4;
    blk.is_last = true;
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_release_write(db, &blk, 0));

    memset(&blk, 0, sizeof(blk));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_acquire_read(db, &blk, 16, 0));
    TEST_ASSERT_EQUAL(4, blk.valid_size);
    TEST_ASSERT_EQUAL_MEMORY("last", blk.buf, 4);
    TEST_ASSERT_TRUE(blk.is_last);
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_release_read(db, &blk, 0));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_deinit(db));
}

TEST_CASE("Data queue bus supports aligned payload with metadata", "[ESP_GMF_DATA_QUEUE]")
{
    esp_gmf_db_handle_t db = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_data_queue(256, 0, &db));
    TEST_ASSERT_NOT_NULL(db);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_set_align(db, DATA_QUEUE_TEST_ALIGN_BYTES, 16));

    esp_gmf_data_bus_block_t blk = {0};
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_acquire_write(db, &blk, 23, 0));
    TEST_ASSERT_NOT_NULL(blk.buf);
    TEST_ASSERT_EQUAL(0, ((uintptr_t)blk.buf) & (DATA_QUEUE_TEST_ALIGN_BYTES - 1));
    TEST_ASSERT_EQUAL(32, blk.buf_length);

    memset(blk.buf, 0x5A, 23);
    blk.valid_size = 23;
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_release_write(db, &blk, 0));

    uint32_t filled = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_get_filled_size(db, &filled));
    TEST_ASSERT_EQUAL(23, filled);

    memset(&blk, 0, sizeof(blk));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_acquire_read(db, &blk, 32, 0));
    TEST_ASSERT_NOT_NULL(blk.buf);
    TEST_ASSERT_EQUAL(0, ((uintptr_t)blk.buf) & (DATA_QUEUE_TEST_ALIGN_BYTES - 1));
    TEST_ASSERT_EQUAL(23, blk.valid_size);
    TEST_ASSERT_EQUAL(23, blk.buf_length);
    for (int i = 0; i < blk.valid_size; i++) {
        TEST_ASSERT_EQUAL(0x5A, blk.buf[i]);
    }
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_release_read(db, &blk, 0));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_get_filled_size(db, &filled));
    TEST_ASSERT_EQUAL(0, filled);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_deinit(db));
}

TEST_CASE("Data queue supports concurrent producer and consumer tasks", "[ESP_GMF_DATA_QUEUE]")
{
    esp_gmf_data_queue_t *queue = esp_gmf_data_queue_create(512);
    TEST_ASSERT_NOT_NULL(queue);

    volatile bool writer1_done = false;
    volatile bool writer2_done = false;
    volatile bool reader_done = false;
    volatile int read_count = 0;
    data_queue_writer_arg_t writer1 = {
        .queue = queue,
        .base = 0,
        .count = DATA_QUEUE_TEST_ITEM_NUM,
        .done = &writer1_done,
    };
    data_queue_writer_arg_t writer2 = {
        .queue = queue,
        .base = 100,
        .count = DATA_QUEUE_TEST_ITEM_NUM,
        .done = &writer2_done,
    };
    data_queue_reader_arg_t reader = {
        .queue = queue,
        .total = DATA_QUEUE_TEST_ITEM_NUM * 2,
        .read_count = &read_count,
        .done = &reader_done,
    };

    xTaskCreate(data_queue_writer_task, "dq_w1", 2048, &writer1, 5, NULL);
    xTaskCreate(data_queue_writer_task, "dq_w2", 2048, &writer2, 5, NULL);
    xTaskCreate(data_queue_reader_task, "dq_r", 2048, &reader, 5, NULL);

    for (int i = 0; i < 200 && (!writer1_done || !writer2_done || !reader_done); i++) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    TEST_ASSERT_TRUE(writer1_done);
    TEST_ASSERT_TRUE(writer2_done);
    TEST_ASSERT_TRUE(reader_done);
    TEST_ASSERT_EQUAL(DATA_QUEUE_TEST_ITEM_NUM * 2, read_count);

    esp_gmf_data_queue_destroy(queue);
}

TEST_CASE("Data queue direct API timeout and abort behavior", "[ESP_GMF_DATA_QUEUE]")
{
    esp_gmf_data_queue_t *queue = esp_gmf_data_queue_create(64);
    TEST_ASSERT_NOT_NULL(queue);

    void *data = NULL;
    int size = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_IO_TIMEOUT,
                      esp_gmf_data_queue_acquire_read(queue, &data, &size, DATA_QUEUE_TEST_TIMEOUT_MS));

    void *wr = NULL;
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_write(queue, &wr, 32, ESP_GMF_DATA_QUEUE_WAIT_FOREVER));
    TEST_ASSERT_NOT_NULL(wr);
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_release_write(queue, 32));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_TIMEOUT,
                      esp_gmf_data_queue_acquire_write(queue, &wr, 32, DATA_QUEUE_TEST_TIMEOUT_MS));

    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_wakeup(queue));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_ABORT,
                      esp_gmf_data_queue_acquire_read(queue, &data, &size, DATA_QUEUE_TEST_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_ABORT,
                      esp_gmf_data_queue_acquire_write(queue, &wr, 1, DATA_QUEUE_TEST_TIMEOUT_MS));

    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_reset(queue));
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_write(queue, &wr, 16, ESP_GMF_DATA_QUEUE_WAIT_FOREVER));
    TEST_ASSERT_NOT_NULL(wr);
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_release_write(queue, 16));
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_read(queue, &data, &size, 0));
    TEST_ASSERT_EQUAL(16, size);
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_release_read(queue));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_TIMEOUT, esp_gmf_data_queue_acquire_read(queue, &data, &size, DATA_QUEUE_TEST_TIMEOUT_MS));
    esp_gmf_data_queue_destroy(queue);
}

TEST_CASE("Data queue bus timeout and abort behavior", "[ESP_GMF_DATA_QUEUE]")
{
    esp_gmf_db_handle_t db = NULL;
    esp_gmf_data_bus_block_t blk = {0};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_data_queue(64, 0, &db));
    TEST_ASSERT_NOT_NULL(db);

    TEST_ASSERT_EQUAL(ESP_GMF_IO_TIMEOUT, esp_gmf_db_acquire_read(db, &blk, 1, 1));

    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_acquire_write(db, &blk, 32, 0));
    blk.valid_size = 32;
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_release_write(db, &blk, 0));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_TIMEOUT, esp_gmf_db_acquire_write(db, &blk, 1, 1));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_abort(db));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_ABORT, esp_gmf_db_acquire_read(db, &blk, 1, 1));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_ABORT, esp_gmf_db_acquire_write(db, &blk, 1, 1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_clear_abort(db));

    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_acquire_read(db, &blk, 32, 0));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_release_read(db, &blk, 0));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_deinit(db));
}

TEST_CASE("Data queue wakeup unblocks direct reader", "[ESP_GMF_DATA_QUEUE]")
{
    esp_gmf_data_queue_t *queue = esp_gmf_data_queue_create(64);
    TEST_ASSERT_NOT_NULL(queue);

    data_queue_wait_arg_t arg = {
        .queue = queue,
        .ret = 0,
        .done = false,
    };
    xTaskCreate(data_queue_wait_read_task, "dq_wait", 2048, &arg, 5, NULL);
    vTaskDelay(20 / portTICK_PERIOD_MS);
    TEST_ASSERT_FALSE(arg.done);
    esp_gmf_data_queue_wakeup(queue);

    for (int i = 0; i < 50 && !arg.done; i++) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    TEST_ASSERT_TRUE(arg.done);
    TEST_ASSERT_EQUAL(ESP_GMF_IO_ABORT, arg.ret);

    esp_gmf_data_queue_destroy(queue);
}

TEST_CASE("Data queue handles empty full wrap and rewind edge cases", "[ESP_GMF_DATA_QUEUE]")
{
    esp_gmf_data_queue_t *queue = esp_gmf_data_queue_create(40);
    TEST_ASSERT_NOT_NULL(queue);
    bool have_data = true;
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_have_data(queue, &have_data));
    TEST_ASSERT_FALSE(have_data);

    void *wr = NULL;
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_write(queue, &wr, 16, ESP_GMF_DATA_QUEUE_WAIT_FOREVER));
    TEST_ASSERT_NOT_NULL(wr);
    memset(wr, 'A', 16);
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_release_write(queue, 16));
    TEST_ASSERT_EQUAL(16, esp_gmf_data_queue_get_available(queue));

    wr = NULL;
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_write(queue, &wr, 16, ESP_GMF_DATA_QUEUE_WAIT_FOREVER));
    TEST_ASSERT_NOT_NULL(wr);
    memset(wr, 'B', 16);
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_release_write(queue, 16));
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_get_available(queue));

    void *rd = NULL;
    int size = 0;
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_read(queue, &rd, &size, ESP_GMF_DATA_QUEUE_WAIT_FOREVER));
    TEST_ASSERT_EQUAL(16, size);
    TEST_ASSERT_EQUAL('A', ((uint8_t *)rd)[0]);
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_release_read(queue));

    wr = NULL;
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_write(queue, &wr, 12, ESP_GMF_DATA_QUEUE_WAIT_FOREVER));
    TEST_ASSERT_NOT_NULL(wr);
    memset(wr, 'C', 12);
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_release_write(queue, 12));

    rd = NULL;
    size = 0;
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_read(queue, &rd, &size, ESP_GMF_DATA_QUEUE_WAIT_FOREVER));
    TEST_ASSERT_EQUAL(16, size);
    TEST_ASSERT_EQUAL('B', ((uint8_t *)rd)[0]);
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_release_read(queue));
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_read(queue, &rd, &size, ESP_GMF_DATA_QUEUE_WAIT_FOREVER));
    TEST_ASSERT_EQUAL(12, size);
    TEST_ASSERT_EQUAL('C', ((uint8_t *)rd)[0]);
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_release_read(queue));
    esp_gmf_data_queue_destroy(queue);

    queue = esp_gmf_data_queue_create(64);
    TEST_ASSERT_NOT_NULL(queue);
    for (int i = 0; i < 3; i++) {
        wr = NULL;
        TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_write(queue, &wr, 4, ESP_GMF_DATA_QUEUE_WAIT_FOREVER));
        TEST_ASSERT_NOT_NULL(wr);
        memset(wr, 'A' + i, 4);
        TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_release_write(queue, 4));
    }
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_rewind(queue, 1));
    rd = NULL;
    size = 0;
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_read(queue, &rd, &size, ESP_GMF_DATA_QUEUE_WAIT_FOREVER));
    TEST_ASSERT_EQUAL('C', ((uint8_t *)rd)[0]);
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_release_read(queue));
    esp_gmf_data_queue_destroy(queue);

    queue = esp_gmf_data_queue_create(64);
    TEST_ASSERT_NOT_NULL(queue);
    for (int i = 0; i < 3; i++) {
        wr = NULL;
        TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_write(queue, &wr, 4, ESP_GMF_DATA_QUEUE_WAIT_FOREVER));
        TEST_ASSERT_NOT_NULL(wr);
        memset(wr, 'A' + i, 4);
        TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_release_write(queue, 4));
    }
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_rewind(queue, 3));
    rd = NULL;
    size = 0;
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_read(queue, &rd, &size, ESP_GMF_DATA_QUEUE_WAIT_FOREVER));
    TEST_ASSERT_EQUAL('A', ((uint8_t *)rd)[0]);
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_release_read(queue));
    TEST_ASSERT_NOT_EQUAL(0, esp_gmf_data_queue_rewind(queue, 10));
    esp_gmf_data_queue_destroy(queue);
}

TEST_CASE("Data queue reports invalid argument errors", "[ESP_GMF_DATA_QUEUE]")
{
    TEST_ASSERT_NULL(esp_gmf_data_queue_create(4));
    TEST_ASSERT_EQUAL(-1, esp_gmf_data_queue_acquire_write(NULL, NULL, 1, 0));
    TEST_ASSERT_NULL(esp_gmf_data_queue_get_write_data(NULL));
    TEST_ASSERT_NOT_EQUAL(0, esp_gmf_data_queue_release_write(NULL, 1));
    TEST_ASSERT_EQUAL(-1, esp_gmf_data_queue_acquire_read(NULL, NULL, NULL, 0));
    TEST_ASSERT_NOT_EQUAL(0, esp_gmf_data_queue_release_read(NULL));
    TEST_ASSERT_NOT_EQUAL(0, esp_gmf_data_queue_rewind(NULL, 1));
    TEST_ASSERT_NOT_EQUAL(0, esp_gmf_data_queue_query(NULL, NULL, NULL));
    TEST_ASSERT_EQUAL(-1, esp_gmf_data_queue_have_data(NULL, NULL));

    esp_gmf_db_handle_t db = NULL;
    esp_gmf_data_bus_block_t blk = {0};
    TEST_ASSERT_NOT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_data_queue(4, 0, &db));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_FAIL, esp_gmf_data_queue_db_acquire_write(NULL, &blk, 1, 0));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_FAIL, esp_gmf_data_queue_db_acquire_write(db, NULL, 1, 0));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_FAIL, esp_gmf_data_queue_db_acquire_read(NULL, &blk, 1, 0));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_FAIL, esp_gmf_data_queue_db_release_read(NULL, &blk, 0));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_FAIL, esp_gmf_data_queue_db_release_write(NULL, &blk, 0));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_data_queue_db_set_align(NULL, 1, 1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_data_queue_db_reset(NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_data_queue_db_abort(NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_data_queue_db_clear_abort(NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_data_queue_db_get_total_size(NULL, NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_data_queue_db_get_filled_size(NULL, NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_data_queue_db_get_free_size(NULL, NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_data_queue_db_done_write(NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_data_queue_db_reset_done_write(NULL));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_data_queue(128, 0, &db));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_db_set_align(db, 3, 1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_db_set_align(db, 1, 3));
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_acquire_write(db, &blk, 16, 0));
    uint8_t *saved_buf = blk.buf;
    blk.buf = saved_buf + 1;
    blk.valid_size = 1;
    TEST_ASSERT_EQUAL(ESP_GMF_IO_FAIL, esp_gmf_db_release_write(db, &blk, 0));
    blk.buf = saved_buf;
    blk.valid_size = blk.buf_length + 1;
    TEST_ASSERT_EQUAL(ESP_GMF_IO_FAIL, esp_gmf_db_release_write(db, &blk, 0));
    blk.valid_size = 1;
    TEST_ASSERT_EQUAL(ESP_GMF_IO_OK, esp_gmf_db_release_write(db, &blk, 0));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_deinit(db));
}

TEST_CASE("Data queue peek release does not consume data", "[ESP_GMF_DATA_QUEUE]")
{
    esp_gmf_data_queue_t *queue = esp_gmf_data_queue_create(64);
    TEST_ASSERT_NOT_NULL(queue);

    void *wr = NULL;
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_write(queue, &wr, 5, ESP_GMF_DATA_QUEUE_WAIT_FOREVER));
    TEST_ASSERT_NOT_NULL(wr);
    memcpy(wr, "peek", 5);
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_release_write(queue, 5));

    void *rd = NULL;
    int size = 0;
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_read(queue, &rd, &size, ESP_GMF_DATA_QUEUE_WAIT_FOREVER));
    TEST_ASSERT_EQUAL(5, size);
    TEST_ASSERT_EQUAL_MEMORY("peek", rd, 5);
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_peek_release(queue));

    rd = NULL;
    size = 0;
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_acquire_read(queue, &rd, &size, ESP_GMF_DATA_QUEUE_WAIT_FOREVER));
    TEST_ASSERT_EQUAL(5, size);
    TEST_ASSERT_EQUAL_MEMORY("peek", rd, 5);
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_release_read(queue));
    bool have_data = true;
    TEST_ASSERT_EQUAL(0, esp_gmf_data_queue_have_data(queue, &have_data));
    TEST_ASSERT_FALSE(have_data);

    esp_gmf_data_queue_destroy(queue);
}

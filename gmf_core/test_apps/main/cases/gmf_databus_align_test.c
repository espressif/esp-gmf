/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "esp_gmf_new_databus.h"

#define TEST_ALIGN_BYTES  (32)

static void assert_ptr_aligned(const void *ptr, unsigned align)
{
    TEST_ASSERT_NOT_NULL(ptr);
    if (align > 1u) {
        uintptr_t a = (uintptr_t)align;
        TEST_ASSERT((((uintptr_t)ptr) & (a - 1)) == 0);
    }
}

static void run_db_align_write_check(const char *name,
                                     int (*new_db)(int num, int item_cnt, esp_gmf_db_handle_t *h),
                                     int db_num,
                                     int db_item_cnt,
                                     uint32_t size1,
                                     uint32_t size2)
{
    esp_gmf_db_handle_t db = NULL;
    esp_gmf_data_bus_block_t blk = {0};

    TEST_ASSERT_EQUAL_MESSAGE(0, new_db(db_num, db_item_cnt, &db), name);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_GMF_ERR_OK, esp_gmf_db_set_align(db, TEST_ALIGN_BYTES, 1), name);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_GMF_IO_OK, esp_gmf_db_acquire_write(db, &blk, size1, 0), name);
    assert_ptr_aligned(blk.buf, TEST_ALIGN_BYTES);
    blk.valid_size = size1;
    TEST_ASSERT_EQUAL_MESSAGE(ESP_GMF_IO_OK, esp_gmf_db_release_write(db, &blk, 0), name);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_GMF_IO_OK, esp_gmf_db_acquire_write(db, &blk, size2, 0), name);
    assert_ptr_aligned(blk.buf, TEST_ALIGN_BYTES);
    blk.valid_size = size2;
    TEST_ASSERT_EQUAL_MESSAGE(ESP_GMF_IO_OK, esp_gmf_db_release_write(db, &blk, 0), name);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_GMF_ERR_OK, esp_gmf_db_deinit(db), name);
}

TEST_CASE("fifo: aligned write buffers via data bus", "[DATA_BUS]")
{
    run_db_align_write_check("fifo", esp_gmf_db_new_fifo, 8, 1, 64, 256);
}

TEST_CASE("block: aligned write buffers via data bus", "[DATA_BUS]")
{
    run_db_align_write_check("block", esp_gmf_db_new_block, 256, 4, 32, 64);
}

TEST_CASE("pbuf: aligned write buffers via data bus", "[DATA_BUS]")
{
    run_db_align_write_check("pbuf", esp_gmf_db_new_pbuf, 8, 1, 128, 256);
}

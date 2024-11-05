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

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_err.h"

#include "esp_gmf_data_bus.h"
#include "esp_gmf_ringbuffer.h"
#include "esp_gmf_block.h"
#include "esp_gmf_pbuf.h"

static const char *TAG = "NEW_DATA_BUS";

int esp_gmf_db_new_ringbuf(int num, int item_cnt, esp_gmf_db_handle_t *h)
{
    ESP_GMF_NULL_CHECK(TAG, h, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_rb_handle_t rb = NULL;
    esp_gmf_rb_create(num, item_cnt, &rb);
    ESP_GMF_NULL_CHECK(TAG, rb, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_db_config_t db_config = {
        .name = "ringbuffer",
        .type = DATA_BUS_TYPE_BYTE,
        .max_size = (item_cnt * num),
        .max_item_num = num,
        .child = rb,
    };
    esp_gmf_data_bus_t *db = NULL;
    if (ESP_GMF_ERR_OK != esp_gmf_db_init(&db_config, (esp_gmf_db_handle_t)&db)) {
        if (rb) {
            esp_gmf_rb_destroy(rb);
        }
        return ESP_GMF_ERR_MEMORY_LACK;
    }
    if (db == NULL) {
        ESP_LOGE(TAG, "DATA BUS is NULL");
        return ESP_GMF_ERR_FAIL;
    }
    db->op.deinit = esp_gmf_rb_destroy;
    db->op.acquire_read = esp_gmf_rb_acquire_read;
    db->op.release_read = esp_gmf_rb_release_read;
    db->op.acquire_write = esp_gmf_rb_acquire_write;
    db->op.release_write = esp_gmf_rb_release_write;
    db->op.done_write = esp_gmf_rb_done_write;
    db->op.reset_done_write = esp_gmf_rb_reset_done_write;
    db->op.reset = esp_gmf_rb_reset;
    db->op.abort = esp_gmf_rb_abort;
    db->op.get_total_size = esp_gmf_rb_get_size;
    db->op.get_filled_size = esp_gmf_rb_bytes_filled;
    db->op.get_available = esp_gmf_rb_bytes_available;
    ESP_LOGI(TAG, "New ringbuffer:%p, num:%d, item_cnt:%d, db:%p", rb, num, item_cnt, db);
    *h = db;
    return ESP_GMF_ERR_OK;
}

int esp_gmf_db_new_block(int num, int item_cnt, esp_gmf_db_handle_t *h)
{
    ESP_GMF_NULL_CHECK(TAG, h, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_block_handle_t handle = NULL;
    esp_gmf_block_create(num, item_cnt, &handle);
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_db_config_t db_config = {
        .name = "block",
        .type = DATA_BUS_TYPE_BLOCK,
        .max_size = (item_cnt * num),
        .max_item_num = num,
        .child = handle,
    };
    esp_gmf_data_bus_t *db = NULL;
    if (ESP_GMF_ERR_OK != esp_gmf_db_init(&db_config, (esp_gmf_db_handle_t)&db)) {
        if (handle) {
            esp_gmf_block_destroy(handle);
        }
        return ESP_GMF_ERR_MEMORY_LACK;
    }
    if (db == NULL) {
        ESP_LOGE(TAG, "DATA BUS is NULL");
        return ESP_GMF_ERR_FAIL;
    }
    db->op.deinit = esp_gmf_block_destroy;
    db->op.acquire_read = esp_gmf_block_acquire_read;
    db->op.release_read = esp_gmf_block_release_read;
    db->op.acquire_write = esp_gmf_block_acquire_write;
    db->op.release_write = esp_gmf_block_release_write;
    db->op.done_write = esp_gmf_block_done_write;
    db->op.reset = esp_gmf_block_reset;
    db->op.abort = esp_gmf_block_abort;
    db->op.get_total_size = esp_gmf_block_get_total_size;
    db->op.get_filled_size = esp_gmf_block_get_filled_size;
    db->op.get_available = esp_gmf_block_get_free_size;
    ESP_LOGI(TAG, "New block buf, num:%d, item_cnt:%d, db:%p", num, item_cnt, db);
    *h = db;
    return ESP_GMF_ERR_OK;
}

int esp_gmf_db_new_pbuf(int num, int item_cnt, esp_gmf_db_handle_t *h)
{
    ESP_GMF_NULL_CHECK(TAG, h, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_pbuf_handle_t handle = NULL;
    esp_gmf_pbuf_create(num, &handle);
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_db_config_t db_config = {
        .name = "pbuf",
        .type = DATA_BUS_TYPE_BLOCK,
        .max_size = (1 * num),
        .max_item_num = num,
        .child = handle,
    };
    esp_gmf_data_bus_t *db = NULL;
    if (ESP_GMF_ERR_OK != esp_gmf_db_init(&db_config, (esp_gmf_db_handle_t)&db)) {
        if (handle) {
            esp_gmf_pbuf_destroy(handle);
        }
        return ESP_GMF_ERR_MEMORY_LACK;
    }
    if (db == NULL) {
        ESP_LOGE(TAG, "DATA BUS is NULL");
        return ESP_GMF_ERR_FAIL;
    }
    db->op.deinit = esp_gmf_pbuf_destroy;
    db->op.acquire_read = esp_gmf_pbuf_acquire_read;
    db->op.release_read = esp_gmf_pbuf_release_read;
    db->op.acquire_write = esp_gmf_pbuf_acquire_write;
    db->op.release_write = esp_gmf_pbuf_release_write;
    db->op.done_write = esp_gmf_pbuf_done_write;
    db->op.reset = esp_gmf_pbuf_reset;
    db->op.abort = esp_gmf_pbuf_abort;
    db->op.get_total_size = NULL;
    db->op.get_filled_size = NULL;
    db->op.get_available = NULL;
    ESP_LOGI(TAG, "New pbuf, num:%d, item_cnt:%d, db:%p", num, item_cnt, db);
    *h = db;
    return ESP_GMF_ERR_OK;
}
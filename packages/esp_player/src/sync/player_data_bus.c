/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_gmf_oal_mem.h"

#include "player_data_bus.h"
#include "esp_gmf_new_databus.h"

static const char *TAG = "ESP_PLAYER_DATA_BUS";

#define PLAYER_DATA_BUS_MAGIC  0x50444253u  /* 'PDBS' */

typedef struct {
    uint64_t  pts;
    uint32_t  bytes_remain;
} data_bus_meta_item_t;

struct player_data_bus {
    uint32_t              magic;
    esp_gmf_db_handle_t   inner;
    SemaphoreHandle_t     lock;
    data_bus_meta_item_t *q;
    uint16_t              cap;
    uint16_t              head;
    uint16_t              count;
    uint32_t              lazy_block_cnt;   /*!< 0: inner provided by caller; else ensure_block may allocate */
    uint32_t              block_item_size;  /*!< Size of one decoded-frame slot; 0 if not allocated */
};

static inline int data_bus_push_meta(player_data_bus_t *bus, uint64_t pts, uint32_t bytes)
{
    if (bytes == 0) {
        return 0;
    }
    if (bus->count >= bus->cap) {
        return -1;
    }
    uint16_t tail = (uint16_t)((bus->head + bus->count) % bus->cap);
    bus->q[tail].pts = pts;
    bus->q[tail].bytes_remain = bytes;
    bus->count++;
    return 0;
}

static inline void data_bus_rollback_push(player_data_bus_t *bus)
{
    if (bus->count) {
        bus->count--;
    }
}

static inline void data_bus_apply_read_meta(player_data_bus_t *bus, esp_gmf_payload_t *load)
{
    if (load->valid_size == 0) {
        load->pts = 0;
        return;
    }
    if (bus->count == 0) {
        load->pts = 0;
        ESP_LOGW(TAG, "PTS underflow on read (vld=%u)", (unsigned)load->valid_size);
        return;
    }
    uint32_t need = (uint32_t)load->valid_size;
    bool first = true;
    load->pts = 0;
    while (need && bus->count) {
        data_bus_meta_item_t *m = &bus->q[bus->head];
        if (first) {
            load->pts = m->pts;
            first = false;
        }
        uint32_t take = (need < m->bytes_remain) ? need : m->bytes_remain;
        need -= take;
        m->bytes_remain -= take;
        if (m->bytes_remain == 0) {
            bus->head = (uint16_t)((bus->head + 1) % bus->cap);
            bus->count--;
        }
    }
    if (need) {
        load->pts = 0;
        ESP_LOGW(TAG, "PTS underrun, missing %u bytes", (unsigned)need);
    }
}

static esp_gmf_err_t data_bus_alloc_block(uint32_t item_size, uint32_t item_cnt, esp_gmf_db_handle_t *out_db)
{
    esp_gmf_db_handle_t db = NULL;
    if (esp_gmf_db_new_block((int)item_size, (int)item_cnt, &db) != ESP_GMF_ERR_OK || db == NULL) {
        ESP_LOGE(TAG, "db_new_block failed, item:%u cnt:%u", (unsigned)item_size, (unsigned)item_cnt);
        return ESP_GMF_ERR_MEMORY_LACK;
    }
    uint8_t align = esp_gmf_oal_get_spiram_cache_align();
    if (esp_gmf_db_set_align(db, align, align) != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "db_set_align failed");
        esp_gmf_db_deinit(db);
        return ESP_GMF_ERR_MEMORY_LACK;
    }
    *out_db = db;
    ESP_LOGI(TAG, "Video decode block %u x %u (%u bytes)",
             (unsigned)item_size, (unsigned)item_cnt,
             (unsigned)(item_size * item_cnt));
    return ESP_GMF_ERR_OK;
}

bool player_data_bus_is_handle(const void *p)
{
    const player_data_bus_t *bus = (const player_data_bus_t *)p;
    return bus && bus->magic == PLAYER_DATA_BUS_MAGIC;
}

esp_gmf_db_handle_t player_data_bus_inner(player_data_bus_t *bus)
{
    return bus ? bus->inner : NULL;
}

uint32_t player_data_bus_block_item_size(player_data_bus_t *bus)
{
    if (!player_data_bus_is_handle(bus) || !bus->lock) {
        return 0;
    }
    uint32_t size = 0;
    if (xSemaphoreTake(bus->lock, portMAX_DELAY) == pdTRUE) {
        size = bus->block_item_size;
        xSemaphoreGive(bus->lock);
    }
    return size;
}

esp_gmf_err_t player_data_bus_set_lazy_block(player_data_bus_t *bus, uint32_t item_cnt)
{
    if (!player_data_bus_is_handle(bus) || item_cnt == 0 || bus->inner != NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    bus->lazy_block_cnt = item_cnt;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t player_data_bus_ensure_block(player_data_bus_t *bus, uint32_t item_size)
{
    if (!player_data_bus_is_handle(bus) || item_size == 0 || bus->lazy_block_cnt == 0) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(bus->lock, portMAX_DELAY) != pdTRUE) {
        return ESP_GMF_ERR_FAIL;
    }
    if (bus->inner != NULL && item_size <= bus->block_item_size) {
        xSemaphoreGive(bus->lock);
        return ESP_GMF_ERR_OK;
    }
    if (bus->inner != NULL) {
        uint32_t filled = 0;
        (void)esp_gmf_db_get_filled_size(bus->inner, &filled);
        if (filled != 0) {
            ESP_LOGE(TAG, "Cannot grow decode block %u -> %u, filled:%u",
                     (unsigned)bus->block_item_size, (unsigned)item_size, (unsigned)filled);
            xSemaphoreGive(bus->lock);
            return ESP_GMF_ERR_INVALID_STATE;
        }
    }
    /* Free the old block before allocating: the bus is empty, so keeping both alive would only
     * double the peak for a whole decoded frame. */
    if (bus->inner != NULL) {
        esp_gmf_db_deinit(bus->inner);
        bus->inner = NULL;
        bus->block_item_size = 0;
    }
    esp_gmf_db_handle_t new_db = NULL;
    esp_gmf_err_t ret = data_bus_alloc_block(item_size, bus->lazy_block_cnt, &new_db);
    if (ret != ESP_GMF_ERR_OK) {
        xSemaphoreGive(bus->lock);
        return ret;
    }
    bus->inner = new_db;
    bus->block_item_size = item_size;
    xSemaphoreGive(bus->lock);
    return ESP_GMF_ERR_OK;
}

void player_data_bus_reset_meta(player_data_bus_t *bus)
{
    if (!bus || !bus->lock) {
        return;
    }
    if (xSemaphoreTake(bus->lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    bus->head = 0;
    bus->count = 0;
    xSemaphoreGive(bus->lock);
}

void player_data_bus_reset(player_data_bus_t *bus)
{
    if (!bus) {
        return;
    }
    player_data_bus_reset_meta(bus);
    if (bus->inner) {
        esp_gmf_db_reset(bus->inner);
    }
}

player_data_bus_t *player_data_bus_create(esp_gmf_db_handle_t inner_db, uint16_t meta_depth)
{
    if (meta_depth == 0) {
        return NULL;
    }
    player_data_bus_t *bus = (player_data_bus_t *)esp_gmf_oal_calloc(1, sizeof(*bus));
    if (!bus) {
        return NULL;
    }
    bus->magic = PLAYER_DATA_BUS_MAGIC;
    bus->inner = inner_db;
    bus->cap = meta_depth;
    bus->q = (data_bus_meta_item_t *)esp_gmf_oal_calloc(meta_depth, sizeof(data_bus_meta_item_t));
    if (!bus->q) {
        esp_gmf_oal_free(bus);
        return NULL;
    }
    bus->lock = xSemaphoreCreateMutex();
    if (!bus->lock) {
        esp_gmf_oal_free(bus->q);
        esp_gmf_oal_free(bus);
        return NULL;
    }
    return bus;
}

void player_data_bus_destroy(player_data_bus_t *bus)
{
    if (!bus) {
        return;
    }
    if (bus->lock) {
        vSemaphoreDelete(bus->lock);
    }
    if (bus->q) {
        esp_gmf_oal_free(bus->q);
    }
    bus->magic = 0;
    esp_gmf_oal_free(bus);
}

void player_data_bus_release(player_data_bus_t **bus)
{
    if (bus == NULL || *bus == NULL) {
        return;
    }
    esp_gmf_db_handle_t inner = player_data_bus_inner(*bus);
    if (inner != NULL) {
        esp_gmf_db_deinit(inner);
    }
    player_data_bus_destroy(*bus);
    *bus = NULL;
}

esp_gmf_err_io_t player_data_bus_acquire_write(void *ctx, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    player_data_bus_t *bus = (player_data_bus_t *)ctx;
    if (!player_data_bus_is_handle(bus) || load == NULL) {
        return ESP_GMF_IO_FAIL;
    }
    if (bus->lazy_block_cnt != 0 && wanted_size > 0) {
        if (player_data_bus_ensure_block(bus, wanted_size) != ESP_GMF_ERR_OK) {
            return ESP_GMF_IO_FAIL;
        }
    }
    if (bus->inner == NULL) {
        ESP_LOGE(TAG, "Acquire write with no inner bus");
        return ESP_GMF_IO_FAIL;
    }
    return esp_gmf_db_acquire_write(bus->inner, (esp_gmf_data_bus_block_t *)load, wanted_size, wait_ticks);
}

esp_gmf_err_io_t player_data_bus_release_write(void *ctx, esp_gmf_payload_t *load, int wait_ticks)
{
    player_data_bus_t *bus = (player_data_bus_t *)ctx;
    if (!player_data_bus_is_handle(bus) || load == NULL) {
        return ESP_GMF_IO_FAIL;
    }
    if (bus->inner == NULL) {
        return ESP_GMF_IO_FAIL;
    }

    if (xSemaphoreTake(bus->lock, portMAX_DELAY) != pdTRUE) {
        return ESP_GMF_IO_FAIL;
    }
    if (data_bus_push_meta(bus, load->pts, (uint32_t)load->valid_size) != 0) {
        xSemaphoreGive(bus->lock);
        return ESP_GMF_IO_FAIL;
    }
    xSemaphoreGive(bus->lock);

    esp_gmf_err_io_t ret = esp_gmf_db_release_write(bus->inner, (esp_gmf_data_bus_block_t *)load, wait_ticks);
    if (ret != ESP_GMF_IO_OK) {
        if (xSemaphoreTake(bus->lock, portMAX_DELAY) == pdTRUE) {
            data_bus_rollback_push(bus);
            xSemaphoreGive(bus->lock);
        }
    }
    return ret;
}

esp_gmf_err_io_t player_data_bus_acquire_read(void *ctx, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    player_data_bus_t *bus = (player_data_bus_t *)ctx;
    if (!player_data_bus_is_handle(bus) || load == NULL) {
        return ESP_GMF_IO_FAIL;
    }
    if (bus->inner == NULL) {
        ESP_LOGE(TAG, "Acquire read with no inner bus");
        return ESP_GMF_IO_FAIL;
    }
    esp_gmf_err_io_t ret = esp_gmf_db_acquire_read(bus->inner, (esp_gmf_data_bus_block_t *)load, wanted_size, wait_ticks);
    if (ret != ESP_GMF_IO_OK) {
        return ret;
    }
    if (xSemaphoreTake(bus->lock, portMAX_DELAY) != pdTRUE) {
        return ESP_GMF_IO_FAIL;
    }
    data_bus_apply_read_meta(bus, load);
    xSemaphoreGive(bus->lock);
    return ESP_GMF_IO_OK;
}

esp_gmf_err_io_t player_data_bus_release_read(void *ctx, esp_gmf_payload_t *load, int wait_ticks)
{
    player_data_bus_t *bus = (player_data_bus_t *)ctx;
    if (!player_data_bus_is_handle(bus) || load == NULL) {
        return ESP_GMF_IO_FAIL;
    }
    if (bus->inner == NULL) {
        return ESP_GMF_IO_FAIL;
    }
    return esp_gmf_db_release_read(bus->inner, (esp_gmf_data_bus_block_t *)load, wait_ticks);
}

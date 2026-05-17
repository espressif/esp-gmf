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

bool player_data_bus_is_handle(const void *p)
{
    const player_data_bus_t *bus = (const player_data_bus_t *)p;
    return bus && bus->magic == PLAYER_DATA_BUS_MAGIC;
}

esp_gmf_db_handle_t player_data_bus_inner(player_data_bus_t *bus)
{
    return bus ? bus->inner : NULL;
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
    if (inner_db == NULL || meta_depth == 0) {
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

esp_gmf_err_io_t player_data_bus_acquire_write(void *ctx, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    player_data_bus_t *bus = (player_data_bus_t *)ctx;
    if (!player_data_bus_is_handle(bus) || load == NULL) {
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
    return esp_gmf_db_release_read(bus->inner, (esp_gmf_data_bus_block_t *)load, wait_ticks);
}

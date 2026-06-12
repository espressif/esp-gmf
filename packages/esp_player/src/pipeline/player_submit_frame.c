/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_gmf_err.h"

#include "player_submit_frame.h"
#include "player_events.h"
#include "player_sync.h"

static const char *TAG = "ESP_PLAYER_SUBMIT";

struct frame_pool_s {
    frame_pool_slot_t  slots[ESP_PLAYER_FILL_POOL_SIZE];
    SemaphoreHandle_t  mux;
};

esp_player_err_t frame_pool_create(frame_pool_t **out_pool)
{
    if (out_pool == NULL) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    frame_pool_t *p = (frame_pool_t *)calloc(1, sizeof(*p));
    if (p == NULL) {
        return ESP_PLAYER_ERR_NO_MEM;
    }
    p->mux = xSemaphoreCreateMutex();
    if (p->mux == NULL) {
        free(p);
        return ESP_PLAYER_ERR_NO_MEM;
    }
    *out_pool = p;
    return ESP_PLAYER_ERR_OK;
}

void frame_pool_destroy(frame_pool_t *pool)
{
    if (pool == NULL) {
        return;
    }
    for (uint32_t i = 0; i < ESP_PLAYER_FILL_POOL_SIZE; i++) {
        free(pool->slots[i].buf);
    }
    if (pool->mux) {
        vSemaphoreDelete(pool->mux);
    }
    free(pool);
}

frame_pool_slot_t *frame_pool_acquire(frame_pool_t *pool, size_t need)
{
    if (pool == NULL || need == 0) {
        return NULL;
    }
    xSemaphoreTake(pool->mux, portMAX_DELAY);
    frame_pool_slot_t *slot = NULL;
    for (uint32_t i = 0; i < ESP_PLAYER_FILL_POOL_SIZE; i++) {
        if (!pool->slots[i].in_use) {
            slot = &pool->slots[i];
            break;
        }
    }
    if (slot == NULL) {
        xSemaphoreGive(pool->mux);
        return NULL;  // pool full — caller should drop the frame
    }
    if (slot->capacity < need) {
        // Grow buf only; never shrink — later smaller frames reuse the same allocation.
        uint8_t *new_buf = (uint8_t *)realloc(slot->buf, need);
        if (new_buf == NULL) {
            ESP_LOGE(TAG, "realloc(%u) failed", (unsigned)need);
            xSemaphoreGive(pool->mux);
            return NULL;
        }
        slot->buf = new_buf;
        slot->capacity = need;
    }
    slot->in_use = true;
    xSemaphoreGive(pool->mux);
    return slot;
}

void frame_pool_release(frame_pool_t *pool, frame_pool_slot_t *slot)
{
    if (pool == NULL || slot == NULL) {
        return;
    }
    xSemaphoreTake(pool->mux, portMAX_DELAY);
    slot->in_use = false;
    xSemaphoreGive(pool->mux);
}

frame_pool_slot_t *frame_pool_find_by_buf(frame_pool_t *pool, const uint8_t *buf)
{
    if (pool == NULL || buf == NULL) {
        return NULL;
    }
    /**
     * No lock needed: the slot whose `buf` we are looking up is currently
     * `in_use` (the caller holds a payload pointing into it). Since
     * `frame_pool_acquire` only assigns a new `buf` to NOT-in-use slots, this
     * slot's `buf` can't change out from under us.
     */
    for (uint32_t i = 0; i < ESP_PLAYER_FILL_POOL_SIZE; i++) {
        if (pool->slots[i].buf == buf) {
            return &pool->slots[i];
        }
    }
    return NULL;
}

static QueueHandle_t player_submit_queue(esp_player_stream_t *stream)
{
    if (stream->av_mask == ESP_PLAYER_MASK_AUDIO) {
        return stream->audio_side ? stream->audio_side->extractor_queue : NULL;
    }
    return stream->video_side ? stream->video_side->extractor_queue : NULL;
}

esp_player_err_t player_submit_frame_fill(esp_player_stream_t *stream,
                                          const esp_player_frame_t *frame,
                                          uint32_t timeout_ms)
{
    frame_pool_slot_t *slot = frame_pool_acquire(stream->fill_pool, frame->data_len);
    if (slot == NULL) {
        /* Pool full (64 slots in-flight) or realloc failed — caller drops. */
        return ESP_PLAYER_ERR_TIMEOUT;
    }
    memcpy(slot->buf, frame->data, frame->data_len);
    esp_gmf_payload_t load = {
        .buf = slot->buf,
        .buf_length = slot->capacity,
        .valid_size = frame->data_len,
        .is_done = frame->eos,
        .pts = frame->pts,
        .needs_free = false,  // pool-managed, NOT heap-freed in release
        .meta_flag = frame->is_bad ? ESP_GMF_META_FLAG_AUD_RECOVERY_PLC : 0,
    };
    QueueHandle_t queue = player_submit_queue(stream);
    if (queue == NULL) {
        frame_pool_release(stream->fill_pool, slot);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    if (xQueueSend(queue, &load, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        frame_pool_release(stream->fill_pool, slot);
        return ESP_PLAYER_ERR_TIMEOUT;
    }
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_submit_frame_block(esp_player_stream_t *stream, esp_player_frame_t *frame)
{
    QueueHandle_t queue = player_submit_queue(stream);
    if (queue == NULL) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_gmf_payload_t load = {
        .buf = frame->data,
        .valid_size = frame->data_len,
        .pts = frame->pts,
        .is_done = frame->eos,
        .needs_free = false,
        .meta_flag = frame->is_bad ? ESP_GMF_META_FLAG_AUD_RECOVERY_PLC : 0,
    };
    if (xQueueSend(queue, &load, ESP_GMF_MAX_DELAY) != pdTRUE) {
        return ESP_PLAYER_ERR_FAIL;
    }
    player_wait_events(stream, _CTRL_DECODER_FRAME_DONE, portMAX_DELAY);
    return ESP_PLAYER_ERR_OK;
}

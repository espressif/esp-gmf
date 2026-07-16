/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_player_advance.h"
#include "player_stream.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define ESP_PLAYER_FILL_POOL_SIZE  (64)

typedef struct frame_pool_s frame_pool_t;

typedef struct frame_pool_slot_s {
    uint8_t *buf;
    size_t   capacity;
    bool     in_use;
} frame_pool_slot_t;

esp_player_err_t frame_pool_create(frame_pool_t **out_pool);
void frame_pool_destroy(frame_pool_t *pool);
frame_pool_slot_t *frame_pool_acquire(frame_pool_t *pool, size_t need, uint32_t timeout_ms);
void frame_pool_release(frame_pool_t *pool, frame_pool_slot_t *slot);
frame_pool_slot_t *frame_pool_find_by_buf(frame_pool_t *pool, const uint8_t *buf);

esp_player_err_t player_submit_frame_fill(esp_player_stream_t *stream, const esp_player_frame_t *frame, uint32_t timeout_ms);
esp_player_err_t player_submit_frame_block(esp_player_stream_t *stream, esp_player_frame_t *frame);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

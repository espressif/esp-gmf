/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_gmf_err.h"
#include "esp_gmf_payload.h"
#include "esp_gmf_data_bus.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef struct player_data_bus player_data_bus_t;

/**
 * @brief  Create a player-side data bus with meta sidecar over an existing GMF data bus
 *
 *         `inner_db` may be NULL when the caller will attach a block later via
 *         `player_data_bus_set_lazy_block()` / `player_data_bus_ensure_block()`.
 *         Does NOT own `inner_db`; caller must deinit the underlying GMF bus separately
 *         (including a block created by `player_data_bus_ensure_block()`).
 */
player_data_bus_t *player_data_bus_create(esp_gmf_db_handle_t inner_db, uint16_t meta_depth);

void player_data_bus_destroy(player_data_bus_t *bus);
void player_data_bus_release(player_data_bus_t **bus);

/**
 * @brief  Allow `player_data_bus_ensure_block()` / first acquire_write to allocate a block bus
 *
 *         A slot must hold a whole decoded frame, whose size the decoder only reports
 *         after the pipeline is built.
 *
 * @note  Only valid while `inner` is still NULL. `item_cnt` is the block count
 *        (e.g. ESP_PLAYER_VIDEO_BLOCK_NUM). Audio ringbuf paths must not call this.
 */
esp_gmf_err_t player_data_bus_set_lazy_block(player_data_bus_t *bus, uint32_t item_cnt);

/**
 * @brief  Create or grow the inner block bus so one slot is at least `item_size` bytes
 *
 * @note  Grow only while the bus is empty: in-flight frames still reference the old
 *        slots, so a non-empty bus returns ESP_GMF_ERR_INVALID_STATE. Growing frees the
 *        old block first, so on failure the bus is left with no block.
 */
esp_gmf_err_t player_data_bus_ensure_block(player_data_bus_t *bus, uint32_t item_size);

/** @brief Return the underlying GMF data bus handle (NULL if not allocated yet). */
esp_gmf_db_handle_t player_data_bus_inner(player_data_bus_t *bus);

/** @brief Per-block size of a lazy/ensured block bus; 0 if not allocated. */
uint32_t player_data_bus_block_item_size(player_data_bus_t *bus);

/** @brief Detect whether `p` is a player_data_bus_t instance (e.g. port ctx). */
bool player_data_bus_is_handle(const void *p);

void player_data_bus_reset_meta(player_data_bus_t *bus);
void player_data_bus_reset(player_data_bus_t *bus);

/* Port ops-compatible callbacks (ctx = player_data_bus_t*). */
esp_gmf_err_io_t player_data_bus_acquire_write(void *ctx, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks);
esp_gmf_err_io_t player_data_bus_release_write(void *ctx, esp_gmf_payload_t *load, int wait_ticks);
esp_gmf_err_io_t player_data_bus_acquire_read(void *ctx, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks);
esp_gmf_err_io_t player_data_bus_release_read(void *ctx, esp_gmf_payload_t *load, int wait_ticks);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

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
 * @brief  Create a player-side data bus with meta sidecar over an existing GMF data bus.
 *
 *         Does NOT own `inner_db`; caller must deinit the underlying GMF bus separately.
 */
player_data_bus_t *player_data_bus_create(esp_gmf_db_handle_t inner_db, uint16_t meta_depth);

void player_data_bus_destroy(player_data_bus_t *bus);

/** @brief Return the underlying GMF data bus handle. */
esp_gmf_db_handle_t player_data_bus_inner(player_data_bus_t *bus);

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

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "player_stream.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

esp_player_err_t _player_update_url(esp_player_stream_t *stream, const char *new_url);
bool _player_is_network_source_uri(esp_player_stream_t *stream);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

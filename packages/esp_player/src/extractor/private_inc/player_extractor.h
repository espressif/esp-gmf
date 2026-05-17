/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_gmf_element.h"
#include "esp_extractor.h"
#include "esp_player_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define DEFAULT_PLAYER_EXTRACTOR_CONFIG()  {  \
    .in_read_cb    = NULL,                    \
    .in_seek_cb    = NULL,                    \
    .in_size_cb    = NULL,                    \
    .out_pool_size = 30 * 1024,               \
    .out_align     = 16,                      \
}

esp_gmf_err_t player_extractor_init(esp_extractor_config_t *config, esp_gmf_element_handle_t *handle);
esp_gmf_err_t player_extractor_seek(esp_gmf_element_handle_t handle, uint64_t time_pos);
esp_gmf_err_t player_extractor_release_frame(esp_gmf_element_handle_t handle, esp_extractor_frame_info_t *frame_info);
esp_gmf_err_t player_extractor_get_stream_num(esp_gmf_element_handle_t handle, esp_extractor_stream_type_t type, uint16_t *stream_num);
esp_gmf_err_t player_extractor_enable_stream(esp_gmf_element_handle_t handle, esp_extractor_stream_type_t type, uint16_t stream_idx, bool enable);
esp_gmf_err_t player_extractor_get_stream_info(esp_gmf_element_handle_t handle, esp_extractor_stream_type_t type, uint16_t stream_idx, esp_extractor_stream_info_t *info);
esp_gmf_err_t player_extractor_get_track_info(esp_gmf_element_handle_t handle, esp_player_track_type_t type, uint16_t track_idx, esp_player_track_info_t *info);
esp_gmf_err_t player_extractor_trans_stream(esp_gmf_element_handle_t handle, esp_extractor_stream_type_t type, uint8_t selected_idx);
esp_gmf_err_t player_extractor_track_active(esp_gmf_element_handle_t handle, esp_extractor_stream_type_t type, int8_t *stream_idx);
esp_gmf_err_t player_extractor_get_last_pts(esp_gmf_element_handle_t handle, uint64_t *pts_ms);
esp_gmf_err_t player_extractor_get_delta_pts(esp_gmf_element_handle_t handle, uint64_t *delta_ms);
esp_gmf_err_t player_extractor_set_raw_pcm_info(esp_gmf_element_handle_t handle, uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample);
bool player_extractor_is_raw_source(esp_gmf_element_handle_t handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_gmf_data_bus.h"
#include "esp_gmf_element.h"
#include "esp_gmf_err.h"
#include "esp_gmf_event.h"
#include "esp_gmf_task.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef struct esp_player_stream esp_player_stream_t;

typedef struct {
    void (*stop_path)(esp_player_stream_t *stream);
    void (*init_params_format)(esp_player_stream_t *stream);
    void (*seek_handles)(esp_player_stream_t *stream, esp_gmf_task_handle_t *dec_tsk, QueueHandle_t *q);
    void (*seek_pause_decoder)(esp_player_stream_t *stream, esp_gmf_db_handle_t db,
                               esp_gmf_task_handle_t dec_tsk, QueueHandle_t q,
                               esp_gmf_event_state_t *state, esp_gmf_err_t *ret);
    void (*seek_stop_decoder_if_running)(esp_player_stream_t *stream, QueueHandle_t q,
                                         esp_gmf_db_handle_t db, esp_gmf_err_t *ret);
    bool (*finished_try_seek)(esp_player_stream_t *stream);
    void (*start_decoder_mask)(esp_player_stream_t *stream);
    void (*seek_flush_render)(esp_player_stream_t *stream, esp_gmf_element_handle_t *render_el);
    void (*seek_resume_or_create_decoder)(esp_player_stream_t *stream);
    void (*seek_create_render_if_expected)(esp_player_stream_t *stream);
    uint32_t (*seek_done_bit)(esp_player_stream_t *stream);
    bool (*seek_wait_decoder_done)(esp_player_stream_t *stream, uint32_t seek_done_bits);
    bool (*decoder_expected_running)(esp_player_stream_t *stream);
    void (*seek_paused_disable_flush)(esp_player_stream_t *stream, esp_gmf_element_handle_t render_el);
    void (*seek_playing_disable_flush)(esp_player_stream_t *stream, esp_gmf_element_handle_t render_el);
} player_st_path_ops_t;

const player_st_path_ops_t *player_st_get_audio_ops(void);
const player_st_path_ops_t *player_st_get_video_ops(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

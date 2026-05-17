/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_gmf_err.h"
#include "esp_gmf_payload.h"

#include "player_stream.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define PLAYER_PORTS_EMPTY_LOAD(load)  do {  \
    load->buf        = NULL;                 \
    load->valid_size = 0;                    \
    load->is_done    = true;                 \
} while (0)

/* -------- GMF port callbacks (player_ports.c, audio/video player_ports_*.c) -------- */

esp_gmf_err_io_t extractor_audio_out_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks);
esp_gmf_err_io_t extractor_video_out_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks);
esp_gmf_err_io_t decoder_audio_in_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks);
esp_gmf_err_io_t decoder_audio_in_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks);
esp_gmf_err_io_t decoder_video_in_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks);
esp_gmf_err_io_t decoder_video_in_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks);

/* -------- Component-internal: shared port helpers (player_ports.c) -------- */

esp_gmf_err_io_t player_ports_push_bounded(esp_player_stream_t *stream, QueueHandle_t q, esp_gmf_payload_t *load, bool is_audio);
esp_gmf_err_io_t player_ports_handle_stop_state(esp_player_stream_t *stream, esp_gmf_payload_t *load, const char *queue_name);
void player_ports_buffer_gate_try_enter(esp_player_stream_t *stream, bool is_audio_path);
bool player_ports_buffer_gate_try_leave(esp_player_stream_t *stream);
void player_ports_buffer_note_extractor_frame(esp_player_stream_t *stream, bool is_audio);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

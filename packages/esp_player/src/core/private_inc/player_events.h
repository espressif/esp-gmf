/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_gmf_event.h"

#include "esp_player_types.h"
#include "player_state.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define _CTRL_DECODER_FRAME_DONE  (1 << 0)  // Decoder frame done

#define _CTRL_PLAYER_RUN                      (1 << 1)   // Player running
#define _CTRL_PLAYER_PAUSED                   (1 << 2)   // Player paused
#define _CTRL_PLAYER_RESUMED                  (1 << 3)   // Player resumed
#define _CTRL_PLAYER_STOPPED                  (1 << 4)   // Player stopped
#define _CTRL_PLAYER_SEEKING                  (1 << 5)   // Player seeking
#define _CTRL_RUN_TO_END                      (1 << 6)   // Run to end
#define _CTRL_PLAYER_QUIT                     (1 << 7)   // Player quit
#define _CTRL_PLAYER_SYNC_AUDIO_RESUMED       (1 << 8)   // Audio sync resumed
#define _CTRL_PLAYER_SYNC_VIDEO_RESUMED       (1 << 9)   // Video sync resumed
#define _CTRL_PLAYER_DECODER_AUDIO_SEEK_DONE  (1 << 10)  // Decoder audio seek done
#define _CTRL_PLAYER_DECODER_VIDEO_SEEK_DONE  (1 << 11)  // Decoder video seek done

#define _CTRL_ALL_EVENTS  (_CTRL_DECODER_FRAME_DONE | _CTRL_PLAYER_RUN | _CTRL_PLAYER_PAUSED | _CTRL_PLAYER_RESUMED | _CTRL_PLAYER_STOPPED | _CTRL_PLAYER_SEEKING | _CTRL_RUN_TO_END | _CTRL_PLAYER_QUIT | _CTRL_PLAYER_SYNC_AUDIO_RESUMED | _CTRL_PLAYER_SYNC_VIDEO_RESUMED)

#define RENDER_STOP_TIMEOUT_MS     1000  // Render stop timeout
#define DECODER_STOP_TIMEOUT_MS    1000  // Decoder stop timeout
#define EXTRACTOR_STOP_TIMEOUT_MS  1000  // Extractor stop timeout
#define ALL_TASKS_STOP_TIMEOUT_MS  2000  // All tasks stop timeout
#define SEEK_DONE_TIMEOUT_MS       5000  // Decoder seek done timeout, generous enough for a network source

#define ESP_PLAYER_TASK_IS_RUNNING(status, task_bit)     ((status & task_bit) != 0)
#define ESP_PLAYER_SET_TASK_RUNNING(status, task_bit)    (status |= task_bit)
#define ESP_PLAYER_CLEAR_TASK_RUNNING(status, task_bit)  (status &= ~task_bit)

typedef esp_gmf_err_t (*player_pipe_event_handler_t)(esp_gmf_event_pkt_t *event, void *ctx);

void player_set_events(esp_player_stream_t *stream, uint32_t event_bits);
void player_clear_events(esp_player_stream_t *stream, uint32_t event_bits);
esp_player_err_t player_wait_events(esp_player_stream_t *stream, uint32_t event_bits, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

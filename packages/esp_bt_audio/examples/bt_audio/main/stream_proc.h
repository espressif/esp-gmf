/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "esp_err.h"
#include "esp_gmf_pool.h"
#include "esp_bt_audio_stream.h"

/**
 * @brief  Structure for stream user data
 */
typedef struct {
    esp_gmf_pipeline_handle_t  pipe;  /*!< Pipeline handle */
} stream_user_data_t;

/**
 * @brief  Initialize the stream processor
 *
 * @param[in]  pool  Pool handle
 *
 * @return
 *       - void
 */
void stream_proc_init(esp_gmf_pool_handle_t pool);

/**
 * @brief  State change callback for the stream processor
 *
 * @param[in]  stream  Stream handle
 * @param[in]  state   Stream state
 *
 * @return
 *       - void
 */
void stream_proc_state_chg(esp_bt_audio_stream_handle_t stream, esp_bt_audio_stream_state_t state);

/**
 * @brief  Play the next track
 *
 * @return
 *       - void
 */
void local2bt_play_next(void);

/**
 * @brief  Play the previous track
 *
 * @return
 *       - void
 */
void local2bt_play_prev(void);

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "sdkconfig.h"

#include "esp_gmf_task.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Built-in GMF task thread defaults (override per handle via esp_player_set_task_config())
 */
#define ESP_PLAYER_DEFAULT_EXTRACTOR_TASK()  {  \
    .stack        = 5120,                       \
    .prio         = 5,                          \
    .core         = 0,                          \
    .stack_in_ext = 1,                          \
}

#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
#define ESP_PLAYER_DEFAULT_AUDIO_DECODER_TASK()  {  \
    .stack        = 5120,                           \
    .prio         = 5,                              \
    .core         = 1,                              \
    .stack_in_ext = 1,                              \
}

#define ESP_PLAYER_DEFAULT_AUDIO_RENDER_TASK()  {  \
    .stack        = 5120,                          \
    .prio         = 5,                             \
    .core         = 0,                             \
    .stack_in_ext = 1,                             \
}
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */

#if CONFIG_ESP_PLAYER_ENABLE_VIDEO
#define ESP_PLAYER_DEFAULT_VIDEO_DECODER_TASK()  {  \
    .stack        = 5120,                           \
    .prio         = 5,                              \
    .core         = 0,                              \
    .stack_in_ext = 1,                              \
}

#define ESP_PLAYER_DEFAULT_VIDEO_RENDER_TASK()  {  \
    .stack        = 5120,                          \
    .prio         = 5,                             \
    .core         = 1,                             \
    .stack_in_ext = 1,                             \
}
#endif  /* CONFIG_ESP_PLAYER_ENABLE_VIDEO */

/**
 * @brief  Built-in buffer defaults (override per handle via esp_player_set_buffer_config())
 */
#define DEFAULT_EXTRACTOR_AUDIO_POOL_SIZE       (32 * 1024)
#define DEFAULT_EXTRACTOR_VIDEO_POOL_SIZE       (100 * 1024)
#define ESP_PLAYER_DEFAULT_HTTP_READ_BUF_SIZE   (81920U)
/* Network buffering gate for http/https/hls; 0 = disabled, non-zero = enabled */
#define ESP_PLAYER_DEFAULT_NETWORK_BUFFERING    (1)
#define ESP_PLAYER_DEFAULT_PREBUFFER_RESUME_MS  (400U)
#define ESP_PLAYER_DEFAULT_REBUFFER_ENTER_MS    (200U)
#define ESP_PLAYER_DEFAULT_REBUFFER_RESUME_MS   (300U)
#define ESP_PLAYER_DEFAULT_REBUFFER_GRACE_MS    (50U)

#ifdef __cplusplus
}
#endif  /* __cplusplus */

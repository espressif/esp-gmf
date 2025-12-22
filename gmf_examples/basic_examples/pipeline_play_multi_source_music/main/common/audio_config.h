/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_bit_defs.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define HTTP_AUDIO_URL     "https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.mp3"
#define SDCARD_AUDIO_PATH  "/sdcard/test.mp3"

#define PIPELINE_BLOCK_BIT        BIT(0)
#define FLASH_PIPELINE_BLOCK_BIT  BIT(1)

#define PIPELINE_STOP_TIMEOUT_MS  5000
#define PIPELINE_TASK_TIMEOUT_MS  30000

#define PLAYBACK_DEFAULT_VOLUME  80

#ifdef __cplusplus
}
#endif  /* __cplusplus */

/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <sdkconfig.h>
#include "esp_capture_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#if CONFIG_IDF_TARGET_ESP32P4
#define VIDEO_SINK0_WIDTH  1280
#define VIDEO_SINK0_HEIGHT 720
#define VIDEO_SINK0_FPS    20
#else
#define VIDEO_SINK0_WIDTH  640
#define VIDEO_SINK0_HEIGHT 480
#define VIDEO_SINK0_FPS    4
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */

#define AUDIO_SINK0_FMT         ESP_CAPTURE_FMT_ID_AAC
#define AUDIO_SINK0_SAMPLE_RATE 16000
#define AUDIO_SINK0_CHANNEL     2

#define AUDIO_SINK1_FMT         ESP_CAPTURE_FMT_ID_G711A
#define AUDIO_SINK1_SAMPLE_RATE 8000
#define AUDIO_SINK1_CHANNEL     1

#define VIDEO_SINK0_FMT    ESP_CAPTURE_FMT_ID_H264
#define VIDEO_SINK1_FMT    ESP_CAPTURE_FMT_ID_MJPEG
#define VIDEO_SINK1_WIDTH  VIDEO_SINK0_WIDTH
#define VIDEO_SINK1_HEIGHT VIDEO_SINK0_HEIGHT
#define VIDEO_SINK1_FPS    (VIDEO_SINK0_FPS / 2)

#ifdef __cplusplus
}
#endif  /* __cplusplus */

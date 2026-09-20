/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Edit CAPTURE_SINKS_SETTINGS to change sink formats / resolutions.
 * Sink 0: streaming (H264 + AAC)
 * Sink 1: snapshot (JPEG, no audio)
 * Sink 2: display  (RGB565, no audio)
 */

#pragma once

#include "esp_capture_sink.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAPTURE_SINKS_SETTINGS {                                                              \
    /* 0: streaming */                                                                        \
    {                                                                                         \
        .video_info = {                                                                       \
            .format_id = ESP_CAPTURE_FMT_ID_H264,                                             \
            .width = 1920,                                                                    \
            .height = 1080,                                                                   \
            .fps = 25,                                                                        \
        },                                                                                    \
        .audio_info = {                                                                       \
            .format_id = ESP_CAPTURE_FMT_ID_AAC,                                              \
            .sample_rate = 16000,                                                             \
            .channel = 1,                                                                     \
            .bits_per_sample = 16,                                                            \
        },                                                                                    \
    },                                                                                        \
    /* 1: JPEG snapshot (no audio) */                                                         \
    {                                                                                         \
        .video_info = {                                                                       \
            .format_id = ESP_CAPTURE_FMT_ID_MJPEG,                                            \
            .width = 2560,                                                                    \
            .height = 1440,                                                                   \
            .fps = 1,                                                                         \
        },                                                                                    \
    },                                                                                        \
    /* 2: small display (no audio) */                                                         \
    {                                                                                         \
        .video_info = {                                                                       \
            .format_id = ESP_CAPTURE_FMT_ID_RGB565,                                           \
            .width = 320,                                                                     \
            .height = 240,                                                                    \
            .fps = 20,                                                                        \
        },                                                                                    \
    },                                                                                        \
}

#define SINK_STREAM  0
#define SINK_SNAP    1
#define SINK_DISP    2

#ifdef __cplusplus
}
#endif

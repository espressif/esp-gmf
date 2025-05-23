/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define TEST_WITH_VIDEO

#if CONFIG_IDF_TARGET_ESP32P4
#define VIDEO_WIDTH      1024
#define VIDEO_HEIGHT     600
#define VIDEO_FPS        20
#define VIDEO_SINK_FMT_0 ESP_CAPTURE_FMT_ID_H264
#define VIDEO_SINK_FMT_1 ESP_CAPTURE_FMT_ID_MJPEG
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */

#if CONFIG_IDF_TARGET_ESP32S3
#define VIDEO_WIDTH      320
#define VIDEO_HEIGHT     240
#define VIDEO_FPS        10
#define VIDEO_SINK_FMT_0 ESP_CAPTURE_FMT_ID_H264
#define VIDEO_SINK_FMT_1 ESP_CAPTURE_FMT_ID_MJPEG
#endif  /* CONFIG_IDF_TARGET_ESP32S3 */

#if CONFIG_IDF_TARGET_ESP32
#undef TEST_WITH_VIDEO
#define VIDEO_WIDTH      0
#define VIDEO_HEIGHT     0
#define VIDEO_FPS        0
#define VIDEO_SINK_FMT_0 ESP_CAPTURE_FMT_ID_NONE
#define VIDEO_SINK_FMT_1 ESP_CAPTURE_FMT_ID_NONE
#endif  /* CONFIG_IDF_TARGET_ESP32 */

#define TEST_USE_UNITY

#ifdef __cplusplus
}
#endif  /* __cplusplus */

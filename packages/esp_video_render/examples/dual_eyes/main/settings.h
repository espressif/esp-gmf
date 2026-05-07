/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

// #define DUAL_EYES_ON_DUAL_DISPLAY
#define VIDEO_WIDTH     240
#define VIDEO_HEIGHT    240
#define MAX_FRAME_SIZE  (16 * 1024)
#define PLAY_TIME       (10000)
#define LEFT_FILE       "/sdcard/left.mjpeg"
#define RIGHT_FILE      "/sdcard/right.mjpeg"

#define VIDEO_RENDER_LVGL_SUPPORT

#ifdef __cplusplus
}
#endif  /* __cplusplus */

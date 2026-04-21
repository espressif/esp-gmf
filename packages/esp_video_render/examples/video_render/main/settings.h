/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define VIDEO_WIDTH     240
#define VIDEO_HEIGHT    240
#define MAX_FRAME_SIZE  (16 * 1024)
#define PLAY_TIME       (10000)

/**
 * @brief  Test files for dual eyes
 *
 * @note  File can be found in the assets folder
 */
#define LEFT_FILE   "/sdcard/left.mjpeg"
#define RIGHT_FILE  "/sdcard/right.mjpeg"

#ifdef __cplusplus
}
#endif  /* __cplusplus */

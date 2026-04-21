/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Maximum number of playlists
 */
#define PLAYER_MAX_PLAYLIST  10

/**
 * @brief  Maximum length of a path
 */
#define PLAYER_MAX_PATH_LEN  100

/**
 * @brief  Extractor output pool size
 */
#define PLAYER_EXTRACT_POOL_SIZE  (256 * 1024)

/**
 * @brief  Default FPS
 */
#define PLAYER_DEFAULT_FPS  20

/**
 * @brief  Source URL to find media files for playlist
 */
#define PLAYER_SOURCE_URL  "/sdcard/render"

/**
 * @brief  Decoder output cache size
 *         Special for H264 decoder so that decode and color convert can be done in parallel
 */
#define PLAYER_DEC_OUT_CACHE_SIZE  (1024 * 1024)

#ifdef __cplusplus
}
#endif  /* __cplusplus */

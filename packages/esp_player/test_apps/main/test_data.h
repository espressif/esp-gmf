/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/** @brief SD-card media directories (pytest / on-device test assets) */
#define TEST_FILE_AUDIO_PATH   "/sdcard/player_testset/audio"
#define TEST_FILE_VIDEO_PATH   "/sdcard/player_testset/video"
#define TEST_FILE_AV_PATH      "/sdcard/player_testset/av"
#define TEST_FILE_ERROR_PATH   "/sdcard/player_testset/error"
#define TEST_FILE_PERROR_PATH  "/sdcard/player_testset/parterror"

/** @brief Fixed media file for interface smoke tests */
#define TEST_FILE_PATH  "/sdcard/player_testset/av/test_h264.mp4"

/** @brief Network streams (require Wi-Fi and reachable servers) */
#define TEST_HTTP_URL   "http://192.168.8.31:8008/0_44100_2_265650_214.m4a"
#define TEST_HTTPS_URL  "https://dl.espressif.com/dl/audio/ff-16b-2c-16000hz.mp3"
#define TEST_HLS_URL    "http://open.ls.qingting.fm/live/274/64k.m3u8?format=aac"

/** @brief HTTP URLs for error-path tests */
#define TEST_HTTP_URL_NOT_FOUND      "http://192.168.8.31:8008/esp_player_test_not_found.m4a"
#define TEST_HTTP_URL_INVALID_MEDIA  "https://www.espressif.com/"

/** @brief Max path buffer for test file discovery helpers */
#define TEST_PATH_MAX_LEN  512

/** @brief Frame-mode virtual URLs (AAC test vectors — params from ADTS bitstream) */
#define TEST_FILL_URL_AAC   "fill:///test.aac"
#define TEST_BLOCK_URL_AAC  "block:///test.aac"

extern const uint8_t test_data_aac_frame1[];
extern const uint8_t test_data_aac_frame2[];

extern const size_t TEST_DATA_AAC_FRAME1_COUNT;
extern const size_t TEST_DATA_AAC_FRAME2_COUNT;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

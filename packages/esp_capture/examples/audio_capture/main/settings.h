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
#define TEST_BOARD_NAME "ESP32_P4_DEV_V14"
#elif CONFIG_IDF_TARGET_ESP32S3
#define TEST_BOARD_NAME "S3_Korvo_V2"
#elif CONFIG_IDF_TARGET_ESP32
#define TEST_BOARD_NAME "LYRAT_MINI_V1"
#else
#define TEST_BOARD_NAME "DUMMY"
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */

#define AUDIO_CAPTURE_FORMAT      ESP_CAPTURE_FMT_ID_AAC
#define AUDIO_CAPTURE_SAMPLE_RATE 16000
#define AUDIO_CAPTURE_CHANNEL     2

#ifdef __cplusplus
}
#endif  /* __cplusplus */

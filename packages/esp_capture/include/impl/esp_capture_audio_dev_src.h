/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_capture_types.h"
#include "esp_capture_audio_src_if.h"
#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Audio device source implementation for ESP Capture
 *
 * @note  This file provides the audio source interface for capturing audio data from
 *        audio devices commonly attached to ESP32 via I2S interface using
 *        [esp_codec_dev](https://github.com/espressif/esp-adf/tree/master/components/esp_codec_dev)
 *
 *        Key Features:
 *          - Provides audio source interface implementation for audio devices
 *          - Supports PCM audio format capture
 *          - Handles audio format negotiation and capability setting
 *          - Manages audio frame reading with proper PTS (Presentation Time Stamp) calculation
 *          - Integrates with esp_codec_dev for hardware abstraction
 */

/**
 * @brief  Audio device source configuration
 */
typedef struct {
    esp_codec_dev_handle_t  record_handle;  /*!< Record handle of `esp_codec_dev` */
} esp_capture_audio_dev_src_cfg_t;

/**
 * @brief  Create audio source instance for audio devices
 *
 * @param[in]  cfg  Audio device source configuration
 *
 * @return
 *       - NULL    Not enough memory to hold audio device source instance
 *       - Others  Audio device source instance
 */
esp_capture_audio_src_if_t *esp_capture_new_audio_dev_src(esp_capture_audio_dev_src_cfg_t *cfg);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

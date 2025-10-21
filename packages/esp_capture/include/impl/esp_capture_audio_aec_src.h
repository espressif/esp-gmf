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
 * @brief  Audio source with Acoustic Echo Cancellation (AEC) implementation for ESP Capture
 *
 * @note  This file implements the audio source interface for capturing audio data with real-time
 *        acoustic echo cancellation. It acquires input data from codec device use
 *        [esp_codec_dev](https://github.com/espressif/esp-adf/tree/master/components/esp_codec_dev)
 *        Currently only supported on ESP32-S3 and ESP32-P4 platforms
 *
 *        Key Features:
 *          - Provides audio source interface implementation with integrated AEC processing
 *          - Supports real-time acoustic echo cancellation for improved audio quality
 *          - Handles audio format negotiation with fixed PCM output (1 channel, 16-bit)
 *          - Manages dual-thread architecture for continuous audio processing
 *          - Implements efficient buffer management and frame caching
 */

/**
 * @brief  Audio with AEC source configuration
 */
typedef struct {
    const char *mic_layout;     /*!< Mic data layout, e.g. "MR", "RMNM"" */
    void       *record_handle;  /*!< Record handle of `esp_codec_dev` */
    uint8_t     channel;        /*!< Audio channel */
    uint8_t     channel_mask;   /*!< Bit mask to select which channels to process
                                      (e.g. 0x1 for left channel, 0x2 for right channel) */
    bool        data_on_vad;    /*!< If enabled, only fetch and send audio data when voice activity is detected (between VAD start and end)
                                     This optimizes resource usage by avoiding continuous encoding/transmission of silent audio
                                     When enabled:
                                       - CPU is saved by skipping full processing during non-voice periods
                                       - Data is only sent when VAD confirms voice presence
                                     Recommended for chat applications to reduce bandwidth and computational overhead */
} esp_capture_audio_aec_src_cfg_t;

/**
 * @brief  Create audio source with AEC for codec
 *
 * @param[in]  cfg  Audio source with AEC configuration
 *
 * @return
 *       - NULL    Not enough memory to new an audio AEC source instance
 *       - Others  Audio AEC source instance
 */
esp_capture_audio_src_if_t *esp_capture_new_audio_aec_src(esp_capture_audio_aec_src_cfg_t *cfg);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

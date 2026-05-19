/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_asrc_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Audio statistics accumulator structure
 */
typedef struct {
    double  sum_squares;    /*!< Sum of squared samples (for RMS calculation) */
    double  sum_dc;         /*!< Sum of samples (for DC offset calculation) */
    double  max_abs;        /*!< Maximum absolute sample value */
    int     total_samples;  /*!< Total number of samples accumulated */
} audio_stats_accumulator_t;

/**
 * @brief  Initialize audio statistics accumulator
 *
 * @param[out]  acc  Pointer to accumulator structure
 */
void init_audio_stats_accumulator(audio_stats_accumulator_t *acc);

/**
 * @brief  Accumulate audio statistics from data buffer
 *
 * @param[in,out]  acc        Pointer to accumulator structure
 * @param[in]      data       Audio data buffer
 * @param[in]      data_size  Data buffer size in bytes
 * @param[in]      info       Pointer to audio information
 */
void accumulate_audio_stats(audio_stats_accumulator_t *acc, uint8_t *data, int data_size, esp_asrc_aud_info_t *info);

/**
 * @brief  Analyze audio quality by comparing original and processed statistics
 *
 * @param[in]  orig_acc  Pointer to original audio statistics
 * @param[in]  proc_acc  Pointer to processed audio statistics
 *
 * @return
 *       - true   Quality test passed
 *       - false  Quality test failed
 */
bool analyze_audio_quality(audio_stats_accumulator_t *orig_acc, audio_stats_accumulator_t *proc_acc);

/**
 * @brief  Generate a multi-channel sine-wave PCM signal in-place
 *
 * @param[out]  buffer           Pointer to destination PCM buffer
 * @param[in]   duration_ms      Signal duration in milliseconds
 * @param[in]   sample_rate      Sample rate in Hz
 * @param[in]   amplitude_db     Sine amplitude in dBFS
 * @param[in]   bits_per_sample  PCM bit depth (for example 16/24/32)
 * @param[in]   channels         Channel count
 * @param[in]   frequency        Sine frequency in Hz
 *
 * @return
 *       - Number  of samples generated
 */
int asrc_generate_sine_signal(void *buffer, int duration_ms, int sample_rate,
                              float amplitude_db, int bits_per_sample, int channels, float frequency);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

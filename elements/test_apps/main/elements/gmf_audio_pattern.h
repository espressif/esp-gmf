/**
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include "gmf_audio_el_com.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SWEEP_DURATION_MS (5000)

void audio_generate_sweep_signal(sweep_data_t *sweep_data, esp_gmf_info_sound_t *sound_info, uint32_t current_sample,
                                 uint32_t total_samples, uint32_t samples_per_chunk, bool *is_done)
{
    const float start_freq = 20.0f;
    const float end_freq = 20000.0f;
    const float duration = SWEEP_DURATION_MS / 1000.0f;
    uint32_t sample_count = 0;
    for (uint32_t i = 0; i < samples_per_chunk; i++) {
        if (current_sample >= total_samples) {
            *is_done = true;
            break;
        }
        float t = (float)current_sample / sound_info->sample_rates;
        float freq = start_freq + (end_freq - start_freq) * t / duration;
        float phase = 2.0f * M_PI * freq * t;
        float sample_value = sinf(phase);
        for (int ch = 0; ch < sound_info->channels; ch++) {
            switch (sound_info->bits) {
                case 16: {
                    int16_t sample = (int16_t)(32767.0f * sample_value);
                    ((int16_t *)sweep_data->data)[i * sound_info->channels + ch] = sample;
                    break;
                }
                case 24: {
                    int32_t sample = (int32_t)(8388607.0f * sample_value);
                    uint8_t *ptr = (uint8_t *)sweep_data->data + (i * sound_info->channels + ch) * 3;
                    ptr[0] = sample & 0xFF;
                    ptr[1] = (sample >> 8) & 0xFF;
                    ptr[2] = (sample >> 16) & 0xFF;
                    break;
                }
                case 32: {
                    int32_t sample = (int32_t)(2147483647.0f * sample_value);
                    ((int32_t *)sweep_data->data)[i * sound_info->channels + ch] = sample;
                    break;
                }
                default:
                    ESP_LOGE("AUD_PATTERN", "Unsupported bit depth: %d", sound_info->bits);
                    *is_done = true;
                    return;
            }
        }
        current_sample++;
        sample_count++;
    }
    sweep_data->size = sample_count * sound_info->channels * (sound_info->bits / 8);
    sweep_data->is_released = false;
    sweep_data->is_done = *is_done;
}

#ifdef __cplusplus
}
#endif

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <math.h>
#include "esp_log.h"
#include "esp_asrc_types.h"
#include "asrc_quality_test.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif  /* M_PI */
#define TAG                       "ASRC_QUALITY_TEST"
#define QUALITY_EPSILON           1e-9f
#define MAX_RMS_DIFF_DIFF         0.01f
#define MAX_PEAK_DIFF_DIFF        0.07f
#define MAX_DC_OFFSET_CHANGE      0.3f
#define AE_TEST_MAX_S32           ((int32_t)0x7fffffffL)
#define AE_TEST_MIN_S32           ((int32_t)0x80000000L)
#define AE_TEST_MAX_S24           ((int32_t)0x7fffff)
#define AE_TEST_MIN_S24           ((int32_t)0xff800000)
#define AE_TEST_MAX_S16           ((int16_t)0x7fff)
#define AE_TEST_MIN_S16           ((int16_t)0x8000)
#define AE_TEST_MAX_U8            ((uint8_t)127)
#define AE_TEST_GET_MAX_VAL(bits) ((bits) == 8 ? AE_TEST_MAX_U8 : ((bits) == 16 ? AE_TEST_MAX_S16 : ((bits) == 24 ? AE_TEST_MAX_S24 : AE_TEST_MAX_S32)))

static float normalize_value(uint8_t *data, int offset, int bits_per_sample)
{
    switch (bits_per_sample) {
        case 8: {
            uint8_t sample = data[offset];
            return (float)(sample - 128) / 128.0f;
        }
        case 16: {
            int16_t sample = *(int16_t *)&data[offset];
            return (float)sample / 32768.0f;
        }
        case 24: {
            int32_t sample = data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16);
            if (sample & 0x800000) {
                sample |= 0xFF000000;
            }
            return (float)sample / 8388608.0f;
        }
        case 32: {
            int32_t sample = *(int32_t *)&data[offset];
            return (float)sample / 2147483648.0f;
        }
        default:
            return 0.0f;
    }
}

static void calculate_final_audio_stats(audio_stats_accumulator_t *acc, float *rms, float *peak, float *dc_offset)
{
    if (acc->total_samples > 0) {
        *rms = sqrt(acc->sum_squares / acc->total_samples);
        *peak = acc->max_abs;
        *dc_offset = acc->sum_dc / acc->total_samples;
    } else {
        *rms = 0.0f;
        *peak = 0.0f;
        *dc_offset = 0.0f;
    }
}

void init_audio_stats_accumulator(audio_stats_accumulator_t *acc)
{
    acc->sum_squares = 0.0f;
    acc->sum_dc = 0.0f;
    acc->max_abs = 0.0f;
    acc->total_samples = 0;
}

void accumulate_audio_stats(audio_stats_accumulator_t *acc, uint8_t *data, int data_size, esp_asrc_aud_info_t *info)
{
    int bytes_per_sample = info->bits_per_sample / 8 * info->channel;
    int total_samples = data_size / bytes_per_sample;

    for (int i = 0; i < total_samples; i++) {
        int byte_offset = i * bytes_per_sample;
        float sample_value = normalize_value(data, byte_offset, info->bits_per_sample);

        acc->sum_squares += sample_value * sample_value;
        acc->sum_dc += sample_value;

        float abs_value = fabs(sample_value);
        if (abs_value > acc->max_abs) {
            acc->max_abs = abs_value;
        }
    }

    acc->total_samples += total_samples;
}

bool analyze_audio_quality(audio_stats_accumulator_t *orig_acc, audio_stats_accumulator_t *proc_acc)
{
    float orig_rms, orig_peak, orig_dc;
    float proc_rms, proc_peak, proc_dc;

    calculate_final_audio_stats(orig_acc, &orig_rms, &orig_peak, &orig_dc);
    calculate_final_audio_stats(proc_acc, &proc_rms, &proc_peak, &proc_dc);

    float rms_diff_percent = (orig_rms > QUALITY_EPSILON) ? (fabs(proc_rms - orig_rms) / orig_rms)
                                                          : (proc_rms > QUALITY_EPSILON ? 1.0f : 0.0f);
    float peak_diff_percent = (orig_peak > QUALITY_EPSILON) ? (fabs(proc_peak - orig_peak) / orig_peak)
                                                            : (proc_peak > QUALITY_EPSILON ? 1.0f : 0.0f);
    float dc_offset_change = fabs(proc_dc - orig_dc);
    ESP_LOGD(TAG, "orig_rms: %f, orig_peak: %f, orig_dc: %f", orig_rms, orig_peak, orig_dc);
    ESP_LOGD(TAG, "proc_rms: %f, proc_peak: %f, proc_dc: %f", proc_rms, proc_peak, proc_dc);
    ESP_LOGD(TAG, "rms_diff_percent: %f, peak_diff_percent: %f, dc_offset_change: %f", rms_diff_percent, peak_diff_percent, dc_offset_change);

    bool rms_pass = rms_diff_percent <= MAX_RMS_DIFF_DIFF;
    bool peak_pass = peak_diff_percent <= MAX_PEAK_DIFF_DIFF;
    bool dc_offset_pass = dc_offset_change <= MAX_DC_OFFSET_CHANGE;
    ESP_LOGD(TAG, "rms_pass: %d, peak_pass: %d, dc_offset_pass: %d", rms_pass, peak_pass, dc_offset_pass);
    return rms_pass && peak_pass && dc_offset_pass;
}

int asrc_generate_sine_signal(void *buffer, int duration_ms, int sample_rate,
                              float amplitude_db, int bits_per_sample, int channels, float frequency)
{
    int frame_num = (duration_ms * sample_rate) / 1000;
    int total_samples = frame_num * channels;
    int64_t max_amplitude = AE_TEST_GET_MAX_VAL(bits_per_sample);
    double amplitude_linear = pow(10.0, amplitude_db / 20.0);
    int64_t amplitude = (int64_t)(amplitude_linear * max_amplitude);
    if (amplitude > max_amplitude) {
        amplitude = max_amplitude;
    }
    for (int frame = 0; frame < frame_num; frame++) {
        double t = (double)frame / sample_rate;
        double sample = sin(2.0 * M_PI * frequency * t);
        int64_t sample_value = (int64_t)(amplitude * sample);

        if (bits_per_sample == 8) {
            uint8_t *buf8 = (uint8_t *)buffer;
            uint8_t val8 = (uint8_t)(128 + sample_value);
            for (int ch = 0; ch < channels; ch++) {
                buf8[frame * channels + ch] = val8;
            }
        } else if (bits_per_sample == 16) {
            int16_t *buf16 = (int16_t *)buffer;
            int16_t val16 = (int16_t)(sample_value);
            for (int ch = 0; ch < channels; ch++) {
                buf16[frame * channels + ch] = val16;
            }
        } else if (bits_per_sample == 24) {
            uint8_t *buf24 = (uint8_t *)buffer;
            for (int ch = 0; ch < channels; ch++) {
                int sample_index = (frame * channels + ch) * 3;
                buf24[sample_index + 0] = (uint8_t)(sample_value & 0xFF);
                buf24[sample_index + 1] = (uint8_t)((sample_value >> 8) & 0xFF);
                buf24[sample_index + 2] = (uint8_t)((sample_value >> 16) & 0xFF);
            }
        } else if (bits_per_sample == 32) {
            int32_t *buf32 = (int32_t *)buffer;
            int32_t val32 = (int32_t)(sample_value);
            for (int ch = 0; ch < channels; ch++) {
                buf32[frame * channels + ch] = val32;
            }
        }
    }
    return total_samples;
}

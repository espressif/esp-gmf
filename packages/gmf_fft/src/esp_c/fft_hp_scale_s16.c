/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Saturating int16 shift: shl > 0 left (x * 2^shl, sat to s16), shl < 0 right.
 * Linked when PIE does not provide fft_hp_scale_s16 (portable C fallback).
 */

#include <stdint.h>

#if defined(ESP_PLATFORM)
#include "esp_attr.h"
#define FFT_HP_SCALE_IRAM  IRAM_ATTR
#else
#define FFT_HP_SCALE_IRAM
#endif

void FFT_HP_SCALE_IRAM fft_hp_scale_s16(int16_t *data, int32_t n, int32_t shl)
{
    int32_t i = 0;
    if (shl > 0) {
        if (shl >= 15) {
            for (; i < n; i++) {
                int16_t x = data[i];
                data[i] = (x > 0) ? 32767 : (x < 0 ? (int16_t)-32768 : 0);
            }
            return;
        }
        const int32_t max_in = 32767 >> shl;
        const int32_t min_ok = -32768 >> shl;
        for (; i + 8 <= n; i += 8) {
            for (int k = 0; k < 8; k++) {
                int32_t v = data[i + k];
                if (v > max_in) {
                    data[i + k] = 32767;
                } else if (v < min_ok) {
                    data[i + k] = (int16_t)-32768;
                } else {
                    data[i + k] = (int16_t)(v << shl);
                }
            }
        }
        for (; i < n; i++) {
            int32_t v = data[i];
            if (v > max_in) {
                data[i] = 32767;
            } else if (v < min_ok) {
                data[i] = (int16_t)-32768;
            } else {
                data[i] = (int16_t)(v << shl);
            }
        }
        return;
    }

    const int32_t shr = -shl;
    if (shr >= 16) {
        for (; i < n; i++) {
            data[i] = 0;
        }
        return;
    }
    const int32_t bias = 1 << (shr - 1);
    for (; i + 8 <= n; i += 8) {
        for (int k = 0; k < 8; k++) {
            int32_t v = data[i + k];
            if (v >= 0) {
                data[i + k] = (int16_t)((v + bias) >> shr);
            } else {
                uint32_t ua = 0u - (uint32_t)v;
                data[i + k] = (int16_t)(-(int32_t)((ua + (uint32_t)bias) >> shr));
            }
        }
    }
    for (; i < n; i++) {
        int32_t v = data[i];
        if (v >= 0) {
            data[i] = (int16_t)((v + bias) >> shr);
        } else {
            uint32_t ua = 0u - (uint32_t)v;
            data[i] = (int16_t)(-(int32_t)((ua + (uint32_t)bias) >> shr));
        }
    }
}

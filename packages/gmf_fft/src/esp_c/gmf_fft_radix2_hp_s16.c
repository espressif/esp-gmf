/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * Portable C block-floating-point radix-2 DIF butterflies.
 *
 * Used when PIE HP kernels are not linked (any target that builds
 * src/esp_c/gmf_fft_radix2_dit_s16.c). Each of the log2n stages
 * right-shifts the add path only when max_abs >= 8192, so extra bits
 * stay in the int16 mantissa. Returns the number of stages that applied
 * >>1 (SAR=15). 8192 leaves headroom for |a-b|*W in int16.
 */

#include <stdint.h>

#define FFT_HP_OVERFLOW_TH  8192
#define FFT_HP_TABLE_BITS   14

static inline int16_t fft_hp_sat16(int32_t x)
{
    if (x > 32767) {
        return 32767;
    }
    if (x < -32768) {
        return -32768;
    }
    return (int16_t)x;
}

static int32_t fft_hp_max_abs(const int16_t *data, int32_t n_i16)
{
    int32_t m = 0;
    for (int32_t i = 0; i < n_i16; i++) {
        int32_t a = (int32_t)data[i];
        if (a == -32768) {
            return 32767;
        }
        if (a < 0) {
            a = -a;
        }
        if (a > m) {
            m = a;
        }
    }
    return m;
}

static int32_t fft_hp_pick_shift(const int16_t *data, int32_t n_cpx, int32_t *extra)
{
    int32_t max_abs = fft_hp_max_abs(data, n_cpx * 2);
    if (max_abs >= FFT_HP_OVERFLOW_TH) {
        *extra += 1;
        return 15;
    }
    return 14;
}

int32_t fft_radix2_fft_bf_s16_hp(int16_t *data, int16_t *win, int32_t log2n, int32_t cpx_points)
{
    int32_t n = cpx_points;
    int32_t wp = 0;
    int32_t extra = 0;

    for (int32_t s = log2n - 1; s >= 0; s--) {
        int32_t loop_shift = fft_hp_pick_shift(data, n, &extra);
        int32_t add_round = 1 << (loop_shift - 1);
        int32_t half = 1 << s;
        int32_t m = half << 1;
        int32_t num_groups = n >> (s + 1);
        int32_t tw_base = wp;
        wp += 2 * half;

        for (int32_t g = 0; g < num_groups; g++) {
            int32_t k0 = g * m;
            for (int32_t j = 0; j < half; j++) {
                int32_t wr = (int32_t)win[tw_base + 2 * j];
                int32_t wi = (int32_t)win[tw_base + 2 * j + 1];
                int32_t idx = k0 + j;
                int32_t idx2 = idx + half;
                int32_t ar = (int32_t)data[2 * idx];
                int32_t ai = (int32_t)data[2 * idx + 1];
                int32_t br = (int32_t)data[2 * idx2];
                int32_t bi = (int32_t)data[2 * idx2 + 1];
                int32_t sum_r = ((ar + br) << FFT_HP_TABLE_BITS) + add_round;
                int32_t sum_i = ((ai + bi) << FFT_HP_TABLE_BITS) + add_round;
                data[2 * idx] = fft_hp_sat16(sum_r >> loop_shift);
                data[2 * idx + 1] = fft_hp_sat16(sum_i >> loop_shift);
                int32_t dr = ar - br;
                int32_t di = ai - bi;
                int32_t tr = (int32_t)(((int64_t)dr * wr + (int64_t)di * wi + add_round) >> loop_shift);
                int32_t ti = (int32_t)(((int64_t)di * wr - (int64_t)dr * wi + add_round) >> loop_shift);
                data[2 * idx2] = fft_hp_sat16(tr);
                data[2 * idx2 + 1] = fft_hp_sat16(ti);
            }
        }
    }
    return extra;
}

int32_t fft_radix2_ifft_bf_s16_hp(int16_t *data, int16_t *win, int32_t log2n, int32_t cpx_points)
{
    int32_t n = cpx_points;
    int32_t wp = 0;
    int32_t extra = 0;

    for (int32_t s = log2n - 1; s >= 0; s--) {
        int32_t loop_shift = fft_hp_pick_shift(data, n, &extra);
        int32_t add_round = 1 << (loop_shift - 1);
        int32_t half = 1 << s;
        int32_t m = half << 1;
        int32_t num_groups = n >> (s + 1);
        int32_t tw_base = wp;
        wp += 2 * half;

        for (int32_t g = 0; g < num_groups; g++) {
            int32_t k0 = g * m;
            for (int32_t j = 0; j < half; j++) {
                int32_t wr = (int32_t)win[tw_base + 2 * j];
                int32_t wi = (int32_t)win[tw_base + 2 * j + 1];
                int32_t idx = k0 + j;
                int32_t idx2 = idx + half;
                int32_t ar = (int32_t)data[2 * idx];
                int32_t ai = (int32_t)data[2 * idx + 1];
                int32_t br = (int32_t)data[2 * idx2];
                int32_t bi = (int32_t)data[2 * idx2 + 1];
                int32_t sum_r = ((ar + br) << FFT_HP_TABLE_BITS) + add_round;
                int32_t sum_i = ((ai + bi) << FFT_HP_TABLE_BITS) + add_round;
                data[2 * idx] = fft_hp_sat16(sum_r >> loop_shift);
                data[2 * idx + 1] = fft_hp_sat16(sum_i >> loop_shift);
                int32_t dr = ar - br;
                int32_t di = ai - bi;
                int32_t tr = (int32_t)(((int64_t)dr * wr - (int64_t)di * wi + add_round) >> loop_shift);
                int32_t ti = (int32_t)(((int64_t)di * wr + (int64_t)dr * wi + add_round) >> loop_shift);
                data[2 * idx2] = fft_hp_sat16(tr);
                data[2 * idx2 + 1] = fft_hp_sat16(ti);
            }
        }
    }
    return extra;
}

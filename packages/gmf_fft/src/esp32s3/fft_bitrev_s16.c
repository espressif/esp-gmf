/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * PIE hardware bit-reverse is 3–10 bits (real N <= 2048). Larger sizes use C.
 */

#include <stdint.h>

/* PIE EE.BITREV reverse 3–10 bits (complex length <= 1024). */
#define FFT_PIE_BITREV_MAX_LOG2  10

void fft_radix2_bit_reverse_s16_hw(int16_t *data, int32_t cpx_points, int32_t log2_n);

static void fft_bitrev_soft(int16_t *data, int32_t cpx_points, int32_t log2_n)
{
    int32_t n = cpx_points;
    for (int32_t i = 0; i < n; i++) {
        int32_t j = 0;
        int32_t t = i;
        for (int32_t k = 0; k < log2_n; k++) {
            j = (j << 1) | (t & 1);
            t >>= 1;
        }
        if (j > i) {
            int32_t a = 2 * i;
            int32_t b = 2 * j;
            int16_t t0 = data[a];
            int16_t t1 = data[a + 1];
            data[a] = data[b];
            data[a + 1] = data[b + 1];
            data[b] = t0;
            data[b + 1] = t1;
        }
    }
}

void fft_radix2_bit_reverse_s16(int16_t *data, int32_t cpx_points, int32_t log2_n)
{
    if (log2_n > FFT_PIE_BITREV_MAX_LOG2) {
        fft_bitrev_soft(data, cpx_points, log2_n);
    } else {
        fft_radix2_bit_reverse_s16_hw(data, cpx_points, log2_n);
    }
}

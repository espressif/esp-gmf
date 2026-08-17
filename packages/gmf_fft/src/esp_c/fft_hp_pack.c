/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hermitian pack/unpack in int32. PIE AMS fftr/ffti add in 16 bits and wrap
 * when |sr|+|tr| exceeds the int16 range; HP keeps extra BFP bits, so packing
 * must not truncate the add. Scale matches the C kernels: fftr stores
 * (sr+tr)>>1, ffti stores sr+dr with no extra shift.
 *
 * Linked on targets that do not provide PIE HP pack (portable C fallback).
 */

#include <stdint.h>

#if defined(ESP_PLATFORM)
#include "esp_attr.h"
#define FFT_HP_PACK_IRAM  IRAM_ATTR
#else
#define FFT_HP_PACK_IRAM
#endif

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

static inline void fft_hp_cmul(int32_t ar, int32_t ai, int32_t br, int32_t bi, int32_t shift,
                               int32_t *out_r, int32_t *out_i)
{
    const int32_t rnd = 1 << (shift - 1);
    *out_r = (int32_t)(((int64_t)ar * br - (int64_t)ai * bi + rnd) >> shift);
    *out_i = (int32_t)(((int64_t)ar * bi + (int64_t)ai * br + rnd) >> shift);
}

void FFT_HP_PACK_IRAM fft_hp_fftr(int16_t *data, int16_t *win, int32_t cpx_points, int32_t shift)
{
    int32_t m = cpx_points;
    int32_t z0r = (int32_t)data[0];
    int32_t z0i = (int32_t)data[1];
    int16_t dc = fft_hp_sat16(z0r + z0i);
    int16_t nyq = fft_hp_sat16(z0r - z0i);
    data[0] = dc;
    data[1] = nyq;
    data[2 * m] = nyq;
    data[2 * m + 1] = 0;

    int16_t *p_lo = data + 2;
    int16_t *p_hi = data + 2 * (m - 1);
    int16_t *p_w = win;
    const int32_t niter = (m >> 1) - 1;
    for (int32_t k = 0; k < niter; k++) {
        int32_t ar = (int32_t)p_lo[0];
        int32_t ai = (int32_t)p_lo[1];
        int32_t bcr = (int32_t)p_hi[0];
        int32_t bi = (int32_t)p_hi[1];
        int32_t sr = ar + bcr;
        int32_t si = ai - bi;
        int32_t dr = ar - bcr;
        int32_t di = ai + bi;
        int32_t wr = (int32_t)p_w[0];
        int32_t wi = (int32_t)p_w[1];
        int32_t tr, ti;
        fft_hp_cmul(dr, di, wr, wi, shift, &tr, &ti);
        p_lo[0] = fft_hp_sat16((sr + tr + 1) >> 1);
        p_lo[1] = fft_hp_sat16((si + ti + 1) >> 1);
        p_hi[0] = fft_hp_sat16((sr - tr + 1) >> 1);
        p_hi[1] = fft_hp_sat16((ti - si + 1) >> 1);
        p_lo += 2;
        p_hi -= 2;
        p_w += 2;
    }

    {
        int32_t k = m >> 1;
        int32_t i = 2 * k;
        int32_t ar = (int32_t)data[i];
        int32_t ai = (int32_t)data[i + 1];
        int32_t wr = (int32_t)win[2 * (k - 1)];
        int32_t wi = (int32_t)win[2 * (k - 1) + 1];
        int32_t tr, ti;
        fft_hp_cmul(0, ai + ai, wr, wi, shift, &tr, &ti);
        data[i] = fft_hp_sat16(((ar + ar) + tr + 1) >> 1);
        data[i + 1] = fft_hp_sat16((ti + 1) >> 1);
    }
}

void FFT_HP_PACK_IRAM fft_hp_ffti(int16_t *data, int16_t *win, int32_t cpx_points, int32_t shift)
{
    int32_t m = cpx_points;
    int32_t nyquist = (int32_t)data[1];
    int32_t dc = (int32_t)data[0];
    data[0] = fft_hp_sat16(dc + nyquist);
    data[1] = fft_hp_sat16(dc - nyquist);

    int16_t *p_lo = data + 2;
    int16_t *p_hi = data + 2 * (m - 1);
    int16_t *p_w = win;
    const int32_t niter = (m >> 1) - 1;
    for (int32_t k = 0; k < niter; k++) {
        int32_t xr = (int32_t)p_lo[0];
        int32_t xi = (int32_t)p_lo[1];
        int32_t yr = (int32_t)p_hi[0];
        int32_t yi = (int32_t)p_hi[1];
        int32_t sr = xr + yr;
        int32_t si = xi - yi;
        int32_t tr = xr - yr;
        int32_t ti = xi + yi;
        int32_t wr = (int32_t)p_w[0];
        int32_t wi = -(int32_t)p_w[1];
        int32_t dr, di;
        fft_hp_cmul(tr, ti, wr, wi, shift, &dr, &di);
        p_lo[0] = fft_hp_sat16(sr + dr);
        p_lo[1] = fft_hp_sat16(si + di);
        p_hi[0] = fft_hp_sat16(sr - dr);
        p_hi[1] = fft_hp_sat16(-(si - di));
        p_lo += 2;
        p_hi -= 2;
        p_w += 2;
    }

    {
        int32_t k = m >> 1;
        int32_t i = 2 * k;
        int32_t xr = (int32_t)data[i];
        int32_t xi = (int32_t)data[i + 1];
        int32_t wr = (int32_t)win[2 * (k - 1)];
        int32_t wi = -(int32_t)win[2 * (k - 1) + 1];
        int32_t dr, di;
        fft_hp_cmul(0, xi + xi, wr, wi, shift, &dr, &di);
        data[i] = fft_hp_sat16((xr + xr) + dr);
        data[i + 1] = fft_hp_sat16(di);
    }
}

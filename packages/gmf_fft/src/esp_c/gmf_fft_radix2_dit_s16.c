/*
 * SPDX-FileCopyrightText: 2026 Contributors to esp_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * Scalar fixed-point radix-2 DIF FFT kernels for esp_fft.
 *
 * Algorithm: Decimation-In-Frequency (DIF), matching the Xtensa PIE assembly
 * (fft_pie_radix2_dit_s16.S) which also uses DIF (large-to-small box order).
 *
 * Pipeline order:
 *   Forward: fft_radix2_fft_bf_s16  -> fft_radix2_bit_reverse_s16 -> fft_radix2_fftr_s16
 *   Inverse: fft_radix2_ffti_s16 -> fft_radix2_ifft_bf_s16 -> fft_radix2_bit_reverse_s16
 *
 * DIF butterfly (forward):
 *   top    = sat16((a + b) >> 1)
 *   bottom = sat16(conj(W_stored) * (a - b) >> shift)
 *
 * DIF butterfly (inverse):
 *   top    = sat16((a + b) >> 1)
 *   bottom = sat16(W_stored * (a - b) >> shift)
 *
 * Twiddle layout (built by esp_fft_init in fft_power2_q15.c):
 *   twiddle_win:  DIF butterfly twiddles, Q14, laid out stage by stage.
 *                 Stage s=log2n-1 (largest, processed FIRST) contributes N/4
 *                 (cos, sin) pairs; each subsequent stage contributes half as many.
 *                 Total: N-2 int16 values for N/2 complex points.
 *   twiddle_win2: Hermitian real-spectrum packing twiddles, Q14.
 *                 N/4 (cos, sin) pairs for bins k = 1 .. N/4.
 *
 * Scaling: each butterfly stage divides all outputs by 2 (from Q14/shift=15
 * combination). After log2(N/2) stages the total scale is 2/N, matching the
 * documented "forward → inverse round-trip scaled by 2/N".
 */

#include <stdint.h>

/* ---------- shared helpers ---------- */

static inline int16_t fft_sat16(int32_t x)
{
    if (x > 32767) {
        return 32767;
    }
    if (x < -32768) {
        return -32768;
    }
    return (int16_t)x;
}

/**
 * Forward DIF bottom: conj(W_stored) * (dr + j*di) >> shift
 * W_stored = (wr, wi) = (cos θ, sin θ), conj = (wr, -wi).
 * Matches EE.FFT.CMUL.S16 with even sel (0/2/4/6).
 *
 *   tr = (dr*wr + di*wi) >> shift
 *   ti = (di*wr - dr*wi) >> shift
 */
static inline void fft_mul_w_fwd(int32_t dr, int32_t di,
                                 int32_t wr, int32_t wi, int32_t shift,
                                 int32_t *tr, int32_t *ti)
{
    *tr = (int32_t)(((int64_t)dr * wr + (int64_t)di * wi) >> shift);
    *ti = (int32_t)(((int64_t)di * wr - (int64_t)dr * wi) >> shift);
}

/**
 * Inverse DIF bottom: W_stored * (dr + j*di) >> shift
 * Matches EE.FFT.CMUL.S16 with odd sel (1/3/5/7).
 *
 *   tr = (dr*wr - di*wi) >> shift
 *   ti = (di*wr + dr*wi) >> shift
 */
static inline void fft_mul_w_inv(int32_t dr, int32_t di,
                                 int32_t wr, int32_t wi, int32_t shift,
                                 int32_t *tr, int32_t *ti)
{
    *tr = (int32_t)(((int64_t)dr * wr - (int64_t)di * wi) >> shift);
    *ti = (int32_t)(((int64_t)di * wr + (int64_t)dr * wi) >> shift);
}

/**
 * Standard complex multiply: (ar + j*ai) * (br + j*bi) >> shift.
 * Used in fftr/ffti.
 */
static inline void fft_cmul(int32_t ar, int32_t ai,
                            int32_t br, int32_t bi, int32_t shift,
                            int32_t *out_r, int32_t *out_i)
{
    *out_r = (int32_t)(((int64_t)ar * br - (int64_t)ai * bi) >> shift);
    *out_i = (int32_t)(((int64_t)ar * bi + (int64_t)ai * br) >> shift);
}

/* ---------- kernel functions ---------- */

void fft_radix2_bit_reverse_s16(int16_t *data, int32_t cpx_points, int32_t log2_n)
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

/**
 * Radix-2 DIF forward butterflies.
 *
 * Stages run from LARGEST (half = N/4) to SMALLEST (half = 1).
 * Twiddle table is read forward (tw_base advances from 0).
 * Butterfly j within a stage uses twiddle pair at (tw_base + j).
 *
 * DIF butterfly:
 *   top    = sat16((a + b) >> 1)
 *   bottom = sat16(conj(W) * (a - b) >> shift)
 *
 * This introduces a uniform scale factor of 1/2 per stage (from Q14 twiddles
 * with shift=15), giving total forward scale 2/N after log2(N/2) stages.
 */
void fft_radix2_fft_bf_s16(int16_t *data, int16_t *win, int32_t shift,
                           int32_t log2n, int32_t cpx_points)
{
    int32_t n = cpx_points;

    int32_t wp = 0;

    /* DIF: process from largest butterfly (s = log2n-1) to smallest (s = 0). */
    for (int32_t s = log2n - 1; s >= 0; s--) {
        int32_t half = 1 << s;              /* butterflies per group  */
        int32_t m = half << 1;              /* group size             */
        int32_t num_groups = n >> (s + 1);  /* = n / m               */
        int32_t tw_base = wp;
        wp += 2 * half;  /* advance twiddle pointer */

        for (int32_t g = 0; g < num_groups; g++) {
            int32_t k0 = g * m;
            for (int32_t j = 0; j < half; j++) {
                /* Twiddle for butterfly j is the j-th pair in this stage. */
                int32_t wr = (int32_t)win[tw_base + 2 * j];
                int32_t wi = (int32_t)win[tw_base + 2 * j + 1];
                int32_t idx = k0 + j;
                int32_t idx2 = idx + half;
                int32_t ar = (int32_t)data[2 * idx];
                int32_t ai = (int32_t)data[2 * idx + 1];
                int32_t br = (int32_t)data[2 * idx2];
                int32_t bi = (int32_t)data[2 * idx2 + 1];
                /* DIF butterfly */
                data[2 * idx] = fft_sat16((ar + br) >> 1);
                data[2 * idx + 1] = fft_sat16((ai + bi) >> 1);
                int32_t tr, ti;
                fft_mul_w_fwd(ar - br, ai - bi, wr, wi, shift, &tr, &ti);
                data[2 * idx2] = fft_sat16(tr);
                data[2 * idx2 + 1] = fft_sat16(ti);
            }
        }
    }
}

/**
 * Radix-2 DIF inverse butterflies.
 *
 * Same stage order and twiddle layout as fft_radix2_fft_bf_s16, but uses
 * W_stored for the bottom output, matching EE.FFT.CMUL.S16 odd sel (sel=1).
 *
 * DIF inverse butterfly:
 *   top    = sat16((a + b) >> 1)
 *   bottom = sat16(W_stored * (a - b) >> shift)
 *
 * No internal bit-reverse; the caller (esp_fft_inverse) performs it after.
 */
void fft_radix2_ifft_bf_s16(int16_t *data, int16_t *win, int32_t shift,
                            int32_t log2n, int32_t cpx_points)
{
    int32_t n = cpx_points;

    int32_t wp = 0;

    for (int32_t s = log2n - 1; s >= 0; s--) {
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
                data[2 * idx] = fft_sat16((ar + br) >> 1);
                data[2 * idx + 1] = fft_sat16((ai + bi) >> 1);
                int32_t tr, ti;
                fft_mul_w_inv(ar - br, ai - bi, wr, wi, shift, &tr, &ti);
                data[2 * idx2] = fft_sat16(tr);
                data[2 * idx2 + 1] = fft_sat16(ti);
            }
        }
    }
}

/**
 * Real-to-complex spectrum packing (Hermitian conjugate merge).
 * Called after fft_bf + bit_reverse to produce the N/2+1 unique bins.
 *
 * DC  bin: data[0]        = sat16(Z[0].re + Z[0].im)
 * Nyq bin: data[1]        = sat16(Z[0].re - Z[0].im)
 *           data[2*m]     = same Nyquist copy (used by ffti)
 *           data[2*m + 1] = 0
 *
 * For k = 1 .. m/2-1 (and conjugate mirror at m-k):
 *   sr = Z[k].re + Z[m-k].re,  si = Z[k].im - Z[m-k].im
 *   dr = Z[k].re - Z[m-k].re,  di = Z[k].im + Z[m-k].im
 *   (tr + j*ti) = (dr + j*di) * (wr + j*wi) >> shift
 *   X[k]   = sat16((sr + tr) >> 1) + j * sat16((si + ti) >> 1)
 *   X[m-k] = sat16((sr - tr) >> 1) + j * sat16((ti - si) >> 1)
 *
 * The >>1 mirrors the EE.FFT.AMS.S16.ST.INCP instruction's built-in >>1 store.
 */
void fft_radix2_fftr_s16(int16_t *data, int16_t *win, int32_t cpx_points, int32_t shift)
{
    int32_t m = cpx_points;

    /* DC and Nyquist from Z[0] = Z[0].re + j*Z[0].im. */
    int32_t z0r = (int32_t)data[0];
    int32_t z0i = (int32_t)data[1];
    int16_t dc = fft_sat16(z0r + z0i);
    int16_t nyq = fft_sat16(z0r - z0i);
    data[0] = dc;
    data[1] = nyq;
    data[2 * m] = nyq;
    data[2 * m + 1] = 0;

    /* Bins k = 1 .. m/2 - 1 and their conjugate mirrors at m-k. */
    for (int32_t k = 1; k < (m >> 1); k++) {
        int32_t i1 = 2 * k;
        int32_t i2 = 2 * (m - k);
        int32_t ar = (int32_t)data[i1];
        int32_t ai = (int32_t)data[i1 + 1];
        int32_t bcr = (int32_t)data[i2];
        int32_t bci = -(int32_t)data[i2 + 1];  /* conjugate of Z[m-k] */
        int32_t sr = ar + bcr;
        int32_t si = ai + bci;
        int32_t dr = ar - bcr;
        int32_t di = ai - bci;
        int32_t wr = (int32_t)win[2 * (k - 1)];
        int32_t wi = (int32_t)win[2 * (k - 1) + 1];
        int32_t tr, ti;
        fft_cmul(dr, di, wr, wi, shift, &tr, &ti);
        data[i1] = fft_sat16((sr + tr) >> 1);
        data[i1 + 1] = fft_sat16((si + ti) >> 1);
        data[i2] = fft_sat16((sr - tr) >> 1);
        data[i2 + 1] = fft_sat16((ti - si) >> 1);
    }

    /* k = m/2: self-conjugate (Nyquist-adjacent) bin. */
    {
        int32_t k = m >> 1;
        int32_t i = 2 * k;
        int32_t ar = (int32_t)data[i];
        int32_t ai = (int32_t)data[i + 1];
        int32_t wr = (int32_t)win[2 * (k - 1)];
        int32_t wi = (int32_t)win[2 * (k - 1) + 1];
        /* sr = 2*ar, dr = 0, di = 2*ai */
        int32_t tr, ti;
        fft_cmul(0, ai + ai, wr, wi, shift, &tr, &ti);
        data[i] = fft_sat16(((ar + ar) + tr) >> 1);
        data[i + 1] = fft_sat16(ti >> 1);
    }
}

/**
 * Complex-to-real spectrum unpacking — inverse of fftr.
 * Called before ifft_bf + bit_reverse.
 *
 * Matches the Xtensa PIE assembly which uses EE.FFT.AMS.S16 with immediate=1
 * (no >>1 on any output).  DC/Nyquist edge bins are plain ADD/SUB with no shift.
 * This produces values 2× larger than a symmetric inverse of fftr, which the
 * subsequent ifft_bf stage accounts for.
 *
 * Recovers Z[0] from packed DC/Nyquist (no >>1):
 *   data[0] = sat16(dc + nyquist)
 *   data[1] = sat16(dc - nyquist)
 *
 * For k = 1 .. m/2-1 (and conjugate mirror at m-k):
 *   Uses twiddle conj(W) = (wr, -wi); outputs stored without >>1.
 */
void fft_radix2_ffti_s16(int16_t *data, int16_t *win, int32_t cpx_points, int32_t shift)
{
    int32_t m = cpx_points;

    /**
     * Recover DC and Nyquist. data[1] is the Nyquist bin written by fftr
     * and is always within bounds.  The assembly reads from data[2*m] (the
     * Nyquist copy appended by fftr), but callers sometimes pass a buffer of
     * only n_fft elements (without the +2 tail), making data[2*m] OOB.
     * data[1] == data[2*m] in correct usage, so using data[1] is safe.
     */
    int32_t nyquist = (int32_t)data[1];
    int32_t dc = (int32_t)data[0];
    /* No >>1: matches assembly plain ADD/SUB (EE.FFT.AMS immediate=1). */
    data[0] = fft_sat16(dc + nyquist);
    data[1] = fft_sat16(dc - nyquist);

    /* Bins k = 1 .. m/2 - 1 and their conjugate mirrors at m-k. */
    for (int32_t k = 1; k < (m >> 1); k++) {
        int32_t i1 = 2 * k;
        int32_t i2 = 2 * (m - k);
        int32_t xr = (int32_t)data[i1];
        int32_t xi = (int32_t)data[i1 + 1];
        int32_t yr = (int32_t)data[i2];
        int32_t yi = -(int32_t)data[i2 + 1];  /* conjugate mirror */
        int32_t sr = xr + yr;
        int32_t si = xi + yi;
        int32_t tr = xr - yr;
        int32_t ti = xi - yi;
        int32_t wr = (int32_t)win[2 * (k - 1)];
        int32_t wi = -(int32_t)win[2 * (k - 1) + 1];  /* conj(W) */
        int32_t dr, di;
        fft_cmul(tr, ti, wr, wi, shift, &dr, &di);
        /* No >>1 on any output (assembly EE.FFT.AMS.S16 immediate=1). */
        data[i1] = fft_sat16(sr + dr);
        data[i1 + 1] = fft_sat16(si + di);
        data[i2] = fft_sat16(sr - dr);
        data[i2 + 1] = fft_sat16(-(si - di));
    }
    /* k = m/2: undo the fftr self-conjugate bin treatment (also no >>1). */
    {
        int32_t k = m >> 1;
        int32_t i = 2 * k;
        int32_t xr = (int32_t)data[i];
        int32_t xi = (int32_t)data[i + 1];
        int32_t wr = (int32_t)win[2 * (k - 1)];
        int32_t wi = -(int32_t)win[2 * (k - 1) + 1];
        int32_t dr, di;
        fft_cmul(0, xi + xi, wr, wi, shift, &dr, &di);
        data[i] = fft_sat16((xr + xr) + dr);
        data[i + 1] = fft_sat16(di);
    }
}

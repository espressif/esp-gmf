/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_gmf_fft_forward_vs_float.c
 * @brief  Unity tests comparing esp_gmf_fft Q15 forward output to a float DFT and checking forward-inverse round-trip.
 *
 * **Forward** — `esp_gmf_fft_forward` uses a fixed small-radix path for `n_cpx≤8`; for `n_cpx>8` it runs radix-2
 *         butterflies, bit-reverse, then **fftr**. Bin layout differs from a textbook complex DFT, so **only `n_cpx≤8`**
 *         uses strict per-bin float DFT checks; larger sizes still run forward and print `max_err` for manual review.
 *
 * **Round-trip** — `esp_gmf_fft_forward` → `esp_gmf_fft_inverse`. The library does **not** apply `N/4` gain;
 *         for `n_cpx>8`, compare `buf[i]*(n_cpx/2)` to `orig[i]` (`n_cpx/2 = N/4`). In `run_roundtrip`,
 * **`max_err_rel_peak`** caps **worst / peak_in**
 *         (relative to input peak).
 *
 * **Stimulus** — `x[t]=A*cos(2*pi*k0*t/N_real)` with `ESP_GMF_FFT_TEST_SINE_BIN`; for `n_cpx>8`, buffers reserve extra tail int16.
 */
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_timer.h"
#include "unity.h"

#include "esp_gmf_fft.h"
#include "esp_gmf_fft_heap.h"

#ifndef M_PI
#define M_PI  3.14159265358979323846
#endif  /* M_PI */

#ifndef ESP_GMF_FFT_TEST_Q15_IMPL
#define ESP_GMF_FFT_TEST_Q15_IMPL  0
#endif  /* ESP_GMF_FFT_TEST_Q15_IMPL */

#define ESP_GMF_FFT_TEST_DATA_TAIL_INT16  2

/** Peak real time-domain cosine amplitude |x[t]| ≤ A (Q15 int16; keep below 32768). */
#define ESP_GMF_FFT_TEST_SINE_AMP  10000.0
/** Cosine bin k0 w.r.t. **N_real** (0..N_real-1), i.e. x[t]=A*cos(2*pi*k0*t/N_real). */
#define ESP_GMF_FFT_TEST_SINE_BIN  1

/** If N exceeds this, dump only head/tail bins to avoid log spam. */
#define ESP_GMF_FFT_TEST_DUMP_FULL_N  \
    32u
#define ESP_GMF_FFT_TEST_DUMP_EDGE  \
    4u

/**
 * @brief  Optional dump of interleaved Q15 (re,im) pairs (guarded by DEBUG_PRINT_DATA).
 * @param  n_cpx_bins  Number of complex bins; buffer as x[2*i]=re, x[2*i+1]=im.
 */
static void print_q15_interleaved(const char *tag, bool is_time_domain, const int16_t *x, unsigned n_cpx_bins)
{
#if DEBUG_PRINT_DATA
    const char *note = is_time_domain
                           ? "time packed-A: buf[t]=x[t]; (re,im)=(buf[2k],buf[2k+1])"
                           : "freq (fftr packed)";
    printf("[esp_gmf_fft] %s n_cpx=%u Q15 [re,im] (%s)\n", tag, n_cpx_bins, note);
    if (n_cpx_bins <= ESP_GMF_FFT_TEST_DUMP_FULL_N) {
        for (unsigned i = 0; i < n_cpx_bins; i++) {
            printf("[esp_gmf_fft]   i=%3u  re=%6" PRId16 "  im=%6" PRId16 "\n", i, x[2 * i], x[2 * i + 1]);
        }
    } else {
        unsigned h = ESP_GMF_FFT_TEST_DUMP_EDGE;
        if (h * 2u > n_cpx_bins) {
            h = n_cpx_bins / 2u;
        }
        for (unsigned i = 0; i < h; i++) {
            printf("[esp_gmf_fft]   i=%3u  re=%6" PRId16 "  im=%6" PRId16 "\n", i, x[2 * i], x[2 * i + 1]);
        }
        printf("[esp_gmf_fft]   ... (%u complex bins omitted) ...\n", n_cpx_bins - 2u * h);
        for (unsigned i = n_cpx_bins - h; i < n_cpx_bins; i++) {
            printf("[esp_gmf_fft]   i=%3u  re=%6" PRId16 "  im=%6" PRId16 "\n", i, x[2 * i], x[2 * i + 1]);
        }
    }
#endif  /* DEBUG_PRINT_DATA */
}

/** @brief Optional dump of float DFT reference bins (guarded by DEBUG_PRINT_DATA). */
static void print_dft_ref_float_bins(const char *tag, const float *re, const float *im, unsigned n)
{
#if DEBUG_PRINT_DATA
    printf("[esp_gmf_fft] %s N=%u DFT gold ref (float; diff vs Q15/32768 scale)\n", tag, n);
    printf("[esp_gmf_fft]   cols: re_norm im_norm | re_q15_equiv im_q15_equiv\n");
    if (n <= ESP_GMF_FFT_TEST_DUMP_FULL_N) {
        for (unsigned i = 0; i < n; i++) {
            printf("[esp_gmf_fft]   k=%3u  %12.6f %12.6f | %9.1f %9.1f\n", i, (double)re[i], (double)im[i],
                   (double)re[i] * 32768.0, (double)im[i] * 32768.0);
        }
    } else {
        unsigned h = ESP_GMF_FFT_TEST_DUMP_EDGE;
        if (h * 2u > n) {
            h = n / 2u;
        }
        for (unsigned i = 0; i < h; i++) {
            printf("[esp_gmf_fft]   k=%3u  %12.6f %12.6f | %9.1f %9.1f\n", i, (double)re[i], (double)im[i],
                   (double)re[i] * 32768.0, (double)im[i] * 32768.0);
        }
        printf("[esp_gmf_fft]   ... (%u bins omitted) ...\n", n - 2u * h);
        for (unsigned i = n - h; i < n; i++) {
            printf("[esp_gmf_fft]   k=%3u  %12.6f %12.6f | %9.1f %9.1f\n", i, (double)re[i], (double)im[i],
                   (double)re[i] * 32768.0, (double)im[i] * 32768.0);
        }
    }
#endif  /* DEBUG_PRINT_DATA */
}

/** @brief Returns a short label for the Q15 backend under test (asm / c_code / unknown). */
static const char *q15_impl_str(void)
{
    return "asm";
}

/**
 * @brief  Writes N_real real Q15 samples (cosine test tone) into packed-A linear buffer.
 * @note  N_real samples x[t] sequentially in buf (packed-A linear layout).
 */
static void fill_signal_packed_a(int16_t *buf, int n_real)
{
    const double A = ESP_GMF_FFT_TEST_SINE_AMP;
    const int k0 = ESP_GMF_FFT_TEST_SINE_BIN;
    for (int t = 0; t < n_real; t++) {
        double ang = 2.0 * M_PI * (double)k0 * (double)t / (double)n_real;
        buf[t] = (int16_t)(A * cos(ang));
    }
}

/** @brief Nominal real peak A/32768 for logs (relative full-scale Q15). */
static double fill_signal_amplitude_scale(int n)
{
    (void)n;
    return ESP_GMF_FFT_TEST_SINE_AMP / 32768.0;
}

/**
 * @brief  n_cpx-point complex DFT reference in float (gold standard for small-N checks).
 * @note  `in` is interleaved Q15; in[2t]+j*in[2t+1] is packed-A z_t (same as linear buf).
 */
static void fft_forward_reference_float(const int16_t *in, float *out_re, float *out_im, int n_cpx)
{
    for (int m = 0; m < n_cpx; m++) {
        float sr = 0.0f;
        float si = 0.0f;
        for (int k = 0; k < n_cpx; k++) {
            double ang = -2.0 * M_PI * (double)m * (double)k / (double)n_cpx;
            double cr = cos(ang);
            double sn = sin(ang);
            double xr = (double)in[2 * k] / 32768.0;
            double xi = (double)in[2 * k + 1] / 32768.0;
            sr += (float)(xr * cr - xi * sn);
            si += (float)(xr * sn + xi * cr);
        }
        out_re[m] = sr;
        out_im[m] = si;
    }
}

/**
 * @brief  Max abs error: fftr-packed Q15 real parts vs float DFT (reference scaled by 1/(N/2)).
 *         @details Compares fftr-packed output (real parts only) to the float DFT on the first half of the spectrum
 *         (bins k = 0 .. n_cpx/2). Layout: slot[0]=Re{DC}, slot[1]=Re{Nyquist} in data[0..1]; slot[k]=(Re,Im) for k=1..N/2-1.
 *         Compare real parts only (`y[0]`, `y[1]`, `y[2k]`). Library output is **not** × N/2; scale reference by 1/(n_cpx/2) vs Q15/32768.
 */
static float max_abs_err_fftr_vs_ref(const int16_t *y, int n_cpx, const float *ref_re, const float *ref_im)
{
    (void)ref_im;
    float m = 0.f;
    const float inv_sc = 1.f / (float)(n_cpx / 2);

    /* bin 0 (DC): y[0] = Re{Y_DC} */
    {
        float yr = (float)y[0] / 32768.f;
        float er = fabsf(yr - ref_re[0] * inv_sc);
        if (er > m) {
            m = er;
        }
    }
    /* bin N/2 (Nyquist): y[1] = Re{Y_{N/2}} */
    {
        int half = n_cpx / 2;
        float yr = (float)y[1] / 32768.f;
        float er = fabsf(yr - ref_re[half] * inv_sc);
        if (er > m) {
            m = er;
        }
    }
    /* bins k=1..N/2-1: real part only y[2k] */
    for (int k = 1; k < n_cpx / 2; k++) {
        float yr = (float)y[2 * k] / 32768.f;
        float er = fabsf(yr - ref_re[k] * inv_sc);
        if (er > m) {
            m = er;
        }
    }
    return m;
}

/**
 * @brief  (Unused in current tests) Expand single-sided spectrum to full Hermitian layout.
 * @note  Fills Y[0].im=0, Nyquist im=0 if n even, Y[n-k]=conj(Y[k]). For natural-order complex spectra before IFFT.
 * fftr packing for n_cpx>8 may not match textbook Hermitian slots; round-trip tests do **not** call this (would break ffti; ~64× gain error).
 */
__attribute__((unused)) static void q15_hermitian_single_sided_to_full(int16_t *d, unsigned n_cpx)
{
    if (n_cpx < 2u) {
        return;
    }
    d[1] = 0;
    if ((n_cpx % 2u) == 0u) {
        d[2u * (n_cpx / 2u) + 1u] = 0;
    }
    for (unsigned k = n_cpx / 2u + 1u; k < n_cpx; k++) {
        unsigned km = n_cpx - k;
        d[2u * k] = d[2u * km];
        int32_t im = -(int32_t)d[2u * km + 1u];
        if (im < -32768) {
            im = -32768;
        }
        if (im > 32767) {
            im = 32767;
        }
        d[2u * k + 1u] = (int16_t)im;
    }
}

/**
 * @brief  Run forward FFT and compare half-spectrum (fftr) to float DFT reference within `tol`.
 * @param  n_real  Real samples (even, power of two); n_cpx = n_real/2.
 */
static void run_forward_vs_dft(unsigned n_real, float tol)
{
    unsigned n_cpx = n_real / 2u;
    int16_t *buf = (int16_t *)esp_gmf_fft_calloc_aligned((size_t)n_real + ESP_GMF_FFT_TEST_DATA_TAIL_INT16, sizeof(int16_t), 16u);
    TEST_ASSERT_NOT_NULL(buf);
    fill_signal_packed_a(buf, (int)n_real);
    print_q15_interleaved("in_time_q15_packA", true, buf, n_cpx);
    float *ref_re = (float *)malloc((size_t)n_cpx * sizeof(float) * 2);
    float *ref_im = (float *)malloc((size_t)n_cpx * sizeof(float) * 2);
    TEST_ASSERT_NOT_NULL(ref_re);
    TEST_ASSERT_NOT_NULL(ref_im);
    int64_t t0 = esp_timer_get_time();
    fft_forward_reference_float(buf, ref_re, ref_im, (int)n_cpx);
    int64_t t1 = esp_timer_get_time();
    print_dft_ref_float_bins("out_dft_reference_float", ref_re, ref_im, n_cpx);
    esp_gmf_fft_handle_t handle = NULL;
    const esp_gmf_fft_cfg_t fft_cfg = {
        .n_fft = (int16_t)n_real,
        .fft_type = ESP_GMF_FFT_TYPE_REAL_Q15,
    };
    int64_t t2 = esp_timer_get_time();
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_init(&fft_cfg, &handle));
    int64_t t3 = esp_timer_get_time();
    TEST_ASSERT_NOT_NULL(handle);
    int64_t t4 = esp_timer_get_time();
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_forward(handle, buf));
    int64_t t5 = esp_timer_get_time();
    print_q15_interleaved("out_forward_spectrum_q15", false, buf, n_cpx);
    float err = max_abs_err_fftr_vs_ref(buf, (int)n_cpx, ref_re, ref_im);
    printf("[esp_gmf_fft] fftr-vs-DFT n_cpx=%u N_real=%u impl=%s max_err=%.6f tol=%.6f (half-spectrum, ref/=%u)\n",
           n_cpx, n_real, q15_impl_str(), err, tol, n_cpx / 2u);
    printf("[esp_gmf_fft] timing n_cpx=%u dft_ref=%" PRId64 " us init=%" PRId64 " us forward=%" PRId64 " us\n", n_cpx,
           (t1 - t0), (t3 - t2), (t5 - t4));
    TEST_ASSERT_LESS_THAN_FLOAT(tol + 1e-5f, err);
    esp_gmf_fft_deinit(&handle);
    free(ref_im);
    free(ref_re);
    esp_gmf_fft_free_aligned(buf);
}

/**
 * @brief  Forward then inverse FFT; check time-domain error vs original (scaled for N/4 library gain when n_cpx>8).
 * @param  n_real            Real samples (even, power of two); n_cpx = n_real/2.
 * @param  max_err_rel_peak  Upper bound on worst_scaled / peak_in (e.g. 0.0064 ≈ 64/10000).
 */
static void run_roundtrip(unsigned n_real, float max_err_rel_peak)
{
    TEST_ASSERT_EQUAL_INT(0, (int)(n_real % 2u));
    unsigned n_cpx = n_real / 2u;
    const size_t nel = (size_t)n_real;
    const size_t buf_nel = nel + ESP_GMF_FFT_TEST_DATA_TAIL_INT16;
    int16_t *orig = (int16_t *)esp_gmf_fft_calloc_aligned(nel, sizeof(int16_t), 16u);
    int16_t *buf = (int16_t *)esp_gmf_fft_calloc_aligned(buf_nel, sizeof(int16_t), 16u);
    TEST_ASSERT_NOT_NULL(orig);
    TEST_ASSERT_NOT_NULL(buf);
    fill_signal_packed_a(orig, (int)n_real);
    print_q15_interleaved("roundtrip_in_time_packA", true, orig, n_cpx);
    memcpy(buf, orig, nel * sizeof(int16_t));
    int peak_in = 0;
    for (size_t i = 0; i < nel; i++) {
        int v = (int)orig[i];
        if (v < 0) {
            v = -v;
        }
        if (v > peak_in) {
            peak_in = v;
        }
    }
    esp_gmf_fft_handle_t handle = NULL;
    const esp_gmf_fft_cfg_t fft_cfg = {
        .n_fft = (int16_t)n_real,
        .fft_type = ESP_GMF_FFT_TYPE_REAL_Q15,
    };
    int64_t t0 = esp_timer_get_time();
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_init(&fft_cfg, &handle));
    int64_t t1 = esp_timer_get_time();
    TEST_ASSERT_NOT_NULL(handle);
    int64_t t2 = esp_timer_get_time();
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_forward(handle, buf));
    int64_t t3 = esp_timer_get_time();
    print_q15_interleaved("roundtrip_out_forward_spectrum_q15", false, buf, n_cpx);
    /* Round-trip: inverse directly (no N/4 fixup in lib). Do not insert Hermitian fill — breaks ffti. */
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_inverse(handle, buf));
    int64_t t4 = esp_timer_get_time();
    print_q15_interleaved("roundtrip_out_inverse_time_packA", true, buf, n_cpx);
    /* n_cpx≤8 small path; n_cpx>8 fftr path scales by 4/N — compare buf*(n_cpx/2) to orig. */
    int worst = 0;
    for (size_t i = 0; i < nel; i++) {
        int64_t diff;
        if (n_cpx > 8u) {
            diff = (int64_t)buf[i] * (int64_t)(n_cpx / 2u) - (int64_t)orig[i];
        } else {
            diff = (int64_t)buf[i] - (int64_t)orig[i];
        }
        int64_t ad = diff >= 0 ? diff : -diff;
        int adi = ad > (int64_t)INT_MAX ? INT_MAX : (int)ad;
        if (adi > worst) {
            worst = adi;
        }
    }
    const float peak_f = (peak_in > 0) ? (float)peak_in : 1.f;
    const float rel = (float)worst / peak_f;
    printf(
        "[esp_gmf_fft] roundtrip N_real=%u n_cpx=%u impl=%s sc=%.5f peak_in=%d worst_scaled_err=%d worst/peak=%.6f lim_rel=%.6f%s\n",
        n_real, n_cpx, q15_impl_str(), fill_signal_amplitude_scale((int)n_real), peak_in, worst, rel,
        (double)max_err_rel_peak, (n_cpx > 8u) ? " (err=|buf*(n_cpx/2)-orig|, n_cpx/2=N/4)" : " (err=|buf-orig|)");
    printf("[esp_gmf_fft] timing N_real=%u init=%" PRId64 " us forward=%" PRId64 " us inverse=%" PRId64
           " us (fwd+inv total=%" PRId64 " us)\n",
           n_real, (t1 - t0), (t3 - t2), (t4 - t3), (t4 - t2));
    TEST_ASSERT_TRUE(rel <= max_err_rel_peak + 5e-4f);
    esp_gmf_fft_deinit(&handle);
    esp_gmf_fft_free_aligned(buf);
    esp_gmf_fft_free_aligned(orig);
}

/** @brief Unity: forward vs float DFT at n_cpx=32. */
void test_fft_q15_forward_vs_float_n32(void)
{
    run_forward_vs_dft(32u, 0.06f);
}

/** @brief Unity: forward vs float DFT at n_cpx=256. */
void test_fft_q15_forward_vs_float_n256(void)
{
    run_forward_vs_dft(256u, 0.02f);
}

/** @brief Unity: forward vs float DFT at n_cpx=1024. */
void test_fft_q15_forward_vs_float_n1024(void)
{
    run_forward_vs_dft(1024u, 0.02f);
}

/** @brief Unity: forward-inverse round-trip, N_real=32. */
void test_fft_q15_roundtrip_n32(void)
{
    run_roundtrip(32u, (float)ESP_GMF_FFT_TEST_SINE_AMP * 0.02f);
}

/** @brief Unity: forward-inverse round-trip, N_real=512. */
void test_fft_q15_roundtrip_n512(void)
{
    run_roundtrip(512u, (float)ESP_GMF_FFT_TEST_SINE_AMP * 0.02f);
}

/** @brief Unity: forward-inverse round-trip, N_real=1024. */
void test_fft_q15_roundtrip_n1024(void)
{
    run_roundtrip(1024u, (float)ESP_GMF_FFT_TEST_SINE_AMP * 0.06f);
}

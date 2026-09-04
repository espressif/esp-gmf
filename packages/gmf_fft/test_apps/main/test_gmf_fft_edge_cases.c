/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Edge coverage that the cosine-10000 Unity suite does not hit:
 * full-scale Q15, N=8192, DC, impulse, and mixed forward / HP pairs.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_gmf_fft.h"
#include "esp_gmf_fft_heap.h"
#include "unity.h"

#ifndef M_PI
#define M_PI  3.14159265358979323846
#endif

#define EDGE_TAIL  2

typedef enum {
    SIG_DC = 0,
    SIG_IMPULSE,
    SIG_COS,
} edge_sig_t;

typedef enum {
    PAIR_REG = 0,       /* forward + inverse; identity uses *N/4 */
    PAIR_HP,            /* forward_hp + inverse_hp; identity is Q15 */
    PAIR_FWD_HPINV,     /* forward + inverse_hp (unsupported mix) */
    PAIR_HP_INV,        /* forward_hp + inverse (unsupported mix) */
} edge_pair_t;

static const char *sig_name(edge_sig_t s)
{
    switch (s) {
        case SIG_DC:
            return "dc";
        case SIG_IMPULSE:
            return "impulse";
        case SIG_COS:
            return "cos";
        default:
            return "?";
    }
}

static const char *pair_name(edge_pair_t p)
{
    switch (p) {
        case PAIR_REG:
            return "fwd+inv";
        case PAIR_HP:
            return "hp+hp";
        case PAIR_FWD_HPINV:
            return "fwd+hpinv";
        case PAIR_HP_INV:
            return "hp+inv";
        default:
            return "?";
    }
}

static void fill_edge(int16_t *buf, int n, edge_sig_t sig, int16_t amp)
{
    memset(buf, 0, (size_t)n * sizeof(int16_t));
    switch (sig) {
        case SIG_DC:
            for (int i = 0; i < n; i++) {
                buf[i] = amp;
            }
            break;
        case SIG_IMPULSE:
            buf[0] = amp;
            break;
        case SIG_COS: {
            for (int t = 0; t < n; t++) {
                double v = (double)amp * cos(2.0 * M_PI * (double)t / (double)n);
                if (v > 32767.0) {
                    v = 32767.0;
                }
                if (v < -32768.0) {
                    v = -32768.0;
                }
                buf[t] = (int16_t)lrint(v);
            }
            break;
        }
    }
}

static int peak_abs(const int16_t *x, int n)
{
    int p = 0;
    for (int i = 0; i < n; i++) {
        int v = x[i];
        if (v < 0) {
            v = -v;
        }
        if (v > p) {
            p = v;
        }
    }
    return p > 0 ? p : 1;
}

static int worst_q15(const int16_t *y, const int16_t *orig, int n)
{
    int w = 0;
    for (int i = 0; i < n; i++) {
        int d = (int)y[i] - (int)orig[i];
        if (d < 0) {
            d = -d;
        }
        if (d > w) {
            w = d;
        }
    }
    return w;
}

static int worst_n4(const int16_t *y, const int16_t *orig, int n, unsigned n_cpx)
{
    const int64_t sc = (int64_t)(n_cpx / 2u);
    int w = 0;
    for (int i = 0; i < n; i++) {
        int64_t d = (int64_t)y[i] * sc - (int64_t)orig[i];
        if (d < 0) {
            d = -d;
        }
        if (d > (int64_t)w) {
            w = d > 2147483647 ? 2147483647 : (int)d;
        }
    }
    return w;
}

static void run_one(unsigned n_real, edge_sig_t sig, int16_t amp, edge_pair_t pair,
                    int *out_q15, int *out_n4)
{
    const int n = (int)n_real;
    const unsigned n_cpx = n_real / 2u;
    int16_t *orig = (int16_t *)esp_gmf_fft_calloc_aligned((size_t)n, sizeof(int16_t), 16u);
    int16_t *buf = (int16_t *)esp_gmf_fft_calloc_aligned((size_t)n + EDGE_TAIL, sizeof(int16_t), 16u);
    TEST_ASSERT_NOT_NULL(orig);
    TEST_ASSERT_NOT_NULL(buf);
    fill_edge(orig, n, sig, amp);
    memcpy(buf, orig, (size_t)n * sizeof(int16_t));

    esp_gmf_fft_handle_t handle = NULL;
    const esp_gmf_fft_cfg_t cfg = {
        .n_fft = (int16_t)n_real,
        .fft_type = ESP_GMF_FFT_TYPE_REAL_Q15,
    };
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_init(&cfg, &handle));

    switch (pair) {
        case PAIR_REG:
            TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_forward(handle, buf));
            TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_inverse(handle, buf));
            break;
        case PAIR_HP:
            TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_forward_hp(handle, buf));
            TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_inverse_hp(handle, buf));
            break;
        case PAIR_FWD_HPINV:
            TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_forward(handle, buf));
            TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_inverse_hp(handle, buf));
            break;
        case PAIR_HP_INV:
            TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_forward_hp(handle, buf));
            TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_inverse(handle, buf));
            break;
    }

    const int peak = peak_abs(orig, n);
    const int e_q15 = worst_q15(buf, orig, n);
    const int e_n4 = worst_n4(buf, orig, n, n_cpx);
    int ymin = 32767;
    int ymax = -32768;
    for (int i = 0; i < n; i++) {
        if (buf[i] < ymin) {
            ymin = buf[i];
        }
        if (buf[i] > ymax) {
            ymax = buf[i];
        }
    }
    *out_q15 = e_q15;
    *out_n4 = e_n4;
    printf("[esp_gmf_fft] edge N=%u sig=%s amp=%d pair=%s peak=%d err_q15=%d (%.4f) err_N/4=%d (%.4f) y0=%d y1=%d ymin=%d ymax=%d\n",
           n_real, sig_name(sig), (int)amp, pair_name(pair), peak, e_q15, (double)e_q15 / (double)peak, e_n4,
           (double)e_n4 / (double)peak, (int)buf[0], (int)buf[1], ymin, ymax);

    esp_gmf_fft_deinit(&handle);
    esp_gmf_fft_free_aligned(buf);
    esp_gmf_fft_free_aligned(orig);
}

/** Matched vs mixed pairs: DC / impulse / full-scale cosine at N=1024. Report-only (no rel-err assert). */
void test_fft_q15_edge_dc_impulse_fullscale(void)
{
    const unsigned n = 1024u;
    const edge_sig_t sigs[] = {SIG_DC, SIG_IMPULSE, SIG_COS};
    const int16_t amps[] = {32767, -32768, 32767};
    static const edge_pair_t pairs[] = {PAIR_REG, PAIR_HP, PAIR_FWD_HPINV, PAIR_HP_INV};
    int q15 = 0;
    int n4 = 0;

    for (size_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        for (size_t p = 0; p < sizeof(pairs) / sizeof(pairs[0]); p++) {
            run_one(n, sigs[i], amps[i], pairs[p], &q15, &n4);
        }
    }
    run_one(n, SIG_DC, 10000, PAIR_REG, &q15, &n4);
    run_one(n, SIG_DC, 10000, PAIR_HP, &q15, &n4);
    run_one(n, SIG_IMPULSE, 10000, PAIR_REG, &q15, &n4);
    run_one(n, SIG_IMPULSE, 10000, PAIR_HP, &q15, &n4);
}

/** N=8192 cosine / DC / impulse; matched pairs only. Report-only. */
void test_fft_q15_edge_n8192(void)
{
    int q15 = 0;
    int n4 = 0;
    run_one(2048u, SIG_COS, 10000, PAIR_REG, &q15, &n4);
    run_one(2048u, SIG_COS, 10000, PAIR_HP, &q15, &n4);
    run_one(4096u, SIG_COS, 10000, PAIR_REG, &q15, &n4);
    run_one(4096u, SIG_COS, 10000, PAIR_HP, &q15, &n4);
    run_one(8192u, SIG_COS, 10000, PAIR_REG, &q15, &n4);
    run_one(8192u, SIG_COS, 10000, PAIR_HP, &q15, &n4);
    run_one(8192u, SIG_COS, 32767, PAIR_REG, &q15, &n4);
    run_one(8192u, SIG_COS, 32767, PAIR_HP, &q15, &n4);
    run_one(8192u, SIG_DC, 32767, PAIR_REG, &q15, &n4);
    run_one(8192u, SIG_DC, 32767, PAIR_HP, &q15, &n4);
    run_one(8192u, SIG_IMPULSE, 32767, PAIR_REG, &q15, &n4);
    run_one(8192u, SIG_IMPULSE, 32767, PAIR_HP, &q15, &n4);
}

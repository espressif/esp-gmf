/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file fft_spectrum_print.c
 * @brief  Example: 512-point real FFT, spectrum printed as a vertical bar chart.
 *
 *         Signal : x[t] = A0*cos(2*pi*k0*t/N) + A1*cos(2*pi*k1*t/N) + A2*cos(2*pi*k2*t/N)
 *
 *         Spectrum chart: Y-axis = dB (0 at top, DB_FLOOR at bottom)
 *         X-axis = frequency bin 0 .. PLOT_BINS-1
 *         '#' = bar reaches this row, '.' = bar does not
 *
 *         0 |......#.................#................#.........|
 *        -3 |......#.................#................#.........|
 *        -9 |......#.................#................#.........|
 * -      15 |......#.................#................#.........|
 * -      21 |......#.................#.........................|
 *           +---------------------------------------------------+
 *            0      8     16    24    32    40    48
 */

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_log.h"

#include "esp_gmf_fft.h"
#include "esp_gmf_fft_heap.h"

static const char *TAG = "FFT_SPECTRUM_PRINT";

/* Signal configuration */
#define N_FFT  512

/* Chart configuration */
#define PLOT_BINS    56   /*!< bins shown on x-axis                  */
#define PLOT_HEIGHT  20   /*!< rows (y-axis resolution)              */
#define DB_FLOOR     -60  /*!< dB floor; anything below drawn as '.' */

#define N_CPX      (N_FFT / 2)
#define BUF_INT16  ESP_GMF_FFT_BUFFER_SIZE(N_FFT)

#define N_TONES  3
static const int TONE_BIN[N_TONES] = {8, 24, 40};
static const int TONE_AMP[N_TONES] = {1000, 5000, 3000};

static uint32_t isqrt32(uint32_t n)
{
    if (n == 0u) {
        return 0u;
    }
    uint32_t x = n, y = (n + 1u) >> 1;
    while (y < x) {
        x = y;
        y = (x + n / x) >> 1;
    }
    return x;
}

/* fftr half-spectrum: DC=data[0], Nyquist=data[1], Re/Im=data[2k]/[2k+1] */
static uint32_t bin_magnitude(const int16_t *data, unsigned k)
{
    int32_t re, im;
    if (k == 0u) {
        re = data[0];
        im = 0;
    }
    else if (k == (unsigned)N_CPX) {
        re = data[1];
        im = 0;
    } else {
        re = data[2u * k];
        im = data[2u * k + 1u];
    }
    return isqrt32((uint32_t)(re * re) + (uint32_t)(im * im));
}

static void print_spectrum(const int16_t *data)
{
    /* find peak magnitude */
    uint32_t peak_mag = 1u;
    int peak_bin = 0;
    for (unsigned k = 0u; k < PLOT_BINS; k++) {
        uint32_t m = bin_magnitude(data, k);
        if (m > peak_mag) {
            peak_mag = m;
            peak_bin = (int)k;
        }
    }

    /* compute bar height (0..PLOT_HEIGHT) for each bin */
    int bar[PLOT_BINS];
    for (unsigned k = 0u; k < PLOT_BINS; k++) {
        uint32_t m = bin_magnitude(data, k);
        if (m == 0u) {
            bar[k] = 0;
        } else {
            float db = 20.0f * log10f((float)m / (float)peak_mag);
            if (db < (float)DB_FLOOR) {
                db = (float)DB_FLOOR;
            }
            int h = (int)((db - (float)DB_FLOOR) / (float)(-DB_FLOOR)
                    * (float)PLOT_HEIGHT + 0.5f);  // round to nearest integer
            bar[k] = (h > PLOT_HEIGHT) ? PLOT_HEIGHT : h;
        }
    }

    /* header */
    printf("\n");
    printf("  esp_gmf_fft  N=%u  |  signal: ", N_FFT);
    for (int t = 0; t < N_TONES; t++) {
        printf("cos(bin=%d,A=%d)%s",
               TONE_BIN[t], TONE_AMP[t], t < N_TONES - 1 ? " + " : "");
    }
    printf("\n");
    printf("  Peak: bin=%d  mag=%" PRIu32 "\n\n", peak_bin, peak_mag);

    /* chart: row 0 = 0 dB (top), row PLOT_HEIGHT-1 = DB_FLOOR (bottom) */
    for (int row = 0; row < PLOT_HEIGHT; row++) {
        int row_from_bottom = PLOT_HEIGHT - 1 - row;
        int row_db = DB_FLOOR + (row_from_bottom + 1) * (-DB_FLOOR) / PLOT_HEIGHT;
        printf("%4d |", row_db);
        for (unsigned k = 0u; k < PLOT_BINS; k++) {
            putchar(bar[k] > row_from_bottom ? '#' : ' ');
        }
        printf("|\n");
    }

    /* x-axis */
    printf("     +");
    for (unsigned k = 0u; k < PLOT_BINS; k++) {  putchar('-');  }
    printf("+\n");

    /* bin labels every 8 bins */
    printf("      ");
    for (unsigned k = 0u; k < PLOT_BINS; k++) {
        if (k % 8u == 0u) {
            printf("%-8u", k);
        }
    }
    printf("\n      Bin\n\n");
}

static uint32_t calc_roundtrip_peak_error(const int16_t *origin, const int16_t *recovered)
{
    uint32_t peak_err = 0;
    for (int i = 0; i < N_FFT; i++) {
        int32_t scaled = (int32_t)recovered[i] * (N_FFT / 4);
        int32_t diff = scaled - (int32_t)origin[i];
        uint32_t abs_diff = (diff < 0) ? (uint32_t)-diff : (uint32_t)diff;
        if (abs_diff > peak_err) {
            peak_err = abs_diff;
        }
    }
    return peak_err;
}

void app_main(void)
{
    esp_gmf_fft_handle_t handle = NULL;
    int16_t *buf = NULL;
    int16_t *origin = NULL;

    buf = (int16_t *)esp_gmf_fft_calloc_aligned(BUF_INT16, sizeof(int16_t), 16u);
    if (buf == NULL) {
        ESP_LOGE(TAG, "alloc failed");
        goto cleanup;
    }
    origin = (int16_t *)esp_gmf_fft_calloc_aligned(N_FFT, sizeof(int16_t), 16u);
    if (origin == NULL) {
        ESP_LOGE(TAG, "alloc failed");
        goto cleanup;
    }

    /* multi-tone cosine signal */
    for (int t = 0; t < N_FFT; t++) {
        double v = 0.0;
        for (int tone = 0; tone < N_TONES; tone++) {
            v += TONE_AMP[tone]
                 * cos(2.0 * M_PI * TONE_BIN[tone] * t / (double)N_FFT);
        }
        if (v > 32767.0) {
            v = 32767.0;
        }
        else if (v < -32768.0) {
            v = -32768.0;
        }
        buf[t] = (int16_t)(v + 0.5);
        origin[t] = buf[t];
    }

    const esp_gmf_fft_cfg_t cfg = {
        .n_fft = N_FFT,
        .fft_type = ESP_GMF_FFT_TYPE_REAL_Q15,
    };
    if (esp_gmf_fft_init(&cfg, &handle) != ESP_GMF_FFT_OK) {
        ESP_LOGE(TAG, "esp_gmf_fft_init failed");
        goto cleanup;
    }
    if (esp_gmf_fft_forward(handle, buf) != ESP_GMF_FFT_OK) {
        ESP_LOGE(TAG, "esp_gmf_fft_forward failed");
        goto cleanup;
    }

    print_spectrum(buf);

    if (esp_gmf_fft_inverse(handle, buf) != ESP_GMF_FFT_OK) {
        ESP_LOGE(TAG, "esp_gmf_fft_inverse failed");
        goto cleanup;
    }
    printf("  Round-trip peak error after scaling by N/4: %" PRIu32 "\n", calc_roundtrip_peak_error(origin, buf));

cleanup:
    esp_gmf_fft_deinit(&handle);
    esp_gmf_fft_free_aligned(origin);
    esp_gmf_fft_free_aligned(buf);
}

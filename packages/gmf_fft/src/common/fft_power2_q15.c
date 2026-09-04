/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_gmf_fft.h"
#include "esp_gmf_fft_heap.h"

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#include "sdkconfig.h"
#else
#include <stdio.h>
#define ESP_LOGE(tag, fmt, ...)  fprintf(stderr, "E (%s): " fmt "\n", tag, ##__VA_ARGS__)
#endif  /* defined(ESP_PLATFORM) */

#ifndef M_PI
#define M_PI  3.14159265358979323846
#endif  /* M_PI */

#define ESP_GMF_FFT_MIN_N_FFT  32
#define ESP_GMF_FFT_MAX_N_FFT  8192

/**
 * Butterfly post-multiply right-shift (Q15). Twiddle values are Q14 (1 << 14),
 * so the extra bit gives a built-in 1/2 gain per stage to prevent overflow.
 */
#define FFT_TWIDDLE_SHIFT_BF  15
/**
 * fftr/ffti post-multiply right-shift (Q14), matching the twiddle2 scale.
 */
#define FFT_TWIDDLE_SHIFT_R   14
/** Q15 input: x_float = int16 * 2^FFT_HP_Q15_EXPONENT */
#define FFT_HP_Q15_EXPONENT   (-15)

static const char *TAG = "GMF_FFT";

typedef struct gmf_fft_plan {
    uint16_t                              cpx_point;
    uint8_t                               log2_n;
    int                                   hp_spec_exp;  /* last forward_hp exponent; inverse_hp reads this */
    __attribute__((aligned(16))) int16_t *twiddle_win;
    __attribute__((aligned(16))) int16_t *twiddle_win2;
} gmf_fft_plan_t;

void fft_radix2_fft_bf_s16(int16_t *data, int16_t *win, int32_t shift, int32_t log2n, int32_t cpx_points);
void fft_radix2_ifft_bf_s16(int16_t *data, int16_t *win, int32_t shift, int32_t log2n, int32_t cpx_points);
int32_t fft_radix2_fft_bf_s16_hp(int16_t *data, int16_t *win, int32_t log2n, int32_t cpx_points);
int32_t fft_radix2_ifft_bf_s16_hp(int16_t *data, int16_t *win, int32_t log2n, int32_t cpx_points);
void fft_radix2_bit_reverse_s16(int16_t *data, int32_t cpx_points, int32_t log2_n);
void fft_radix2_fftr_s16(int16_t *data, int16_t *win, int32_t cpx_points, int32_t shift);
void fft_radix2_ffti_s16(int16_t *data, int16_t *win, int32_t cpx_points, int32_t shift);
void fft_hp_fftr(int16_t *data, int16_t *win, int32_t cpx_points, int32_t shift);
void fft_hp_ffti(int16_t *data, int16_t *win, int32_t cpx_points, int32_t shift);
void fft_hp_scale_s16(int16_t *data, int32_t n, int32_t shl);

#if defined(CONFIG_IDF_TARGET_ESP32P4) || (defined(CONFIG_IDF_TARGET_ESP32S31) && CONFIG_GMF_FFT_S31_USE_ASM)
void gmf_fft_pie_cfg_round(void);
#define GMF_FFT_HAS_PIE_CFG_ROUND  1
#endif  /* defined(CONFIG_IDF_TARGET_ESP32P4) || (defined(CONFIG_IDF_TARGET_ESP32S31) && CONFIG_GMF_FFT_S31_USE_ASM) */

static int16_t fft_q14_from_double(double x)
{
    long v = (long)round(x * (double)(1 << FFT_TWIDDLE_SHIFT_R));
    if (v > 32767) {
        v = 32767;
    }
    if (v < -32768) {
        v = -32768;
    }
    return (int16_t)v;
}

/** @brief Returns log2(n) for a power-of-two n, or -1 if n is zero or not a power of two. */
static int fft_log2_pow2(uint16_t n)
{
    if (n == 0u || (n & (n - 1u)) != 0u) {
        return -1;
    }
    int r = 0;
    while (n > 1u) {
        n >>= 1;
        r++;
    }
    return r;
}

/** Convert HP block-float int16 (`x_float = data[i] * 2^out_exponent`) back to Q15. */
static void fft_hp_scale_to_q15(int16_t *data, int n, int out_exponent)
{
    const int shl = out_exponent - FFT_HP_Q15_EXPONENT;
    if (shl == 0 || n <= 0) {
        return;
    }
    fft_hp_scale_s16(data, (int32_t)n, (int32_t)shl);
}

static void fft_plan_free(gmf_fft_plan_t *plan)
{
    if (plan == NULL) {
        return;
    }
    esp_gmf_fft_free_aligned(plan->twiddle_win);
    esp_gmf_fft_free_aligned(plan->twiddle_win2);
    esp_gmf_fft_free_aligned(plan);
}

esp_gmf_fft_err_t esp_gmf_fft_init(const esp_gmf_fft_cfg_t *cfg, esp_gmf_fft_handle_t *handle)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "invalid argument: cfg=%p handle=%p", (void *)cfg, (void *)handle);
        return ESP_GMF_FFT_ERR_INVALID_ARG;
    }
    *handle = NULL;
    if (cfg == NULL) {
        ESP_LOGE(TAG, "invalid argument: cfg=%p handle=%p", (void *)cfg, (void *)handle);
        return ESP_GMF_FFT_ERR_INVALID_ARG;
    }
    const int16_t n_fft = cfg->n_fft;
    if (n_fft < ESP_GMF_FFT_MIN_N_FFT || n_fft > ESP_GMF_FFT_MAX_N_FFT || (n_fft & (n_fft - 1)) != 0) {
        ESP_LOGE(TAG, "invalid n_fft=%" PRId16 ", expected power-of-two in [%d, %d]",
                 n_fft, ESP_GMF_FFT_MIN_N_FFT, ESP_GMF_FFT_MAX_N_FFT);
        return ESP_GMF_FFT_ERR_INVALID_ARG;
    }
    if (cfg->fft_type != ESP_GMF_FFT_TYPE_REAL_Q15) {
        ESP_LOGE(TAG, "unsupported fft_type=%d", (int)cfg->fft_type);
        return ESP_GMF_FFT_ERR_INVALID_ARG;
    }
    gmf_fft_plan_t *plan = (gmf_fft_plan_t *)esp_gmf_fft_calloc_aligned(1, sizeof(gmf_fft_plan_t), 4u);
    if (plan == NULL) {
        ESP_LOGE(TAG, "failed to allocate FFT plan");
        return ESP_GMF_FFT_ERR_NO_MEM;
    }
    plan->cpx_point = (uint16_t)n_fft >> 1;                  /* n_cpx = n_real / 2 */
    plan->log2_n = (uint8_t)fft_log2_pow2(plan->cpx_point);  /* log2(n_cpx) = log2(n_real) - 1 */
    plan->hp_spec_exp = FFT_HP_Q15_EXPONENT;
    plan->twiddle_win = NULL;
    plan->twiddle_win2 = NULL;
    plan->twiddle_win = (int16_t *)esp_gmf_fft_calloc_aligned((size_t)n_fft, sizeof(int16_t), 16u);
    if (plan->twiddle_win == NULL) {
        ESP_LOGE(TAG, "failed to allocate twiddle table");
        goto error;
    }
    plan->twiddle_win2 = (int16_t *)esp_gmf_fft_calloc_aligned((size_t)plan->cpx_point, sizeof(int16_t), 16u);
    if (plan->twiddle_win2 == NULL) {
        ESP_LOGE(TAG, "failed to allocate real FFT twiddle table");
        goto error;
    }
    int count = (int)plan->cpx_point;
    int16_t *win_ptr = plan->twiddle_win;
    for (int i = 1; i <= (int)plan->log2_n; i++) {
        count = count >> 1;
        for (int j = 0; j < count; j++) {
            double angle = M_PI * pow(2.0, (double)i) * (double)j / (double)plan->cpx_point;
            *win_ptr++ = fft_q14_from_double(cos(angle));
            *win_ptr++ = fft_q14_from_double(sin(angle));
        }
    }
    for (uint16_t i = 0u; i < plan->cpx_point / 2u; i++) {
        double phase = -M_PI * ((double)(i + 1u) / (double)plan->cpx_point + 0.5);
        plan->twiddle_win2[i * 2u + 0u] = fft_q14_from_double(cos(phase));
        plan->twiddle_win2[i * 2u + 1u] = fft_q14_from_double(sin(phase));
    }
#if defined(GMF_FFT_HAS_PIE_CFG_ROUND)
    gmf_fft_pie_cfg_round();
#endif  /* defined(GMF_FFT_HAS_PIE_CFG_ROUND) */
    *handle = plan;
    return ESP_GMF_FFT_OK;
error:
    fft_plan_free(plan);
    return ESP_GMF_FFT_ERR_NO_MEM;
}

void esp_gmf_fft_deinit(esp_gmf_fft_handle_t *handle)
{
    if (handle == NULL || *handle == NULL) {
        return;
    }
    gmf_fft_plan_t *plan = *handle;
    fft_plan_free(plan);
    *handle = NULL;
}

esp_gmf_fft_err_t esp_gmf_fft_forward(esp_gmf_fft_handle_t handle, int16_t *data)
{
    if (handle == NULL || data == NULL) {
        ESP_LOGE(TAG, "invalid argument: handle=%p data=%p", (void *)handle, (void *)data);
        return ESP_GMF_FFT_ERR_INVALID_ARG;
    }
    gmf_fft_plan_t *plan = (gmf_fft_plan_t *)handle;
    fft_radix2_fft_bf_s16(data, plan->twiddle_win, FFT_TWIDDLE_SHIFT_BF, (int32_t)plan->log2_n, (int32_t)plan->cpx_point);
    fft_radix2_bit_reverse_s16(data, (int32_t)plan->cpx_point, (int32_t)plan->log2_n);
    fft_radix2_fftr_s16(data, plan->twiddle_win2, (int32_t)plan->cpx_point, FFT_TWIDDLE_SHIFT_R);
    return ESP_GMF_FFT_OK;
}

esp_gmf_fft_err_t esp_gmf_fft_inverse(esp_gmf_fft_handle_t handle, int16_t *data)
{
    if (handle == NULL || data == NULL) {
        ESP_LOGE(TAG, "invalid argument: handle=%p data=%p", (void *)handle, (void *)data);
        return ESP_GMF_FFT_ERR_INVALID_ARG;
    }
    gmf_fft_plan_t *plan = (gmf_fft_plan_t *)handle;
    fft_radix2_ffti_s16(data, plan->twiddle_win2, (int32_t)plan->cpx_point, FFT_TWIDDLE_SHIFT_R);
    fft_radix2_ifft_bf_s16(data, plan->twiddle_win, FFT_TWIDDLE_SHIFT_BF, (int32_t)plan->log2_n, (int32_t)plan->cpx_point);
    fft_radix2_bit_reverse_s16(data, (int32_t)plan->cpx_point, (int32_t)plan->log2_n);
    return ESP_GMF_FFT_OK;
}

esp_gmf_fft_err_t esp_gmf_fft_forward_hp(esp_gmf_fft_handle_t handle, int16_t *data)
{
    if (handle == NULL || data == NULL) {
        ESP_LOGE(TAG, "invalid argument: handle=%p data=%p", (void *)handle, (void *)data);
        return ESP_GMF_FFT_ERR_INVALID_ARG;
    }
    gmf_fft_plan_t *plan = (gmf_fft_plan_t *)handle;
    int32_t bf_shifts = fft_radix2_fft_bf_s16_hp(data, plan->twiddle_win,
                                                 (int32_t)plan->log2_n, (int32_t)plan->cpx_point);
    fft_radix2_bit_reverse_s16(data, (int32_t)plan->cpx_point, (int32_t)plan->log2_n);
    fft_hp_fftr(data, plan->twiddle_win2, (int32_t)plan->cpx_point, FFT_TWIDDLE_SHIFT_R);
    /* Q15 in_exponent is fixed; BFP >>1 count stays in the handle for inverse_hp. */
    plan->hp_spec_exp = FFT_HP_Q15_EXPONENT + (int)bf_shifts;
    return ESP_GMF_FFT_OK;
}

esp_gmf_fft_err_t esp_gmf_fft_inverse_hp(esp_gmf_fft_handle_t handle, int16_t *data)
{
    if (handle == NULL || data == NULL) {
        ESP_LOGE(TAG, "invalid argument: handle=%p data=%p", (void *)handle, (void *)data);
        return ESP_GMF_FFT_ERR_INVALID_ARG;
    }
    gmf_fft_plan_t *plan = (gmf_fft_plan_t *)handle;
    fft_hp_ffti(data, plan->twiddle_win2, (int32_t)plan->cpx_point, FFT_TWIDDLE_SHIFT_R);
    int32_t bf_shifts = fft_radix2_ifft_bf_s16_hp(data, plan->twiddle_win,
                                                  (int32_t)plan->log2_n, (int32_t)plan->cpx_point);
    fft_radix2_bit_reverse_s16(data, (int32_t)plan->cpx_point, (int32_t)plan->log2_n);
    /* Inverse of the always-shift 4/N path. Extra skipped >>1 bits are folded back to Q15.
     * ffti has no extra >>1; -1 matches the original 4/N vs 2^exp identity. */
    int time_exp = plan->hp_spec_exp + (int)bf_shifts - (int)plan->log2_n - 1;
    fft_hp_scale_to_q15(data, (int)plan->cpx_point * 2, time_exp);
    return ESP_GMF_FFT_OK;
}

/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Buffer length in int16_t elements for in-place real FFT/IFFT data
 */
#define ESP_GMF_FFT_BUFFER_SIZE(n_fft)  ((size_t)(n_fft) + 2U)

/**
 * @brief  Return codes for esp_gmf_fft API functions
 */
typedef enum {
    ESP_GMF_FFT_OK              = 0,   /*!< Success */
    ESP_GMF_FFT_ERR_INVALID_ARG = -1,  /*!< NULL pointer, unsupported type, out-of-range `n_fft`, or non-power-of-two */
    ESP_GMF_FFT_ERR_NO_MEM      = -2,  /*!< Heap allocation failed */
} esp_gmf_fft_err_t;

/**
 * @brief  FFT handle (twiddle tables and transform size)
 */
typedef void *esp_gmf_fft_handle_t;

/**
 * @brief  FFT transform type
 */
typedef enum {
    ESP_GMF_FFT_TYPE_REAL_Q15 = 0,  /*!< Real-input Q15 FFT/IFFT */
} esp_gmf_fft_type_t;

/**
 * @brief  Configuration for @ref esp_gmf_fft_init
 */
typedef struct {
    int16_t             n_fft;     /*!< Number of real samples `N` (power of two, range [32, 8192]) */
    esp_gmf_fft_type_t  fft_type;  /*!< Transform type */
} esp_gmf_fft_cfg_t;

/**
 * @brief  Allocates twiddle tables and builds a FFT handle from `cfg`
 *
 * @note  Fixed-point precision: Q14 twiddle factors with Q15 butterfly shift (1/2 gain per stage to prevent overflow)
 *
 * @param[in]   cfg     Non-NULL configuration
 * @param[out]  handle  Receives the FFT handle; set to `NULL` on failure
 *
 * @return
 *       - ESP_GMF_FFT_OK               Succeeded; `*handle` is valid
 *       - ESP_GMF_FFT_ERR_INVALID_ARG  `cfg` or `handle` is `NULL`, `n_fft` is not a power-of-two in [32, 8192],
 *                                      or `fft_type` is unsupported
 *       - ESP_GMF_FFT_ERR_NO_MEM       Heap allocation failed
 */
esp_gmf_fft_err_t esp_gmf_fft_init(const esp_gmf_fft_cfg_t *cfg, esp_gmf_fft_handle_t *handle);

/**
 * @brief  Releases the FFT handle and clears the caller's pointer
 *
 * @param[in,out]  handle  Pointer to the FFT handle; `*handle` is set to `NULL` after free
 *                         If `handle` or `*handle` is `NULL`, no operation is performed
 */
void esp_gmf_fft_deinit(esp_gmf_fft_handle_t *handle);

/**
 * @brief  In-place forward FFT on interleaved data
 *
 * @note
 *       1. `data` is an in-place processing buffer used to store both input and output data.
 *          Its capacity must be no less than `ESP_GMF_FFT_BUFFER_SIZE(n_fft) * sizeof(int16_t)` bytes.
 *       2. The forward transform outputs half-spectrum + 1 (`N/2 + 1` frequency bins) in-place:
 *          `data[0]` = DC real, `data[1]` = Nyquist real, and `data[2k], data[2k+1]` = Re/Im
 *          of bin `k` for `k = 1..N/2`
 *
 * @param[in]      handle  Handle obtained from @ref esp_gmf_fft_init
 * @param[in,out]  data    Pointer to in-place real input and packed spectrum output
 *
 * @return
 *       - ESP_GMF_FFT_OK               Succeeded
 *       - ESP_GMF_FFT_ERR_INVALID_ARG  `data` or `handle` is `NULL`
 */
esp_gmf_fft_err_t esp_gmf_fft_forward(esp_gmf_fft_handle_t handle, int16_t *data);

/**
 * @brief  In-place inverse FFT on the half-spectrum produced by @ref esp_gmf_fft_forward
 *
 * @note
 *       1. `data` is an in-place processing buffer used to store both input and output data.
 *          Its capacity must be no less than `ESP_GMF_FFT_BUFFER_SIZE(n_fft) * sizeof(int16_t)` bytes.
 *       2. Input must use the half-spectrum layout produced by @ref esp_gmf_fft_forward
 *       3. A `forward -> inverse` round trip is scaled by `4/N` relative to the original input
 *          Multiply the inverse output by `N/4` if identity scaling is required
 *
 * @param[in]      handle  Handle obtained from @ref esp_gmf_fft_init
 * @param[in,out]  data    Pointer to packed spectrum input and in-place real output
 *
 * @return
 *       - ESP_GMF_FFT_OK               Succeeded
 *       - ESP_GMF_FFT_ERR_INVALID_ARG  `data` or `handle` is `NULL`
 */
esp_gmf_fft_err_t esp_gmf_fft_inverse(esp_gmf_fft_handle_t handle, int16_t *data);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

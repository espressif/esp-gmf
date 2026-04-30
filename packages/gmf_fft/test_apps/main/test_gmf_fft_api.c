/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_gmf_fft_api.c
 * @brief  Interface validation tests for the public esp_gmf_fft API (esp_gmf_fft.h).
 *
 *         Covers argument validation, error-code contracts, and handle lifecycle for
 *         esp_gmf_fft_init / esp_gmf_fft_deinit / esp_gmf_fft_forward / esp_gmf_fft_inverse.
 *         These tests do NOT check numerical correctness — only API contract compliance.
 */

#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "esp_gmf_fft.h"
#include "esp_gmf_fft_heap.h"

/* A valid n_fft (n_real) accepted by the library. */
#define VALID_N_FFT  64

/* Buffer length for VALID_N_FFT: n_real + 2 guard int16 (fftr output needs N+2). */
#define BUF_LEN  (VALID_N_FFT + 2)

/* -------------------------------------------------------------------------
 * esp_gmf_fft_init — argument validation
 * ---------------------------------------------------------------------- */

/** @brief esp_gmf_fft_init with NULL cfg must return ESP_GMF_FFT_ERR_INVALID_ARG. */
void test_api_init_null_cfg(void)
{
    esp_gmf_fft_handle_t h = NULL;
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_ERR_INVALID_ARG, esp_gmf_fft_init(NULL, &h));
    TEST_ASSERT_NULL(h);
}

/** @brief esp_gmf_fft_init with NULL handle must return ESP_GMF_FFT_ERR_INVALID_ARG. */
void test_api_init_null_handle(void)
{
    const esp_gmf_fft_cfg_t cfg = {.n_fft = VALID_N_FFT, .fft_type = ESP_GMF_FFT_TYPE_REAL_Q15};
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_ERR_INVALID_ARG, esp_gmf_fft_init(&cfg, NULL));
}

/** @brief esp_gmf_fft_init with n_fft below minimum must return ESP_GMF_FFT_ERR_INVALID_ARG. */
void test_api_init_n_fft_too_small(void)
{
    esp_gmf_fft_handle_t h = NULL;
    const esp_gmf_fft_cfg_t cfg = {.n_fft = 16, .fft_type = ESP_GMF_FFT_TYPE_REAL_Q15};
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_ERR_INVALID_ARG, esp_gmf_fft_init(&cfg, &h));
    TEST_ASSERT_NULL(h);
}

/** @brief esp_gmf_fft_init with n_fft above maximum must return ESP_GMF_FFT_ERR_INVALID_ARG. */
void test_api_init_n_fft_too_large(void)
{
    esp_gmf_fft_handle_t h = NULL;
    const esp_gmf_fft_cfg_t cfg = {.n_fft = (int16_t)(8192 + 2), .fft_type = ESP_GMF_FFT_TYPE_REAL_Q15};
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_ERR_INVALID_ARG, esp_gmf_fft_init(&cfg, &h));
    TEST_ASSERT_NULL(h);
}

/** @brief esp_gmf_fft_init with n_fft that is not a power of two must return ESP_GMF_FFT_ERR_INVALID_ARG. */
void test_api_init_n_fft_not_pow2(void)
{
    esp_gmf_fft_handle_t h = NULL;
    const esp_gmf_fft_cfg_t cfg = {.n_fft = 100, .fft_type = ESP_GMF_FFT_TYPE_REAL_Q15};
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_ERR_INVALID_ARG, esp_gmf_fft_init(&cfg, &h));
    TEST_ASSERT_NULL(h);
}

/** @brief esp_gmf_fft_init with unsupported fft_type must return ESP_GMF_FFT_ERR_INVALID_ARG. */
void test_api_init_unsupported_fft_type(void)
{
    esp_gmf_fft_handle_t h = NULL;
    const esp_gmf_fft_cfg_t cfg = {.n_fft = VALID_N_FFT, .fft_type = (esp_gmf_fft_type_t)1};
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_ERR_INVALID_ARG, esp_gmf_fft_init(&cfg, &h));
    TEST_ASSERT_NULL(h);
}

/** @brief esp_gmf_fft_init with valid cfg must return ESP_GMF_FFT_OK and a non-NULL handle. */
void test_api_init_valid(void)
{
    esp_gmf_fft_handle_t h = NULL;
    const esp_gmf_fft_cfg_t cfg = {.n_fft = VALID_N_FFT, .fft_type = ESP_GMF_FFT_TYPE_REAL_Q15};
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_init(&cfg, &h));
    TEST_ASSERT_NOT_NULL(h);
    esp_gmf_fft_deinit(&h);
}

/* -------------------------------------------------------------------------
 * esp_gmf_fft_deinit — lifecycle
 * ---------------------------------------------------------------------- */

/** @brief esp_gmf_fft_deinit with NULL pointer must not crash. */
void test_api_deinit_null_ptr(void)
{
    esp_gmf_fft_deinit(NULL);  /* must be a no-op */
}

/** @brief esp_gmf_fft_deinit with pointer to NULL handle must not crash. */
void test_api_deinit_null_handle(void)
{
    esp_gmf_fft_handle_t h = NULL;
    esp_gmf_fft_deinit(&h);  /* must be a no-op */
}

/** @brief esp_gmf_fft_deinit must set the caller's handle to NULL after freeing. */
void test_api_deinit_clears_handle(void)
{
    esp_gmf_fft_handle_t h = NULL;
    const esp_gmf_fft_cfg_t cfg = {.n_fft = VALID_N_FFT, .fft_type = ESP_GMF_FFT_TYPE_REAL_Q15};
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_init(&cfg, &h));
    TEST_ASSERT_NOT_NULL(h);
    esp_gmf_fft_deinit(&h);
    TEST_ASSERT_NULL(h);
}

/* -------------------------------------------------------------------------
 * esp_gmf_fft_forward — argument validation
 * ---------------------------------------------------------------------- */

/** @brief esp_gmf_fft_forward with NULL data must return ESP_GMF_FFT_ERR_INVALID_ARG. */
void test_api_forward_null_data(void)
{
    esp_gmf_fft_handle_t h = NULL;
    const esp_gmf_fft_cfg_t cfg = {.n_fft = VALID_N_FFT, .fft_type = ESP_GMF_FFT_TYPE_REAL_Q15};
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_init(&cfg, &h));
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_ERR_INVALID_ARG, esp_gmf_fft_forward(h, NULL));
    esp_gmf_fft_deinit(&h);
}

/** @brief esp_gmf_fft_forward with NULL handle must return ESP_GMF_FFT_ERR_INVALID_ARG. */
void test_api_forward_null_handle(void)
{
    int16_t buf[BUF_LEN];
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_ERR_INVALID_ARG, esp_gmf_fft_forward(NULL, buf));
}

/** @brief esp_gmf_fft_forward with valid args must return ESP_GMF_FFT_OK. */
void test_api_forward_valid(void)
{
    int16_t *buf = (int16_t *)esp_gmf_fft_calloc_aligned(BUF_LEN, sizeof(int16_t), 16u);
    TEST_ASSERT_NOT_NULL(buf);
    esp_gmf_fft_handle_t h = NULL;
    const esp_gmf_fft_cfg_t cfg = {.n_fft = VALID_N_FFT, .fft_type = ESP_GMF_FFT_TYPE_REAL_Q15};
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_init(&cfg, &h));
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_forward(h, buf));
    esp_gmf_fft_deinit(&h);
    esp_gmf_fft_free_aligned(buf);
}

/* -------------------------------------------------------------------------
 * esp_gmf_fft_inverse — argument validation
 * ---------------------------------------------------------------------- */

/** @brief esp_gmf_fft_inverse with NULL data must return ESP_GMF_FFT_ERR_INVALID_ARG. */
void test_api_inverse_null_data(void)
{
    esp_gmf_fft_handle_t h = NULL;
    const esp_gmf_fft_cfg_t cfg = {.n_fft = VALID_N_FFT, .fft_type = ESP_GMF_FFT_TYPE_REAL_Q15};
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_init(&cfg, &h));
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_ERR_INVALID_ARG, esp_gmf_fft_inverse(h, NULL));
    esp_gmf_fft_deinit(&h);
}

/** @brief esp_gmf_fft_inverse with NULL handle must return ESP_GMF_FFT_ERR_INVALID_ARG. */
void test_api_inverse_null_handle(void)
{
    int16_t buf[BUF_LEN];
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_ERR_INVALID_ARG, esp_gmf_fft_inverse(NULL, buf));
}

/** @brief esp_gmf_fft_inverse with valid args must return ESP_GMF_FFT_OK. */
void test_api_inverse_valid(void)
{
    int16_t *buf = (int16_t *)esp_gmf_fft_calloc_aligned(BUF_LEN, sizeof(int16_t), 16u);
    TEST_ASSERT_NOT_NULL(buf);
    esp_gmf_fft_handle_t h = NULL;
    const esp_gmf_fft_cfg_t cfg = {.n_fft = VALID_N_FFT, .fft_type = ESP_GMF_FFT_TYPE_REAL_Q15};
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_init(&cfg, &h));
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_forward(h, buf));
    TEST_ASSERT_EQUAL_INT(ESP_GMF_FFT_OK, esp_gmf_fft_inverse(h, buf));
    esp_gmf_fft_deinit(&h);
    esp_gmf_fft_free_aligned(buf);
}

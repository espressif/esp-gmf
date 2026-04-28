/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file gmf_fft_test_runner.c
 * @brief  Registers and executes esp_gmf_fft Q15 Unity tests in a fixed order.
 */

#include "unity.h"

#include "gmf_fft_test_cases.h"
#include "gmf_fft_test_runner.h"

/** @brief Starts Unity, runs all esp_gmf_fft tests, ends Unity (prints summary). */
void esp_gmf_fft_test_runner_run(void)
{
    UNITY_BEGIN();

    /* API contract */
    RUN_TEST(test_api_init_null_cfg);
    RUN_TEST(test_api_init_null_handle);
    RUN_TEST(test_api_init_n_fft_too_small);
    RUN_TEST(test_api_init_n_fft_too_large);
    RUN_TEST(test_api_init_n_fft_not_pow2);
    RUN_TEST(test_api_init_unsupported_fft_type);
    RUN_TEST(test_api_init_valid);
    RUN_TEST(test_api_deinit_null_ptr);
    RUN_TEST(test_api_deinit_null_handle);
    RUN_TEST(test_api_deinit_clears_handle);
    RUN_TEST(test_api_forward_null_data);
    RUN_TEST(test_api_forward_null_handle);
    RUN_TEST(test_api_forward_valid);
    RUN_TEST(test_api_inverse_null_data);
    RUN_TEST(test_api_inverse_null_handle);
    RUN_TEST(test_api_inverse_valid);

    /* Numerical correctness */
    RUN_TEST(test_fft_q15_forward_vs_float_n32);
    RUN_TEST(test_fft_q15_forward_vs_float_n256);
    RUN_TEST(test_fft_q15_forward_vs_float_n1024);
    RUN_TEST(test_fft_q15_roundtrip_n32);
    RUN_TEST(test_fft_q15_roundtrip_n512);
    RUN_TEST(test_fft_q15_roundtrip_n1024);

    (void)UNITY_END();
}

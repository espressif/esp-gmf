/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file gmf_fft_test_cases.h
 * @brief  Declares Unity test cases implemented by the esp_gmf_fft test sources.
 */
#pragma once

/* API interface tests (test_gmf_fft_api.c) */
void test_api_init_null_cfg(void);
void test_api_init_null_handle(void);
void test_api_init_n_fft_too_small(void);
void test_api_init_n_fft_too_large(void);
void test_api_init_n_fft_not_pow2(void);
void test_api_init_unsupported_fft_type(void);
void test_api_init_valid(void);
void test_api_deinit_null_ptr(void);
void test_api_deinit_null_handle(void);
void test_api_deinit_clears_handle(void);
void test_api_forward_null_data(void);
void test_api_forward_null_handle(void);
void test_api_forward_valid(void);
void test_api_inverse_null_data(void);
void test_api_inverse_null_handle(void);
void test_api_inverse_valid(void);

/* Numerical tests (test_gmf_fft_forward_vs_float.c) */
void test_fft_q15_forward_vs_float_n32(void);
void test_fft_q15_forward_vs_float_n256(void);
void test_fft_q15_forward_vs_float_n1024(void);
void test_fft_q15_roundtrip_n32(void);
void test_fft_q15_roundtrip_n512(void);
void test_fft_q15_roundtrip_n1024(void);

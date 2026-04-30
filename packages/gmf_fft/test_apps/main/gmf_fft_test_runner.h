/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file gmf_fft_test_runner.h
 * @brief  Declares the Unity test runner entry for esp_gmf_fft on-chip tests
 */
#pragma once

/** @brief Runs the esp_gmf_fft Unity test group (forward vs float + round-trip) */
void esp_gmf_fft_test_runner_run(void);

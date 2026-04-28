/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file app_main.c
 * @brief  ESP-IDF application entry for the gmf_fft Unity test app.
 */

#include "esp_task_wdt.h"
#include "gmf_fft_test_runner.h"

/** @brief Disable task WDT and run all registered esp_gmf_fft Unity tests. */
void app_main(void)
{
    (void)esp_task_wdt_deinit();
    esp_gmf_fft_test_runner_run();
}

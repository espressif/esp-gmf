/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Call this function from a test case which requires TCP/IP or
 *         LWIP functionality
 *
 * @note  This should be the first function the test case calls, as it will
 *        allocate memory on first use (and also reset the test case leak checker)
 */
void esp_gmf_app_test_case_uses_tcpip(void);

/**
 * @brief  Entry point of the test application
 *         Starts Unity test runner in a separate task and returns.
 */
void esp_gmf_app_test_main(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

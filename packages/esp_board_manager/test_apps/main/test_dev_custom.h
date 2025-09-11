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
 * @brief  Test custom device functionality
 *
 *         This function tests the custom device by:
 *         - Getting the device handle and configuration
 *         - Displaying device information (name, chip, type)
 *         - Testing custom device initialization and deinitialization
 */
void test_dev_custom(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

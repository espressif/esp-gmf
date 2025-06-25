/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Get capture version string
 *
 * @return
 *       - String representation of capture version
 */
const char *esp_capture_get_version(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

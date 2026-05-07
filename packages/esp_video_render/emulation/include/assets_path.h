/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Get absolute path to an asset file
 *
 * @param  filename     Asset filename (e.g., "DejaVuSans.ttf", "left.mjpeg")
 * @param  buffer       Output buffer for the absolute path
 * @param  buffer_size  Size of the output buffer
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int get_assets_path(const char *filename, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

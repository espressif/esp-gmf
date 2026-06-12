/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

int _extractor_read(void *buffer, uint32_t size, void *ctx);
int _extractor_seek(uint32_t position, void *ctx);
uint32_t _extractor_total_size(void *ctx);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

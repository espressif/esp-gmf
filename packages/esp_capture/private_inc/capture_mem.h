/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_oal_mem.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Memory management functions
 *
 * @note  These functions are wrappers around the GMF OAL memory management functions
 *        They provide a consistent interface for memory allocation and deallocation
 */

#define capture_malloc(size)       esp_gmf_oal_malloc(size)
#define capture_free(ptr)          esp_gmf_oal_free(ptr)
#define capture_calloc(n, size)    esp_gmf_oal_calloc(n, size)
#define capture_realloc(ptr, size) esp_gmf_oal_realloc(ptr, size)

#ifdef __cplusplus
}
#endif  /* __cplusplus */

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define ASRC_MEM_CHECK(TAG, a, name, want_sz, action) do {                                        \
    if (!(a)) {                                                                                   \
        ESP_LOGE(TAG, "Fail to allocate memory for '%s'(%d), line:%d", name, want_sz, __LINE__);  \
        action;                                                                                   \
    }                                                                                             \
} while (0)
#define ASRC_NULL_CHECK(TAG, a, name, action) do {                          \
    if (!(a)) {                                                             \
        ESP_LOGE(TAG, "Invalid parameter '%s' on %s", name, __FUNCTION__);  \
        action;                                                             \
    }                                                                       \
} while (0)
#define ASRC_RET_CHECK(a, action) do {  \
    if ((a) != 0) {                     \
        action;                         \
    }                                   \
} while (0)

/**
 * @brief  Allocate zeroed memory for ASRC (PSRAM-oriented path)
 *
 * @param[in]  num   Element count
 * @param[in]  size  Element size in bytes
 *
 * @return
 *       - Non-NULL  Pointer to zero-filled memory
 *       - NULL      Allocation failed
 */
void *asrc_calloc(size_t num, size_t size);

/**
 * @brief  Allocate zeroed internal buffer
 *
 * @param[in]  num   Element count
 * @param[in]  size  Element size in bytes
 *
 * @return
 *       - Non-NULL  Pointer to zero-filled memory
 *       - NULL      Allocation failed
 */
void *asrc_calloc_inner(size_t num, size_t size);

/**
 * @brief  Allocate zeroed buffer with explicit address alignment
 *
 * @param[in]  aligned  Required alignment in bytes
 * @param[in]  num      Element count
 * @param[in]  size     Element size in bytes
 *
 * @return
 *       - Non-NULL  Pointer to zero-filled memory
 *       - NULL      Allocation failed
 */
void *asrc_calloc_aligned(size_t aligned, size_t num, size_t size);

/**
 * @brief  Reallocate a buffer from ASRC internal alloc helpers
 *
 * @param[in]  ptr   Pointer previously returned by an ASRC alloc helper
 * @param[in]  size  New allocation size in bytes
 *
 * @return
 *       - Non-NULL  Pointer to resized memory
 *       - NULL      Reallocation failed
 */
void *asrc_realloc_inner(void *ptr, size_t size);

/**
 * @brief  Free memory allocated by ASRC alloc helpers
 *
 * @param[in]  ptr  Pointer returned by an ASRC alloc helper (NULL is ignored)
 */
void asrc_free(void *ptr);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

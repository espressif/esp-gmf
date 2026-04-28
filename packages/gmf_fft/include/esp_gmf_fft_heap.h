/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  This function allocates a zero-filled buffer with a minimum starting alignment
 *
 * @note  This API is intended for FFT twiddle and spectrum buffers that require aligned vector
 *         load/store. On ESP-IDF the block is allocated from internal aligned heap caps; on other
 *         hosts `posix_memalign` and `memset` are used. Only pointers returned by this API may be
 *         passed to @ref esp_gmf_fft_free_aligned
 *
 * @param[in]  n      Number of elements (not bytes)
 * @param[in]  elem   Size of each element, in bytes
 * @param[in]  align  Required alignment of the returned pointer, in bytes (must be supported by the platform allocator)
 *
 * @return
 *       - Non-NULL  Succeeded; `n * elem` bytes are cleared to zero
 *       - NULL      Failed (allocation error or `n * elem` overflow)
 */
void *esp_gmf_fft_calloc_aligned(size_t n, size_t elem, size_t align);

/**
 * @brief  This function frees memory previously allocated by @ref esp_gmf_fft_calloc_aligned
 *
 * @param[in]  p  Pointer returned by @ref esp_gmf_fft_calloc_aligned, or `NULL` (no operation)
 */
void esp_gmf_fft_free_aligned(void *p);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

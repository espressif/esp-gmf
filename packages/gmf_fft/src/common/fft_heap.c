/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#else
#include <stdlib.h>
#endif  /* defined(ESP_PLATFORM) */

#include "esp_gmf_fft_heap.h"

void *esp_gmf_fft_calloc_aligned(size_t n, size_t elem, size_t align)
{
    size_t nbytes = n * elem;
    if (n != 0u && nbytes / n != elem) {
        return NULL;
    }
#if defined(ESP_PLATFORM)
    void *p = heap_caps_aligned_alloc(align, nbytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (p != NULL) {
        memset(p, 0, nbytes);
    }
    return p;
#else
    void *p = NULL;
    if (posix_memalign(&p, align, nbytes) != 0) {
        return NULL;
    }
    memset(p, 0, nbytes);
    return p;
#endif  /* defined(ESP_PLATFORM) */
}

void esp_gmf_fft_free_aligned(void *p)
{
    if (p == NULL) {
        return;
    }
#if defined(ESP_PLATFORM)
    heap_caps_free(p);
#else
    free(p);
#endif  /* defined(ESP_PLATFORM) */
}

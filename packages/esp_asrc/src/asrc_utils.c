/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include <malloc.h>
#include "asrc_utils.h"
#include "esp_asrc_types.h"

#define ASRC_MALLOC_CAP_IRAM  (1 << 0)
#define ASRC_MALLOC_CAP_PSRAM (1 << 2)

void *__attribute__((weak)) media_lib_malloc(size_t size)
{
    return malloc(size);
}

void __attribute__((weak)) media_lib_free(void *buf)
{
    free(buf);
}

void *__attribute__((weak)) media_lib_calloc(size_t num, size_t size)
{
    return calloc(num, size);
}

void *__attribute__((weak)) media_lib_realloc(void *buf, size_t size)
{
    return realloc(buf, size);
}

void *__attribute__((weak)) media_lib_caps_malloc_align(size_t align, size_t size, int caps)
{
    (void)caps;
    void *mem = NULL;
    if (posix_memalign(&mem, align, size) != 0) {
        return NULL;
    }
    return mem;
}

void *asrc_calloc(size_t num, size_t size)
{
    return media_lib_calloc(num, size);
}

void *asrc_calloc_inner(size_t num, size_t size)
{
    size_t total = num * size;
    void *ptr = media_lib_caps_malloc_align(16, total, ASRC_MALLOC_CAP_IRAM);
    if (ptr != NULL) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *asrc_calloc_aligned(size_t aligned, size_t num, size_t size)
{
    size_t total = num * size;
    void *addr = media_lib_caps_malloc_align(aligned, total, ASRC_MALLOC_CAP_PSRAM);
    if (addr == NULL) {
        addr = media_lib_caps_malloc_align(aligned, total, 0);
    }
    if (addr != NULL) {
        memset(addr, 0, total);
    }
    return addr;
}

void *asrc_realloc_inner(void *ptr, size_t size)
{
    return media_lib_realloc(ptr, size);
}

void asrc_free(void *ptr)
{
    media_lib_free(ptr);
}

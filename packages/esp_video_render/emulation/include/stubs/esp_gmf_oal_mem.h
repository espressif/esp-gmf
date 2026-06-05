
/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define ESP_GMF_OAL_ALIGN_UP(num, align)  \
    ((align) <= 1 ? (num) : (((num) + ((align) - 1)) & ~((align) - 1)))

#define ESP_GMF_OAL_ALIGN_BYTES_VALID(align)  \
    ((align) <= 1 || (((align) & ((align) - 1)) == 0))

#define ESP_GMF_OAL_SPIRAM_CACHE_ALIGN  64

void *esp_gmf_oal_malloc(size_t size);
void *esp_gmf_oal_calloc(size_t n, size_t size);
void *esp_gmf_oal_realloc(void *ptr, size_t size);
void esp_gmf_oal_free(void *ptr);
void *esp_gmf_oal_malloc_align(uint8_t align, size_t size);
uint8_t esp_gmf_oal_get_spiram_cache_align(void);

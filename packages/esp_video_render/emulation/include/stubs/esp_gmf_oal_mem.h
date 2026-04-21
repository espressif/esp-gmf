
/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

void *esp_gmf_oal_malloc(size_t size);
void *esp_gmf_oal_calloc(size_t n, size_t size);
void *esp_gmf_oal_realloc(void *ptr, size_t size);
void esp_gmf_oal_free(void *ptr);
void *esp_gmf_oal_malloc_align(uint8_t align, size_t size);

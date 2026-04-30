/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "asrc_utils.h"
#include "esp_asrc_types.h"
#include "esp_heap_caps.h"
#ifdef CONFIG_SOC_ASRC_SUPPORTED
#include "asrc_adapter.h"
#endif  /* CONFIG_SOC_ASRC_SUPPORTED */

static const char *TAG = "ASRC_MEM";

esp_asrc_err_t esp_asrc_get_buffer_alignment(esp_asrc_buffer_alignment_t *buffer_alignment)
{
    ASRC_NULL_CHECK(TAG, buffer_alignment, "buffer alignment", return ESP_ASRC_ERR_INVALID_PARAMETER);
    size_t cache_align = 0;
#ifdef CONFIG_SOC_ASRC_SUPPORTED
    asrc_hw_get_buffer_alignment(MALLOC_CAP_SPIRAM, &cache_align);
#else
    cache_align = 1;
#endif  /* CONFIG_SOC_ASRC_SUPPORTED */
    buffer_alignment->inbuf_addr_align = 1;
    buffer_alignment->inbuf_size_align = 1;
    buffer_alignment->outbuf_addr_align = (uint32_t)cache_align;
    buffer_alignment->outbuf_size_align = (uint32_t)cache_align;
    return ESP_ASRC_ERR_OK;
}

void *esp_asrc_align_alloc(uint32_t size, uint32_t addr_align, uint32_t size_align, uint32_t *allocated_size)
{
    ASRC_NULL_CHECK(TAG, allocated_size, "allocated size pointer", return NULL);
    size = ((size + size_align - 1) & ~(size_align - 1));
    *allocated_size = size;
    return asrc_calloc_aligned(addr_align, 1, size);
}

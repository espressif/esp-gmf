/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2024 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include "string.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_gmf_oal_mem.h"
#include "esp_heap_caps.h"
#include "hal/efuse_hal.h"

// #define ENABLE_AUDIO_MEM_TRACE
#define MALLOC_RAM_FLAG 1


#ifdef ENABLE_AUDIO_MEM_TRACE
int __attribute__((weak)) media_lib_add_trace_mem(const char *module, void *addr, int size, uint8_t flag)
{
    return 0;
}

void __attribute__((weak)) media_lib_remove_trace_mem(void *addr)
{
}
#endif  /* ENABLE_AUDIO_MEM_TRACE */

void *esp_gmf_oal_malloc(size_t size)
{
    void *data = NULL;
#if CONFIG_SPIRAM_BOOT_INIT
    data = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    data = malloc(size);
#endif  /* CONFIG_SPIRAM_BOOT_INIT */
    // ESP_LOGI("ESP_GMF_MEM", "malloc:%p, size:%d, called:0x%08x", data, size, (intptr_t)__builtin_return_address(0) - 2);
#ifdef ENABLE_AUDIO_MEM_TRACE
    media_lib_add_trace_mem(NULL, data, size, 0);
#endif  /* ENABLE_AUDIO_MEM_TRACE */
    return data;
}

void *esp_gmf_oal_malloc_align(uint8_t align, size_t size)
{
    void *data = NULL;
#if CONFIG_SPIRAM_BOOT_INIT
    if (align <= 1) {
        data = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    } else {
        data = heap_caps_aligned_alloc(align, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
#else
    if (align <= 1) {
        data = heap_caps_malloc(size, MALLOC_CAP_DEFAULT);
    } else {
        data = heap_caps_aligned_alloc(align, size, MALLOC_CAP_DEFAULT);
    }
#endif  /* CONFIG_SPIRAM_BOOT_INIT */
    // ESP_LOGI("ESP_GMF_MEM", "malloc:%p, size:%d, called:0x%08x", data, size, (intptr_t)__builtin_return_address(0) - 2);
#ifdef ENABLE_AUDIO_MEM_TRACE
    media_lib_add_trace_mem(NULL, data, size, 0);
#endif  /* ENABLE_AUDIO_MEM_TRACE */
    return data;
}

void esp_gmf_oal_free(void *ptr)
{
#ifdef ENABLE_AUDIO_MEM_TRACE
    media_lib_remove_trace_mem(ptr);
#endif  /* ENABLE_AUDIO_MEM_TRACE */
    // ESP_LOGI("ESP_GMF_MEM", "free:%p, called:0x%08x", ptr, (intptr_t)__builtin_return_address(0) - 2);

    free(ptr);
}

void *esp_gmf_oal_calloc(size_t nmemb, size_t size)
{
    void *data = NULL;
#if CONFIG_SPIRAM_BOOT_INIT
    data = heap_caps_malloc(nmemb * size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data) {
        memset(data, 0, nmemb * size);
    }
#else
    data = calloc(nmemb, size);
#endif  /* CONFIG_SPIRAM_BOOT_INIT */
    // ESP_LOGI("ESP_GMF_MEM", "calloc:%p, size:%d, called:0x%08x", data, size, (intptr_t)__builtin_return_address(0) - 2);

#ifdef ENABLE_AUDIO_MEM_TRACE
    media_lib_add_trace_mem(NULL, data, nmemb * size, 0);
#endif  /* ENABLE_AUDIO_MEM_TRACE */
    return data;
}

void *esp_gmf_oal_realloc(void *ptr, size_t size)
{
    void *p = NULL;
#ifdef ENABLE_AUDIO_MEM_TRACE
    media_lib_remove_trace_mem(ptr);
#endif  /* ENABLE_AUDIO_MEM_TRACE */

#if CONFIG_SPIRAM_BOOT_INIT
    p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    p = heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
#endif  /* CONFIG_SPIRAM_BOOT_INIT */
    // ESP_LOGI("ESP_GMF_MEM", "realloc:%p, size:%d, called:0x%08x", p, size, (intptr_t)__builtin_return_address(0) - 2);

#ifdef ENABLE_AUDIO_MEM_TRACE
    media_lib_add_trace_mem(NULL, p, size, 0);
#endif  /* ENABLE_AUDIO_MEM_TRACE */
    return p;
}

char *esp_gmf_oal_strdup(const char *str)
{
    int size = strlen(str) + 1;
#if CONFIG_SPIRAM_BOOT_INIT
    char *copy = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    char *copy = malloc(size);
#endif  /* CONFIG_SPIRAM_BOOT_INIT */
    if (copy) {
        strcpy(copy, str);
#ifdef ENABLE_AUDIO_MEM_TRACE
        media_lib_add_trace_mem(NULL, copy, size, 0);
#endif  /* ENABLE_AUDIO_MEM_TRACE */
    }
    // ESP_LOGI("ESP_GMF_MEM", "strdup:%p, size:%d, called:0x%08x", copy, size, (intptr_t)__builtin_return_address(0) - 2);

    return copy;
}

void *esp_gmf_oal_calloc_inner(size_t n, size_t size)
{
    void *data = NULL;
#if CONFIG_SPIRAM_BOOT_INIT
    data = heap_caps_calloc_prefer(n, size, 2, MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM);
#else
    data = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif  /* CONFIG_SPIRAM_BOOT_INIT */
#ifdef ENABLE_AUDIO_MEM_TRACE
    media_lib_add_trace_mem(NULL, data, n * size, MALLOC_RAM_FLAG);
#endif  /* ENABLE_AUDIO_MEM_TRACE */
    // ESP_LOGI("ESP_GMF_MEM", "inner:%p, size:%d, called:0x%08x", data, size, (intptr_t)__builtin_return_address(0) - 2);

    return data;
}

void esp_gmf_oal_mem_print(const char *tag, int line, const char *func)
{
#ifdef CONFIG_SPIRAM_BOOT_INIT
    ESP_LOGI(tag, "Func:%s, Line:%d, MEM Total:%d Bytes, Inter:%d Bytes, Dram:%d Bytes\r\n", func, line, (int)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#else
    ESP_LOGI(tag, "Func:%s, Line:%d, MEM Total:%d Bytes\r\n", func, line, (int)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
#endif  /* CONFIG_SPIRAM_BOOT_INIT */
}

bool esp_gmf_oal_mem_spiram_stack_is_enabled(void)
{
#if defined(CONFIG_SPIRAM_BOOT_INIT) && (CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY)
    bool ret = true;
#if defined(CONFIG_IDF_TARGET_ESP32)
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 4)
    uint32_t chip_ver = efuse_hal_chip_revision();
#else
    uint8_t chip_ver = esp_efuse_get_chip_ver();
#endif  /* ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 4) */
    if (chip_ver < 3) {
        ESP_LOGW("ESP_GMF_MEM", "Can't support stack on external memory due to ESP32 chip is %d", (int)chip_ver);
        ret = false;
    }
#endif  // defined(CONFIG_IDF_TARGET_ESP32)
    return ret;
#else  // defined(CONFIG_SPIRAM_BOOT_INIT)
    return false;
#endif  /* defined(CONFIG_SPIRAM_BOOT_INIT) && (CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY) */
}
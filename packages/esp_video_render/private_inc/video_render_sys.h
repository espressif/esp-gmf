/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_oal_mem.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Allocate memory
 *
 * @param[in]  size  Size in bytes to allocate
 *
 * @return
 *       - Pointer  to allocated memory on success
 *       - NULL     on failure
 */
#define video_render_malloc(size)  esp_gmf_oal_malloc(size)

/**
 * @brief  Allocate aligned memory
 *
 * @param[in]  size   Size in bytes to allocate
 * @param[in]  align  Alignment requirement
 *
 * @return
 *       - Pointer  to allocated memory on success
 *       - NULL     on failure
 */
#define video_render_malloc_align(size, align)  esp_gmf_oal_malloc_align(align, size)

/**
 * @brief  Free allocated memory
 *
 * @param[in]  ptr  Pointer to memory to free
 */
#define video_render_free(ptr)  esp_gmf_oal_free(ptr)

/**
 * @brief  Allocate and zero-initialize memory
 *
 * @param[in]  n     Number of elements
 * @param[in]  size  Size of each element
 *
 * @return
 *       - Pointer  to allocated memory on success
 *       - NULL     on failure
 */
#define video_render_calloc(n, size)  esp_gmf_oal_calloc(n, size)

/**
 * @brief  Reallocate memory
 *
 * @param[in]  ptr   Pointer to existing memory (can be NULL)
 * @param[in]  size  New size in bytes
 *
 * @return
 *       - Pointer  to reallocated memory on success
 *       - NULL     on failure
 */
#define video_render_realloc(ptr, size)  esp_gmf_oal_realloc(ptr, size)

/**
 * @brief  Event group handle type
 */
typedef void *video_render_event_grp_handle_t;

/**
 * @brief  Maximum lock time value (infinite wait)
 */
#define VIDEO_RENDER_MAX_LOCK_TIME  0xFFFFFFFF

/**
 * @brief  Convert milliseconds to FreeRTOS ticks
 *
 * @param[in]  ms  Time in milliseconds
 *
 * @return
 */
#define VIDEO_RENDER_TIME_TO_TICKS(ms)  ((ms) == VIDEO_RENDER_MAX_LOCK_TIME ? portMAX_DELAY : (ms) / portTICK_PERIOD_MS)

/**
 * @brief  Mutex handle type
 */
typedef void *video_render_mutex_handle_t;

/**
 * @brief  Create recursive mutex
 *
 * @return
 *       - Mutex  handle on success
 *       - NULL   on failure
 */
static inline video_render_mutex_handle_t video_render_mutex_create(void)
{
    return (video_render_mutex_handle_t)xSemaphoreCreateRecursiveMutex();
}

/**
 * @brief  Lock mutex
 *
 * @param[in]  mutex    Mutex handle
 * @param[in]  timeout  Timeout in milliseconds (VIDEO_RENDER_MAX_LOCK_TIME for infinite)
 *
 * @return
 *       - pdTRUE   On success
 *       - pdFALSE  On timeout or failure
 */
#define video_render_mutex_lock(mutex, timeout)  \
    xSemaphoreTakeRecursive((SemaphoreHandle_t)mutex, VIDEO_RENDER_TIME_TO_TICKS(timeout))

/**
 * @brief  Unlock mutex
 *
 * @param[in]  mutex  Mutex handle
 *
 * @return
 *       - pdTRUE   On success
 *       - pdFALSE  On failure
 */
#define video_render_mutex_unlock(mutex)  xSemaphoreGiveRecursive((SemaphoreHandle_t)mutex)

/**
 * @brief  Destroy mutex
 *
 * @param[in]  mutex  Mutex handle
 */
#define video_render_mutex_destroy(mutex)  vSemaphoreDelete((SemaphoreHandle_t)mutex)

/**
 * @brief  Create event group
 *
 * @param[out]  event_group_ptr  Pointer to store event group handle
 */
#define video_render_event_grp_create(event_group_ptr)  (*(event_group_ptr) = (video_render_event_grp_handle_t)xEventGroupCreate())

/**
 * @brief  Set bits in event group
 *
 * @param[in]  event_group  Event group handle
 * @param[in]  bits         Bits to set
 *
 * @return
 */
#define video_render_event_grp_set_bits(event_group, bits)  do {                                          \
    if (xPortInIsrContext()) {                                                                            \
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;                                                    \
        xEventGroupSetBitsFromISR((EventGroupHandle_t)(event_group), (bits), &xHigherPriorityTaskWoken);  \
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);                                                     \
    } else {                                                                                              \
        xEventGroupSetBits((EventGroupHandle_t)(event_group), (bits));                                    \
    }                                                                                                     \
} while (0)

/**
 * @brief  Clear bits in event group
 *
 * @param[in]  event_group  Event group handle
 * @param[in]  bits         Bits to clear
 *
 * @return
 */
#define video_render_event_grp_clr_bits(event_group, bits)  xEventGroupClearBits((EventGroupHandle_t)(event_group), (bits))

/**
 * @brief  Destroy event group
 *
 * @param[in]  event_group  Event group handle
 */
#define video_render_event_grp_destroy(event_group)  vEventGroupDelete((EventGroupHandle_t)(event_group))

/**
 * @brief  Delay task execution
 *
 * @param[in]  ms  Delay time in milliseconds
 */
#define video_render_delay(ms)  vTaskDelay(VIDEO_RENDER_TIME_TO_TICKS(ms))

/**
 * @brief  Wait for bits to be set in event group
 *
 * @param[in]  event_group  Event group handle
 * @param[in]  bits         Bits to wait for
 * @param[in]  timeout      Timeout in milliseconds (VIDEO_RENDER_MAX_LOCK_TIME for infinite)
 *
 * @return
 */
#define video_render_event_grp_wait_bits(event_group, bits, timeout)  \
    (uint32_t) xEventGroupWaitBits((EventGroupHandle_t)(event_group), (bits), false, true, VIDEO_RENDER_TIME_TO_TICKS(timeout))

#ifdef __cplusplus
}
#endif  /* __cplusplus */

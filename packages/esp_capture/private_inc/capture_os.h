/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "capture_mem.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Event group handle type
 */
typedef void *capture_event_grp_handle_t;

/**
 * @brief  Maximum lock time value (infinite wait)
 */
#define CAPTURE_MAX_LOCK_TIME 0xFFFFFFFF

/**
 * @brief  Convert milliseconds to FreeRTOS ticks
 */
#define CAPTURE_TIME_TO_TICKS(ms) (ms == CAPTURE_MAX_LOCK_TIME ? portMAX_DELAY : ms / portTICK_PERIOD_MS)

/**
 * @brief  Event group management functions
 *
 * @note  These functions provide a wrapper around FreeRTOS event group functions
 *        for event synchronization and signaling
 */
#define capture_event_group_create(event_group)         *event_group = (capture_event_grp_handle_t)xEventGroupCreate()
#define capture_event_group_set_bits(event_group, bits) xEventGroupSetBits((EventGroupHandle_t)event_group, bits)
#define capture_event_group_clr_bits(event_group, bits) xEventGroupClearBits((EventGroupHandle_t)event_group, bits)

/**
 * @brief  Wait for bits (in milliseconds) to be set in an event group
 */
#define capture_event_group_wait_bits(event_group, bits, timeout) \
    (uint32_t) xEventGroupWaitBits((EventGroupHandle_t)event_group, bits, false, true, CAPTURE_TIME_TO_TICKS(timeout))

#define capture_event_group_destroy(event_group) vEventGroupDelete((EventGroupHandle_t)event_group)

/**
 * @brief  Mutex handle type
 */
typedef void *capture_mutex_handle_t;

/**
 * @brief  Mutex management functions
 *
 * @note  These functions provide a wrapper around FreeRTOS recursive mutex functions
 */
#define capture_mutex_create(mutex) *mutex = (capture_mutex_handle_t)xSemaphoreCreateRecursiveMutex()

#define capture_mutex_lock(mutex, timeout) \
    xSemaphoreTakeRecursive((SemaphoreHandle_t)mutex, CAPTURE_TIME_TO_TICKS(timeout))

#define capture_mutex_unlock(mutex) xSemaphoreGiveRecursive((SemaphoreHandle_t)mutex)

#define capture_mutex_destroy(mutex) vSemaphoreDelete((SemaphoreHandle_t)mutex)

/**
 * @brief  Sleep for specified milliseconds
 */
#define capture_sleep(ms) vTaskDelay(ms / portTICK_PERIOD_MS)

/**
 * @brief  Semaphore handle type
 */
typedef void *capture_sema_handle_t;

/**
 * @brief  Semaphore management functions
 *
 * @note  These functions provide a wrapper around FreeRTOS counting semaphore functions
 */
#define capture_sema_create(sema) *sema = (capture_sema_handle_t)xSemaphoreCreateCounting(1, 0)

#define capture_sema_lock(sema, timeout) \
    xSemaphoreTake((SemaphoreHandle_t)sema, CAPTURE_TIME_TO_TICKS(timeout));

#define capture_sema_unlock(sema) xSemaphoreGive((SemaphoreHandle_t)sema)

#define capture_sema_destroy(sema) vSemaphoreDelete((SemaphoreHandle_t)sema)

#ifdef __cplusplus
}
#endif  /* __cplusplus */

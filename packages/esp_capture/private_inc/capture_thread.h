/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_capture.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Thread handle type
 */
typedef void *capture_thread_handle_t;

/**
 * @brief  Set thread scheduler callback
 *
 * @param[in]  thread_scheduler  Thread scheduler callback function
 */
void capture_thread_set_scheduler(esp_capture_thread_scheduler_cb_t thread_scheduler);

/**
 * @brief  Get thread scheduler callback
 *
 * @return
 */
esp_capture_thread_scheduler_cb_t capture_thread_get_scheduler(void);

/**
 * @brief  Create a thread using the scheduler
 *
 * @param[out]  handle  Pointer to store the thread handle
 * @param[in]   name    Thread name
 * @param[in]   body    Thread function
 * @param[in]   arg     Thread function argument
 *
 * @return
 *       - 0       Thread created successfully
 *       - Others  Failed to create thread
 */
int capture_thread_create_from_scheduler(capture_thread_handle_t *handle, const char *name,
                                         void (*body)(void *arg), void *arg);

/**
 * @brief  Destroy a thread
 *
 * @param[in]  thread  Thread handle
 */
void capture_thread_destroy(capture_thread_handle_t thread);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

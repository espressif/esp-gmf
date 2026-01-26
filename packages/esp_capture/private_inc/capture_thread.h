/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_capture.h"
#include "capture_os.h"
#include "esp_attr.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Default scheduler for capture
 */
#define CAPTURE_DEFAULT_SCHEDULER() {                     \
    .priority = 5, .stack_size = 4096, .stack_in_ext = 1, \
}

/**
 * @brief  Thread handle type
 */
typedef void *capture_thread_handle_t;

/**
 * @brief  Capture synchronized call argument
 */
typedef struct {
    int                    ret;                 /*!< Function return value */
    int                    (*body)(void *arg);  /*!< Function body */
    void                  *arg;                 /*!< Function argument */
    capture_sema_handle_t  sema;                /*!< Lock to wait for function return */
} capture_thread_sync_run_arg_t;

/**
 * @brief  Define for running a function in another thread and wait for its return
 *
 * @note  This define specially for some API must run on RAM stack but the running task is on PSRAM
 *        Running task may need huge stack, if user not want to change the whole task stack into PSRAM
 *        Can run the special API in RAM stack, wait for finished and continue with left part
 */
#define CAPTURE_RUN_SYNC_IN_RAM(name, body_func, arg_info, ret, _stack_size)                     \
    do {                                                                                         \
        esp_capture_thread_schedule_cfg_t cur_cfg = CAPTURE_DEFAULT_SCHEDULER();                 \
        capture_thread_get_scheduler_cfg(NULL, &cur_cfg);                                        \
        if (capture_thread_is_stack_in_ram()) {                                                  \
            ret = body_func(arg_info);                                                           \
            break;                                                                               \
        }                                                                                        \
        capture_thread_sync_run_arg_t _arg = {                                                   \
            .body = body_func,                                                                   \
            .arg = arg_info,                                                                     \
        };                                                                                       \
        capture_sema_create(&_arg.sema);                                                         \
        if (_arg.sema == NULL) {                                                                 \
            ret = ESP_CAPTURE_ERR_NO_RESOURCES;                                                  \
            break;                                                                               \
        }                                                                                        \
        cur_cfg.stack_in_ext = false;                                                            \
        cur_cfg.stack_size = _stack_size;                                                        \
        capture_thread_handle_t _sync_thread = NULL;                                             \
        capture_thread_create(&_sync_thread, name, &cur_cfg, capture_thread_run_in_ram, &_arg);  \
        if (_sync_thread) {                                                                      \
            capture_sema_lock(_arg.sema, CAPTURE_MAX_LOCK_TIME);                                 \
            ret = _arg.ret;                                                                      \
        } else {                                                                                 \
            ret = ESP_CAPTURE_ERR_NO_RESOURCES;                                                  \
        }                                                                                        \
        capture_sema_destroy(_arg.sema);                                                         \
    } while (0);

/**
 * @brief  Helper function for synchronized function call
 *
 * @param[in]  arg  Pointer to `capture_thread_sync_run_arg_t`
 */
IRAM_ATTR void capture_thread_run_in_ram(void *arg);

/**
 * @brief  Check whether thread stack is in ram
 *
 * @return
 *       - true   Thread stack is in internal ram
 *       - false  Thread stack is in PSRAM
 */
bool capture_thread_is_stack_in_ram(void);

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
 * @brief  Get scheduler configuration for created thread
 *
 * @param[in]   name  Thread name
 * @param[out]  cfg   Thread scheduler configuration
 */
void capture_thread_get_scheduler_cfg(const char *name, esp_capture_thread_schedule_cfg_t *cfg);

/**
 * @brief  Create a thread using schedule configuration
 *
 * @param[out]  handle  Pointer to store the thread handle
 * @param[in]   name    Thread name
 * @param[in]   cfg     Scheduler configuration
 * @param[in]   body    Thread function
 * @param[in]   arg     Thread function argument
 *
 * @return
 *       - 0       Thread created successfully
 *       - Others  Failed to create thread
 */
int capture_thread_create(capture_thread_handle_t *handle, const char *name, esp_capture_thread_schedule_cfg_t *cfg,
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

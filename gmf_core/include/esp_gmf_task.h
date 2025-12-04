/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "stdbool.h"
#include "esp_gmf_err.h"
#include "esp_gmf_event.h"
#include "esp_gmf_job.h"
#include "esp_gmf_obj.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define ESP_GMF_MAX_DELAY (0xFFFFFFFFUL)

/**
 * @brief  GMF Task Handle
 */
typedef void *esp_gmf_task_handle_t;

/**
 * @brief  GMF task configuration
 *
 *         Configuration structure for GMF tasks, specifying parameters such as stack size,
 *         priority, CPU core affinity, and whether the stack is allocated in external memory.
 */
typedef struct esp_gmf_task_config {
    int         stack;             /*!< Size of the task stack */
    int         prio;              /*!< Priority of the task */
    uint32_t    core         : 4;  /*!< CPU core affinity for the task */
    uint32_t    stack_in_ext : 4;  /*!< Flag indicating if the stack is in external memory */
} esp_gmf_task_config_t;

/**
 * @brief  GMF Task configuration
 *
 *         Configuration structure for GMF tasks, specifying parameters such as thread configuration,
 *         task name, user context, and callback function.
 */
typedef struct {
    esp_gmf_task_config_t  thread;  /*!< Configuration settings for the task thread */
    const char            *name;    /*!< Name of the task */
    void                  *ctx;     /*!< User context */
    esp_gmf_event_cb       cb;      /*!< Callback function for task events */
} esp_gmf_task_cfg_t;

#define DEFAULT_ESP_GMF_STACK_SIZE (4 * 1024)
#define DEFAULT_ESP_GMF_TASK_PRIO  (5)
#define DEFAULT_ESP_GMF_TASK_CORE  (0)

#define DEFAULT_ESP_GMF_TASK_CONFIG() {       \
    .thread = {                               \
        .stack = DEFAULT_ESP_GMF_STACK_SIZE,  \
        .prio = DEFAULT_ESP_GMF_TASK_PRIO,    \
        .core = DEFAULT_ESP_GMF_TASK_CORE,    \
        .stack_in_ext = false,                \
    },                                        \
    .name = NULL,                             \
    .ctx = NULL,                              \
    .cb = NULL,                               \
}

/**
 * @brief  Initialize a GMF task
 *
 * @param[in]   config  Configuration for the GMF task
 * @param[out]  tsk_hd  Pointer to store the GMF task handle after initialization
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  If the configuration or handle is invalid
 *       - ESP_GMF_ERR_MEMORY_LACK  If there is insufficient memory to perform the initialization
 *       - Others                   Indicating failure
 */
esp_gmf_err_t esp_gmf_task_init(esp_gmf_task_cfg_t *config, esp_gmf_task_handle_t *tsk_hd);

/**
 * @brief  Deinitialize a GMF task, freeing associated resources
 *
 * @param[in]  handle  GMF task handle to deinitialize
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  If the configuration or handle is invalid
 */
esp_gmf_err_t esp_gmf_task_deinit(esp_gmf_task_handle_t handle);

/**
 * @brief  Register a ready job to the specific GMF task
 *
 * @param[in]  handle  GMF task handle
 * @param[in]  label   Label for the job
 * @param[in]  job     Job function to register
 * @param[in]  times   Job execution times configuration
 * @param[in]  ctx     Context to be passed to the job function
 * @param[in]  done    Flag indicating whether the job is done
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Indicating the handle is invalid
 *       - ESP_GMF_ERR_MEMORY_LACK  Insufficient memory to perform the registration
 */
esp_gmf_err_t esp_gmf_task_register_ready_job(esp_gmf_task_handle_t handle, const char *label, esp_gmf_job_func job, esp_gmf_job_times_t times, void *ctx, bool done);

/**
 * @brief  Set the event callback function for a GMF task
 *
 * @param[in]  handle  GMF task handle
 * @param[in]  cb      Event callback function
 * @param[in]  ctx     Context to be passed to the callback function
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Indicating the handle is invalid
 */
esp_gmf_err_t esp_gmf_task_set_event_func(esp_gmf_task_handle_t handle, esp_gmf_event_cb cb, void *ctx);

/**
 * @brief  Run the specific GMF task
 *         This function may block for either the DEFAULT_TASK_OPT_MAX_TIME_MS or the time set by the set esp_gmf_task_set_timeout
 *
 * @param[in]  handle  GMF task handle
 *
 * @return
 *       - ESP_GMF_ERR_OK             On success
 *       - ESP_GMF_ERR_INVALID_ARG    Indicating the handle is invalid
 *       - ESP_GMF_ERR_NOT_SUPPORT    Indicating the state of task is ESP_GMF_EVENT_STATE_PAUSED or ESP_GMF_EVENT_STATE_RUNNING
 *       - ESP_GMF_ERR_INVALID_STATE  The task is not running
 *       - ESP_GMF_ERR_TIMEOUT        Indicating that the synchronization operation has timed out
 */
esp_gmf_err_t esp_gmf_task_run(esp_gmf_task_handle_t handle);

/**
 * @brief  Stop a running or paused GMF task
 *         This function may block for either the DEFAULT_TASK_OPT_MAX_TIME_MS or the time set by the set esp_gmf_task_set_timeout
 *
 *
 * @param[in]  handle  GMF task handle
 *
 * @return
 *       - ESP_GMF_ERR_OK             On success or the task already stopped
 *       - ESP_GMF_ERR_INVALID_ARG    Indicating the handle is invalid
 *       - ESP_GMF_ERR_NOT_SUPPORT    The state of task is ESP_GMF_EVENT_STATE_NONE
 *       - ESP_GMF_ERR_INVALID_STATE  The task is not running
 *       - ESP_GMF_ERR_TIMEOUT        Indicating that the synchronization operation has timed out
 */
esp_gmf_err_t esp_gmf_task_stop(esp_gmf_task_handle_t handle);

/**
 * @brief  Pause a running GMF task
 *         This function may block for either the DEFAULT_TASK_OPT_MAX_TIME_MS or the time set by the set esp_gmf_task_set_timeout
 *
 * @param[in]  handle  GMF task handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success or the task already paused
 *       - ESP_GMF_ERR_INVALID_ARG  Indicating the handle is invalid
 *       - ESP_GMF_ERR_NOT_SUPPORT  The state of task is not ESP_GMF_EVENT_STATE_RUNNING
 *       - ESP_GMF_ERR_TIMEOUT      Indicating that the synchronization operation has timed out
 */
esp_gmf_err_t esp_gmf_task_pause(esp_gmf_task_handle_t handle);

/**
 * @brief  Resume a paused GMF task only
 *         This function may block for either the DEFAULT_TASK_OPT_MAX_TIME_MS or the time set by the set esp_gmf_task_set_timeout
 *
 * @param[in]  handle  GMF task handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Indicating the handle is invalid
 *       - ESP_GMF_ERR_NOT_SUPPORT  The state of task is not ESP_GMF_EVENT_STATE_PAUSED
 *       - ESP_GMF_ERR_TIMEOUT      Indicating that the synchronization operation has timed out
 */
esp_gmf_err_t esp_gmf_task_resume(esp_gmf_task_handle_t handle);

/**
 * @brief  Reset a GMF task to its initial state
 *
 * @param[in]  handle  GMF task handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Indicating the handle is invalid
 */
esp_gmf_err_t esp_gmf_task_reset(esp_gmf_task_handle_t handle);

/**
 * @brief  Set the synchronization timeout for run, stop, pause, and resume operations
 *
 * @param[in]  handle   GMF task handle
 * @param[in]  wait_ms  Timeout duration in milliseconds
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Indicating the handle is invalid
 */
esp_gmf_err_t esp_gmf_task_set_timeout(esp_gmf_task_handle_t handle, int wait_ms);

/**
 * @brief  Get the state of the specific task
 *
 * @param[in]   handle  GMF task handle
 * @param[out]  state   Pointer to store the GMF event state
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Indicating the handle is invalid
 */
esp_gmf_err_t esp_gmf_task_get_state(esp_gmf_task_handle_t handle, esp_gmf_event_state_t *state);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

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
 * @brief  GMF task strategy type: abort or finish
 */
#define GMF_TASK_STRATEGY_TYPE_FINISH (0)  /*!< Strategy type for finish flow, when all jobs are finished */
#define GMF_TASK_STRATEGY_TYPE_ABORT  (1)  /*!< Strategy type for abort flow, when job returns ESP_GMF_JOB_ERR_ABORT */

/**
 * @brief  GMF task strategy action when the task is triggered by the strategy type
 *
 *         For the GMF_TASK_STRATEGY_TYPE_FINISH and GMF_TASK_STRATEGY_TYPE_ABORT, the default action is GMF_TASK_STRATEGY_ACTION_DEFAULT
 */
#define GMF_TASK_STRATEGY_ACTION_DEFAULT (0)  /*!< Going to the default flow (done to finish or abort to stop) */
#define GMF_TASK_STRATEGY_ACTION_RESET   (1)  /*!< Going to the reset flow, loading the elements' reset jobs */
#define GMF_TASK_STRATEGY_ACTION_STOP    (2)  /*!< Going to the stop flow, loading the elements' close jobs */

/**
 * @brief  Strategy function for GMF task
 *         Set the strategy function for the task, can be used to inject specific logic to let the task have special behavior
 *         e.g. the strategy function will be called when the task is triggered by the strategy type and
                the action will be returned to the task to decide the next action
 *
 * @param[in]   handle         GMF task handle
 * @param[in]   strategy_type  Strategy type
 * @param[in]   ctx            Context
 * @param[out]  out_action     Output strategy action
 */
typedef esp_gmf_err_t (*esp_gmf_task_strategy_func)(esp_gmf_task_handle_t handle, uint8_t strategy_type, void *ctx, uint8_t *out_action);

/**
 * @brief  GMF task configuration
 *
 *         Configuration structure for GMF tasks, specifying parameters such as stack size,
 *         priority, CPU core affinity, and whether the stack is allocated in external memory.
 */
typedef struct esp_gmf_task_config {
    int       stack;             /*!< Size of the task stack */
    int       prio;              /*!< Priority of the task */
    uint32_t  core         : 4;  /*!< CPU core affinity for the task */
    uint32_t  stack_in_ext : 4;  /*!< Flag indicating if the stack is in external memory */
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
 *         This function allows dynamically registering a job to the task and the job will be executed in the order of registration
 *
 * @note   This function can only be called when the task is not running, and it's not thread safe
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
 * @brief  Insert a job at the head of the job list in a GMF task
 *         This function allows dynamically inserting a job at the head of the job list and the previous head job will be
 *         moved to the second position
 *
 * @note   This function can only be called when the task is not running, and it's not thread safe
 *
 * @param[in]  handle  GMF task handle
 * @param[in]  label   Label for the job
 * @param[in]  job     Job function to insert
 * @param[in]  times   Job execution times configuration
 * @param[in]  ctx     Context to be passed to the job function
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Indicating the handle is invalid
 *       - ESP_GMF_ERR_MEMORY_LACK  Insufficient memory to perform the insertion
 */
esp_gmf_err_t esp_gmf_task_insert_head_job(esp_gmf_task_handle_t handle, const char *label, esp_gmf_job_func job, esp_gmf_job_times_t times, void *ctx);

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
 * @brief  Set the strategy function for a GMF task, when the task is triggered by the strategy type, the strategy function will be called
 *         Currently, it supports the strategy type of GMF_TASK_STRATEGY_TYPE_ABORT and GMF_TASK_STRATEGY_TYPE_FINISH
 *         and the action of GMF_TASK_STRATEGY_ACTION_DEFAULT, GMF_TASK_STRATEGY_ACTION_RESET, and GMF_TASK_STRATEGY_ACTION_STOP is supported

 * @note   In strategy function, do not call the task API or pipeline API, otherwise it will cause a timeout error.
 *         If you want to waiting some special outside event then return to the task, you can use the user event such as semaphore to block the strategy function.
 *
 * @param[in]  handle  GMF task handle
 * @param[in]  func    Strategy function to set
 * @param[in]  ctx     Context to be passed to the strategy function
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Indicating the handle is invalid
 */
esp_gmf_err_t esp_gmf_task_set_strategy_func(esp_gmf_task_handle_t handle, esp_gmf_task_strategy_func func, void *ctx);

/**
 * @brief  Set pause on start for the task. If set, the task will behavior to pause status after call `esp_gmf_task_run`
 *
 * @param[in]  handle  GMF task handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Indicating the handle is invalid
 */
esp_gmf_err_t esp_gmf_task_set_pause_on_start(esp_gmf_task_handle_t handle);

/**
 * @brief  Run the specific GMF task
 *
 * @note   The function returns once the worker task receives the run command
 *         If internal IO blocks (e.g., due to poor network conditions),
 *         the user may call `esp_gmf_task_stop()` to abort the ongoing jobs
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
 *
 *         Guarantees all task jobs have exited before returning, ensuring resources are in a stable state
 *
 *         Stop is executed in two stages:
 *         1. Wait up to the configured timeout
 *         2. If timeout occurs, print a warning and continue waiting until all jobs exit
 *
 * @note   This API acts as the lifecycle synchronization barrier
 *         Operations that timed out in other APIs are guaranteed to be fully resolved when this function returns
 *
 * @param[in]  handle  GMF task handle
 *
 * @return
 *       - ESP_GMF_ERR_OK             On success or the task already stopped
 *       - ESP_GMF_ERR_INVALID_ARG    Indicating the handle is invalid
 *       - ESP_GMF_ERR_NOT_SUPPORT    The state of task is ESP_GMF_EVENT_STATE_NONE
 *       - ESP_GMF_ERR_INVALID_STATE  The task is not running
 *       - ESP_GMF_ERR_TIMEOUT        The synchronization operation has timed out, but the stop still takes effect
 */
esp_gmf_err_t esp_gmf_task_stop(esp_gmf_task_handle_t handle);

/**
 * @brief  Pause a running GMF task
 *         This function may block for either the DEFAULT_TASK_OPT_MAX_TIME_MS or the time set by esp_gmf_task_set_timeout
 *         This function is used to pause a GMF task during its execution lifecycle.
 *         You can call this function after the task has been started with `esp_gmf_task_run`.
 *         The pause can only happen while the task is actively running.
 *         After all task elements (jobs) are completed or the task is stopped, pausing is no longer possible.
 *
 * @note   - If ESP_GMF_ERR_TIMEOUT is returned, the pause operation is still effective. This just means the function
             did not finish waiting for the pause within the timeout, but the pause will still take effect in the background
           - Calling `esp_gmf_task_run` or `esp_gmf_task_resume` will automatically clear the pause bit and restore the task to normal running state
             This makes it easy and intuitive—there's no need to manually clear the pause status
           - Supports the block time is zero, it will block until the pause operation is completed
 *
 * @param[in]  handle  GMF task handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success or the task already paused
 *       - ESP_GMF_ERR_INVALID_ARG  Indicating the handle is invalid
 *       - ESP_GMF_ERR_NOT_SUPPORT  The state of task is not ESP_GMF_EVENT_STATE_RUNNING
 *       - ESP_GMF_ERR_TIMEOUT      The synchronization operation has timed out, but the pause still takes effect
 */
esp_gmf_err_t esp_gmf_task_pause(esp_gmf_task_handle_t handle);

/**
 * @brief  Resume a paused GMF task only
 *         This function may block for either the DEFAULT_TASK_OPT_MAX_TIME_MS or the time set by esp_gmf_task_set_timeout
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

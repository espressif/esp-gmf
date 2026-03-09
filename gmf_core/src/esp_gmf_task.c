/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sys/queue.h"

#include "esp_gmf_oal_mutex.h"
#include "esp_gmf_oal_thread.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_node.h"
#include "esp_gmf_task.h"
#include "esp_log.h"

static const char *TAG = "ESP_GMF_TASK";

#define DEFAULT_TASK_OPT_MAX_TIME_MS (2000 / portTICK_PERIOD_MS)

#define GMF_TASK_RUN_BIT    (1 << 0)
#define GMF_TASK_PAUSE_BIT  (1 << 1)
#define GMF_TASK_RESUME_BIT (1 << 2)
#define GMF_TASK_STOP_BIT   (1 << 3)
#define GMF_TASK_EXIT_BIT   (1 << 4)

#define GMF_TASK_WAIT_FOR_STATE_BITS(event_group, bits, timeout) \
    (bits == (bits & xEventGroupWaitBits((EventGroupHandle_t)event_group, bits, true, true, timeout)))

#define GMF_TASK_WAIT_FOR_MULTI_STATE_BITS(event_group, bits, timeout) \
    (0 != ((bits) & xEventGroupWaitBits((EventGroupHandle_t)event_group, (bits), false, false, timeout)))

#define GMF_TASK_SET_STATE_BITS(event_group, bits) \
    xEventGroupSetBits((EventGroupHandle_t)event_group, bits);

#define GMF_TASK_CLR_STATE_BITS(event_group, bits) \
    xEventGroupClearBits((EventGroupHandle_t)event_group, bits);

#define TASK_HAS_ACTION(flag, type)  (atomic_load(&flag) & (1 << type))
#define TASK_SET_ACTION(flag, type)  (atomic_fetch_or(&flag, (1 << type)))
#define TASK_CLR_ACTION(flag, type)  (atomic_fetch_and(&flag, ~(1 << type)))

typedef enum {
    GMF_TASK_ACTION_TYPE_CREATED        = 0,
    GMF_TASK_ACTION_TYPE_RUN            = 1,
    GMF_TASK_ACTION_TYPE_RUNNING        = 2,
    GMF_TASK_ACTION_TYPE_PAUSE          = 3,
    GMF_TASK_ACTION_TYPE_STOP           = 4,
    GMF_TASK_ACTION_TYPE_DESTROY        = 5,
    GMF_TASK_ACTION_TYPE_PAUSE_ON_START = 6,
} gmf_task_action_type_t;

/**
 * @brief  GMF task structure
 *
 *         Represents a GMF task, including its properties, configuration, and internal state.
 */
typedef struct _esp_gmf_task {
    struct esp_gmf_obj_         base;            /*!< Base object for GMF tasks */
    esp_gmf_job_t              *working;         /*!< Currently executing job in the task */
    esp_gmf_job_stack_t        *start_stack;     /*!< Stack for the start job */

    /* Properties */
    esp_gmf_event_cb            event_func;      /*!< Callback function for task events */
    esp_gmf_event_state_t       state;           /*!< Current state of the task */

    /* Protect */
    esp_gmf_task_config_t       thread;          /*!< Configuration settings for the task */
    void                       *ctx;             /*!< Context associated with the task */
    esp_gmf_task_strategy_func  _strategy_func;  /*!< Strategy function for task */
    void                       *_strategy_ctx;   /*!< Context for strategy function */

    /* Private */
    void                       *oal_thread;      /*!< Handle to the thread */
    void                       *lock;            /*!< Mutex lock for task synchronization */
    void                       *event_group;     /*!< Event group for wait events */
    void                       *block_sem;       /*!< Semaphore for blocking tasks */
    void                       *wait_sem;        /*!< Semaphore for task waiting */
    int                         api_sync_time;   /*!< Timeout for synchronization */
    atomic_ushort               _actions;        /*!< Task actions */
} esp_gmf_task_t;

static inline esp_gmf_err_t __esp_gmf_event_state_notify(esp_gmf_task_handle_t handle, esp_gmf_event_type_t type, esp_gmf_event_state_t st)
{
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    esp_gmf_event_pkt_t evt = {
        .from = tsk,
        .type = type,
        .sub = st,
        .payload = NULL,
        .payload_size = 0,
    };
    if (tsk->event_func == NULL) {
        return ESP_GMF_ERR_OK;
    }
    return tsk->event_func(&evt, tsk->ctx);
}

static inline esp_gmf_err_t __esp_gmf_task_event_state_change_and_notify(esp_gmf_task_handle_t handle, esp_gmf_event_state_t new_st)
{
    int ret = ESP_GMF_ERR_OK;
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    if (tsk->state != new_st) {
        // Notification first then change the state to keep the previous state in callback function
        ret = __esp_gmf_event_state_notify(tsk, ESP_GMF_EVT_TYPE_CHANGE_STATE, new_st);
        if (ret == ESP_GMF_ERR_OK) {
            tsk->state = new_st;
        }
    }
    return ret;
}

static inline esp_gmf_err_t __esp_gmf_task_load_job(esp_gmf_task_handle_t handle, int action)
{
    int ret = ESP_GMF_ERR_OK;
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    ret = __esp_gmf_event_state_notify(tsk, ESP_GMF_EVT_TYPE_LOADING_JOB, action);
    return ret;
}

static inline int __esp_gmf_task_acquire_signal(esp_gmf_task_handle_t handle, int ticks)
{
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    if (xSemaphoreTake(tsk->wait_sem, ticks) != pdPASS) {
        return ESP_GMF_ERR_FAIL;
    }
    return ESP_GMF_ERR_OK;
}

static inline int __esp_gmf_task_release_signal(esp_gmf_task_handle_t handle, int ticks)
{
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    if (xSemaphoreGive(tsk->wait_sem) != pdTRUE) {
        return ESP_GMF_ERR_FAIL;
    }
    return ESP_GMF_ERR_OK;
}

static int get_jobs_num(esp_gmf_job_t *job)
{
    int k = 1;
    esp_gmf_job_t *tmp = job;
    while (tmp && (tmp = tmp->next)) {
        k++;
    }
    return k;
}

static inline void __esp_gmf_task_free(esp_gmf_task_handle_t handle)
{
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    if (tsk->lock) {
        esp_gmf_oal_mutex_destroy(tsk->lock);
    }
    if (tsk->event_group) {
        vEventGroupDelete((EventGroupHandle_t)tsk->event_group);
    }
    if (tsk->block_sem) {
        vSemaphoreDelete(tsk->block_sem);
    }
    if (tsk->wait_sem) {
        vSemaphoreDelete(tsk->wait_sem);
    }
    if (tsk->start_stack) {
        esp_gmf_job_stack_destroy(tsk->start_stack);
    }
    esp_gmf_obj_set_tag((esp_gmf_obj_handle_t)tsk, NULL);
    esp_gmf_oal_free(tsk);
}

static int esp_gmf_job_item_free(void *ptr)
{
    esp_gmf_job_t *job = (esp_gmf_job_t *)ptr;
    esp_gmf_oal_free((void *)job->label);
    esp_gmf_oal_free(job);
    return ESP_GMF_ERR_OK;
}

static inline void __esp_gmf_task_delete_jobs(esp_gmf_task_handle_t handle)
{
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    esp_gmf_node_clear((esp_gmf_node_t **)&tsk->working, esp_gmf_job_item_free);
}

static inline void __esp_gmf_task_del_job_at(esp_gmf_task_t *tsk, esp_gmf_job_t *job)
{
    if (job->times == ESP_GMF_JOB_TIMES_ONCE) {
        // For ONCE jobs, delete immediately
        esp_gmf_node_del_at((esp_gmf_node_t **)&tsk->working, (esp_gmf_node_t *)job);
        esp_gmf_job_item_free(job);
        return;
    }
    // For INFINITE jobs, only mark as deleted
    // The actual deletion will happen in __esp_gmf_task_delete_jobs
    job->is_deleted = 1;
    ESP_LOGD(TAG, "Mark infinite job as deleted [wk:%p, ctx:%p, label:%s]", job, job->ctx, job->label);
}

static inline void __esp_gmf_task_clear_deleted_flag(esp_gmf_task_t *tsk)
{
    // Clear deleted flag for all jobs in the work list
    esp_gmf_job_t *job = tsk->working;
    while (job) {
        job->is_deleted = 0;
        job = job->next;
    }
}

static inline void __esp_gmf_task_handle_reset_action(esp_gmf_task_t *tsk)
{
    __esp_gmf_task_clear_deleted_flag(tsk);
    // Load reset jobs (this will insert them at the head of the list)
    __esp_gmf_task_load_job(tsk, GMF_TASK_STRATEGY_ACTION_RESET);
    // Find the first infinite job and push it to the start stack
    esp_gmf_job_t *first_infinite_job = tsk->working;
    while (first_infinite_job) {
        if (first_infinite_job->times == ESP_GMF_JOB_TIMES_INFINITE) {
            esp_gmf_job_stack_push(tsk->start_stack, (uint32_t)first_infinite_job);
            break;
        }
        first_infinite_job = first_infinite_job->next;
    }
}

static inline esp_gmf_job_t *__esp_gmf_get_next_job(esp_gmf_task_t *tsk, esp_gmf_job_t *worker)
{
    esp_gmf_job_t *next_job = worker->next;
    if (worker->times == ESP_GMF_JOB_TIMES_ONCE) {
        ESP_LOGI(TAG, "One times job is complete, del[wk:%p, ctx:%p, label:%s]", worker, worker->ctx, worker->label);
        esp_gmf_node_del_at((esp_gmf_node_t **)&tsk->working, (esp_gmf_node_t *)worker);
        esp_gmf_job_item_free(worker);
    }

    // Skip marked deleted jobs in the linked list
    while (next_job && next_job->is_deleted) {
        ESP_LOGD(TAG, "Skip marked deleted job [wk:%p, ctx:%p, label:%s]", next_job, next_job->ctx, next_job->label);
        next_job = next_job->next;
    }

    if (next_job) {
        return next_job;
    }
    bool is_empty = false;
    esp_gmf_job_stack_is_empty(tsk->start_stack, &is_empty);
    if (is_empty == false) {
        esp_gmf_job_stack_pop(tsk->start_stack, (uint32_t *)&next_job);
    }
    return next_job;
}

static inline int __gmf_task_handle_user_action(esp_gmf_task_t *tsk, esp_gmf_job_t *worker)
{
    if (TASK_HAS_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_STOP) && (tsk->state != ESP_GMF_EVENT_STATE_ERROR)) {
        ESP_LOGV(TAG, "Stop task, [%s-%p, wk:%p, job:%p-%s]", OBJ_GET_TAG((esp_gmf_obj_handle_t)(tsk)), tsk, worker,
                 worker ? worker->ctx : NULL, worker ? worker->label : "NULL");
        __esp_gmf_task_delete_jobs(tsk);
        return GMF_TASK_STRATEGY_ACTION_STOP;
    }
    if (TASK_HAS_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_PAUSE)) {
        ESP_LOGI(TAG, "Pause task, [%s-%p, wk:%p, job:%p-%s],st:%s", OBJ_GET_TAG((esp_gmf_obj_handle_t)(tsk)),
                 tsk, worker, worker ? worker->ctx : NULL, worker ? worker->label : "NULL", esp_gmf_event_get_state_str(tsk->state));
        __esp_gmf_task_event_state_change_and_notify(tsk, ESP_GMF_EVENT_STATE_PAUSED);
        GMF_TASK_SET_STATE_BITS(tsk->event_group, GMF_TASK_PAUSE_BIT);
        TASK_CLR_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_PAUSE);
        __esp_gmf_task_acquire_signal(tsk, portMAX_DELAY);
        ESP_LOGI(TAG, "Resume task, [%s-%p, wk:%p, job:%p-%s]", OBJ_GET_TAG((esp_gmf_obj_handle_t)(tsk)), tsk, worker,
                 worker ? worker->ctx : NULL, worker ? worker->label : "NULL");
        __esp_gmf_task_event_state_change_and_notify(tsk, ESP_GMF_EVENT_STATE_RUNNING);
        GMF_TASK_SET_STATE_BITS(tsk->event_group, GMF_TASK_RESUME_BIT);
    }
    return GMF_TASK_STRATEGY_ACTION_DEFAULT;
}

static inline uint8_t __gmf_task_handle_strategy_func(esp_gmf_task_t *tsk, uint8_t type)
{
    uint8_t ret_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
    if (tsk->_strategy_func) {
        tsk->_strategy_func(tsk, type, tsk->_strategy_ctx, &ret_action);
    }
    return ret_action;
}

static inline int __process_func(esp_gmf_task_handle_t handle, void *para)
{
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    esp_gmf_job_t *worker = tsk->working;
    if ((worker == NULL) || (worker->func == NULL)) {
        ESP_LOGE(TAG, "Jobs list are invalid[%p, %p]", tsk, worker);
        return ESP_GMF_ERR_INVALID_ARG;
    }
    int result = ESP_GMF_ERR_OK;
    GMF_TASK_CLR_STATE_BITS(tsk->event_group, GMF_TASK_STOP_BIT);
    esp_gmf_event_state_t quit_state = ESP_GMF_EVENT_STATE_STOPPED;
    while (worker && worker->func) {
        if (TASK_HAS_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_PAUSE_ON_START)) {
            ESP_LOGI(TAG, "Pause on start, pause task, [%s-%p, wk:%p, job:%p-%s],st:%s", OBJ_GET_TAG((esp_gmf_obj_handle_t)(tsk)),
                     tsk, worker, worker ? worker->ctx : NULL, worker ? worker->label : "NULL", esp_gmf_event_get_state_str(tsk->state));
            __esp_gmf_task_event_state_change_and_notify(tsk, ESP_GMF_EVENT_STATE_PAUSED);
            TASK_CLR_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_PAUSE_ON_START);
            __esp_gmf_task_acquire_signal(tsk, portMAX_DELAY);
            ESP_LOGI(TAG, "Pause on start, resume task, [%s-%p, wk:%p, job:%p-%s]", OBJ_GET_TAG((esp_gmf_obj_handle_t)(tsk)), tsk, worker,
                     worker ? worker->ctx : NULL, worker ? worker->label : "NULL");
            __esp_gmf_task_event_state_change_and_notify(tsk, ESP_GMF_EVENT_STATE_RUNNING);
            GMF_TASK_SET_STATE_BITS(tsk->event_group, GMF_TASK_RESUME_BIT);
        }
        ESP_LOGV(TAG, "Running, job:%p, ctx:%p, working:%p", worker->func, worker->ctx, tsk->working);
        worker->ret = worker->func(worker->ctx, NULL);
        ESP_LOGV(TAG, "Job ret:%d, [tsk:%s-%p:%p-%p-%s]", worker->ret, OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk, worker, worker->ctx, worker->label);
        if (worker->ret == ESP_GMF_JOB_ERR_CONTINUE) {
            if (__gmf_task_handle_user_action(tsk, worker) == GMF_TASK_STRATEGY_ACTION_STOP) {
                // STOP bit is set, exit the loop, enter into terminate flow
                quit_state = ESP_GMF_EVENT_STATE_STOPPED;
                break;
            }
            if (worker->times == ESP_GMF_JOB_TIMES_ONCE) {
                ESP_LOGW(TAG, "Continue once job easy to infinite loop, [wk:%p, ctx:%p, label:%s]", worker, worker->ctx, worker->label);
            }
            // Restart the first job
            worker = tsk->working;
            continue;
        } else if (worker->ret == ESP_GMF_JOB_ERR_TRUNCATE) {
            if (worker->times == ESP_GMF_JOB_TIMES_ONCE) {
                // Once job and truncated is conflicted, enter into error state when detected
                ESP_LOGE(TAG, "Once job not support truncated");
                __esp_gmf_task_delete_jobs(tsk);
                quit_state = ESP_GMF_EVENT_STATE_ERROR;
                break;
            } else {
                ESP_LOGD(TAG, "Job truncated [tsk:%s-%p:%p-%p-%s], st:%s", OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk, worker, worker->ctx, worker->label,
                         esp_gmf_event_get_state_str(tsk->state));
                esp_gmf_job_stack_push(tsk->start_stack, (uint32_t)worker);
            }
        } else if (worker->ret == ESP_GMF_JOB_ERR_DONE) {
            ESP_LOGI(TAG, "Job is done, [tsk:%s-%p, wk:%p, job:%p-%s]", OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk, worker, worker->ctx, worker->label);
            esp_gmf_job_t *next_worker = worker->next;
            __esp_gmf_task_del_job_at(tsk, worker);
            esp_gmf_job_stack_remove(tsk->start_stack, (uint32_t)worker);
            worker = next_worker;
            if (worker == NULL) {
                uint8_t action = __gmf_task_handle_strategy_func(tsk, GMF_TASK_STRATEGY_TYPE_FINISH);
                ESP_LOGI(TAG, "Finish, strategy action: %d, [tsk:%p-%s]", action, tsk, OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk));
                if (action == GMF_TASK_STRATEGY_ACTION_RESET) {
                    __esp_gmf_task_handle_reset_action(tsk);
                    worker = tsk->working;
                    continue;
                } else {
                    //  Otherwise, it's finished, exit the loop, enter into terminate flow
                    quit_state = ESP_GMF_EVENT_STATE_FINISHED;
                    break;
                }
            }
            if (__gmf_task_handle_user_action(tsk, worker) == GMF_TASK_STRATEGY_ACTION_STOP) {
                // STOP bit is set, exit the loop, enter into terminate flow
                quit_state = ESP_GMF_EVENT_STATE_STOPPED;
                break;
            }
            continue;
        } else if (worker->ret == ESP_GMF_JOB_ERR_FAIL) {
            ESP_LOGE(TAG, "Job failed[tsk:%s-%p:%p-%p-%s], ret:%d, st:%s", OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk, worker, worker->ctx, worker->label, worker->ret,
                     esp_gmf_event_get_state_str(tsk->state));
            // If the task state is not stopped, enter into terminate flow
            if (tsk->state != ESP_GMF_EVENT_STATE_STOPPED) {
                __esp_gmf_task_delete_jobs(tsk);
                quit_state = ESP_GMF_EVENT_STATE_ERROR;
                break;
            }
        }
        if (worker->ret == ESP_GMF_JOB_ERR_ABORT) {
            uint8_t action = __gmf_task_handle_strategy_func(tsk, GMF_TASK_STRATEGY_TYPE_ABORT);
            ESP_LOGI(TAG, "Abort, strategy action: %d, [tsk:%p-%s]", action, tsk, OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk));
            if (action == GMF_TASK_STRATEGY_ACTION_RESET) {
                esp_gmf_job_stack_clear(tsk->start_stack);
                __esp_gmf_task_handle_reset_action(tsk);
                worker = tsk->working;
                continue;
            } else {
                //  Otherwise, it's stopped, exit the loop, enter into terminate flow
                quit_state = ESP_GMF_EVENT_STATE_STOPPED;
                break;
            }
        }
        if (__gmf_task_handle_user_action(tsk, worker) == GMF_TASK_STRATEGY_ACTION_STOP) {
            // STOP bit is set, exit the loop, enter into terminate flow
            quit_state = ESP_GMF_EVENT_STATE_STOPPED;
            break;
        }
        ESP_LOGV(TAG, "Find next job to process, [%s-%p, cur:%p-%p-%s]", OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk, worker, worker->ctx, worker->label);
        worker = __esp_gmf_get_next_job(tsk, worker);
        ESP_LOGV(TAG, "Found next job[%p] to process", worker);
    }
    // Terminate flow: load close jobs and run
    __esp_gmf_task_delete_jobs(tsk);
    esp_gmf_job_stack_clear(tsk->start_stack);
    __esp_gmf_task_load_job(tsk, GMF_TASK_STRATEGY_ACTION_STOP);

    ESP_LOGD(TAG, "Worker exit, [%p-%s], cur_st:%s, quit_state:%s", tsk, OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), esp_gmf_event_get_state_str(tsk->state), esp_gmf_event_get_state_str(quit_state));
    worker = tsk->working;
    while (worker && worker->func) {
        worker->ret = worker->func(worker->ctx, NULL);
        // Failed when do clear up set state to error, continue to clear up for other jobs
        if (worker->ret != ESP_GMF_JOB_ERR_OK) {
            quit_state = ESP_GMF_EVENT_STATE_ERROR;
        }
        worker = __esp_gmf_get_next_job(tsk, worker);
    }
    tsk->state = quit_state;
    __esp_gmf_event_state_notify(tsk, ESP_GMF_EVT_TYPE_CHANGE_STATE, tsk->state);
    GMF_TASK_SET_STATE_BITS(tsk->event_group, GMF_TASK_STOP_BIT);
    TASK_CLR_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_STOP);
    return result;
}

static void esp_gmf_thread_fun(void *pv)
{
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)pv;
    TASK_CLR_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_DESTROY);
    while (TASK_HAS_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_CREATED)) {
        while ((tsk->working == NULL) || (!TASK_HAS_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_RUNNING))) {
            ESP_LOGI(TAG, "Waiting to run... [tsk:%s-%p, wk:%p, run:%d]", OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk,
                     tsk->working, TASK_HAS_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_RUNNING));
            xSemaphoreTake(tsk->block_sem, portMAX_DELAY);
            if (TASK_HAS_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_DESTROY)) {
                TASK_CLR_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_DESTROY);
                ESP_LOGD(TAG, "Thread will be destroyed, [%s,%p]", OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk);
                goto ESP_GMF_THREAD_EXIT;
            }
            if (TASK_HAS_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_RUN)) {
                TASK_SET_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_RUNNING);
                TASK_CLR_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_RUN);
            }
        }
        // Set RUN_BIT before io open for maybe time consuming
        GMF_TASK_SET_STATE_BITS(tsk->event_group, GMF_TASK_RUN_BIT);
        int ret = __esp_gmf_task_event_state_change_and_notify(tsk, ESP_GMF_EVENT_STATE_RUNNING);
        if (ret != ESP_GMF_ERR_OK) {
            TASK_CLR_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_RUNNING);
            ESP_LOGE(TAG, "Failed on prepare, [%s,%p],ret:%d", OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk, ret);
            // Clear the jobs, to make sure the task re-running must need to loading jobs again
            __esp_gmf_task_delete_jobs(tsk);
            esp_gmf_job_stack_clear(tsk->start_stack);
            // For already run force set STOP_BIT
            GMF_TASK_SET_STATE_BITS(tsk->event_group, GMF_TASK_STOP_BIT);
            continue;
        }
        // Loop jobs until done or error
        __process_func(tsk, tsk->ctx);
        TASK_CLR_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_RUNNING);
    }
ESP_GMF_THREAD_EXIT:
    tsk->state = ESP_GMF_EVENT_STATE_NONE;
    void *oal_thread = tsk->oal_thread;
    GMF_TASK_SET_STATE_BITS(tsk->event_group, GMF_TASK_EXIT_BIT);
    ESP_LOGD(TAG, "Thread destroyed! [%s,%p]", OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk);
    esp_gmf_oal_thread_delete(oal_thread);
}

static esp_gmf_err_t _task_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_task_init((esp_gmf_task_cfg_t *)cfg, handle);
}

esp_gmf_err_t esp_gmf_task_init(esp_gmf_task_cfg_t *config, esp_gmf_task_handle_t *tsk_hd)
{
    ESP_GMF_NULL_CHECK(TAG, tsk_hd, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_task_t *handle = calloc(1, sizeof(struct _esp_gmf_task));
    ESP_GMF_MEM_CHECK(TAG, handle, return ESP_GMF_ERR_MEMORY_LACK);
    handle->lock = esp_gmf_oal_mutex_create();
    ESP_GMF_MEM_CHECK(TAG, handle->lock, goto _tsk_init_failed);
    handle->event_group = xEventGroupCreate();
    ESP_GMF_MEM_CHECK(TAG, handle->event_group, goto _tsk_init_failed);
    handle->block_sem = xSemaphoreCreateBinary();
    ESP_GMF_MEM_CHECK(TAG, handle->block_sem, goto _tsk_init_failed);
    handle->wait_sem = xSemaphoreCreateBinary();
    ESP_GMF_MEM_CHECK(TAG, handle->wait_sem, goto _tsk_init_failed);
    esp_gmf_task_cfg_t *cfg = (esp_gmf_task_cfg_t *)config;
    handle->event_func = cfg->cb;
    handle->ctx = cfg->ctx;
    handle->api_sync_time = DEFAULT_TASK_OPT_MAX_TIME_MS;
    handle->_strategy_func = NULL;
    handle->_strategy_ctx = NULL;

    esp_gmf_job_stack_create(&handle->start_stack);
    ESP_GMF_MEM_CHECK(TAG, handle->start_stack, goto _tsk_init_failed);

    char tag[ESP_GMF_TAG_MAX_LEN] = {0};
    if (cfg->name) {
        // User original name without modify
        snprintf(tag, ESP_GMF_TAG_MAX_LEN, "%s", cfg->name);
    } else {
        snprintf(tag, ESP_GMF_TAG_MAX_LEN, "TSK_%p", handle);
    }

    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)handle;
    esp_gmf_err_t ret = esp_gmf_obj_set_tag(obj, tag);
    ESP_GMF_RET_ON_ERROR(TAG, ret, goto _tsk_init_failed, "Failed set OBJ tag");
    obj->new_obj = _task_new;
    obj->del_obj = esp_gmf_task_deinit;

    if (cfg->thread.stack > 0) {
        handle->thread.stack = cfg->thread.stack;
        handle->thread.stack_in_ext = cfg->thread.stack_in_ext;
    }
    if (cfg->thread.prio) {
        handle->thread.prio = cfg->thread.prio;
    } else {
        handle->thread.prio = DEFAULT_ESP_GMF_TASK_PRIO;
    }
    if (cfg->thread.core) {
        handle->thread.core = cfg->thread.core;
    } else {
        handle->thread.core = DEFAULT_ESP_GMF_TASK_CORE;
    }
    TASK_SET_ACTION(handle->_actions, GMF_TASK_ACTION_TYPE_CREATED);
    if (handle->thread.stack > 0) {
        ret = esp_gmf_oal_thread_create(&handle->oal_thread, OBJ_GET_TAG(obj), esp_gmf_thread_fun, handle, handle->thread.stack,
                                        handle->thread.prio, handle->thread.stack_in_ext, handle->thread.core);
        if (ret == ESP_GMF_ERR_FAIL) {
            TASK_CLR_ACTION(handle->_actions, GMF_TASK_ACTION_TYPE_CREATED);
            ESP_LOGE(TAG, "Create thread failed, [%s]", OBJ_GET_TAG((esp_gmf_obj_handle_t)handle));
            goto _tsk_init_failed;
        }
    }
    handle->state = ESP_GMF_EVENT_STATE_INITIALIZED;
    *tsk_hd = handle;
    return ESP_GMF_ERR_OK;
_tsk_init_failed:
    __esp_gmf_task_free(handle);
    return ESP_GMF_ERR_FAIL;
}

esp_gmf_err_t esp_gmf_task_deinit(esp_gmf_task_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    // Wait for thread quit if already run
    if (TASK_HAS_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_CREATED)) {
        esp_gmf_oal_mutex_lock(tsk->lock);
        if ((tsk->state == ESP_GMF_EVENT_STATE_RUNNING) || (tsk->state == ESP_GMF_EVENT_STATE_PAUSED)) {
            TASK_SET_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_STOP);
        }
        if (tsk->state == ESP_GMF_EVENT_STATE_PAUSED) {
            __esp_gmf_task_release_signal(tsk, portMAX_DELAY);
        }
        TASK_CLR_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_CREATED);
        TASK_SET_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_DESTROY);
        xSemaphoreGive(tsk->block_sem);
        // Wait for task exit
        if (GMF_TASK_WAIT_FOR_STATE_BITS(tsk->event_group, GMF_TASK_EXIT_BIT, portMAX_DELAY) == false) {
            ESP_LOGE(TAG, "Failed to wait task %p to exit", tsk);
        }
        ESP_LOGD(TAG, "%s, %s", __func__, OBJ_GET_TAG(tsk));
        __esp_gmf_task_delete_jobs(tsk);
        esp_gmf_oal_mutex_unlock(tsk->lock);
    }
    // Clear-up all related resources
    __esp_gmf_task_free(tsk);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_task_register_ready_job(esp_gmf_task_handle_t handle, const char *label, esp_gmf_job_func job, esp_gmf_job_times_t times, void *ctx, bool done)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    esp_gmf_job_t *new_job = NULL;
    new_job = esp_gmf_oal_calloc(1, sizeof(esp_gmf_job_t));
    ESP_GMF_MEM_CHECK(TAG, new_job, return ESP_GMF_ERR_MEMORY_LACK;);
    new_job->label = esp_gmf_oal_strdup(label == NULL ? "NULL" : label);
    ESP_GMF_MEM_CHECK(TAG, new_job->label, { esp_gmf_oal_free(new_job); return ESP_GMF_ERR_MEMORY_LACK;});
    new_job->func = job;
    new_job->ctx = ctx;
    new_job->times = times;
    new_job->is_deleted = 0;

    // Add the first infinite processing job to the stack
    bool is_empty = false;
    esp_gmf_job_stack_is_empty(tsk->start_stack, &is_empty);
    if ((times == ESP_GMF_JOB_TIMES_INFINITE) && (is_empty == true)) {
        esp_gmf_job_stack_push(tsk->start_stack, (uint32_t)new_job);
    }

    if (tsk->working == NULL) {
        tsk->working = new_job;
    } else {
        esp_gmf_node_add_last((esp_gmf_node_t *)tsk->working, (esp_gmf_node_t *)new_job);
    }
    ESP_LOGD(TAG, "Reg new job to task:%p, item:%p, label:%s, func:%p, ctx:%p cnt:%d", tsk, new_job, new_job->label, job, ctx, get_jobs_num(tsk->working));
    if (done) {
        xSemaphoreGive(tsk->block_sem);
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_task_insert_head_job(esp_gmf_task_handle_t handle, const char *label, esp_gmf_job_func job, esp_gmf_job_times_t times, void *ctx)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    esp_gmf_job_t *new_job = NULL;
    new_job = esp_gmf_oal_calloc(1, sizeof(esp_gmf_job_t));
    ESP_GMF_MEM_CHECK(TAG, new_job, return ESP_GMF_ERR_MEMORY_LACK;);
    new_job->label = esp_gmf_oal_strdup(label == NULL ? "NULL" : label);
    ESP_GMF_MEM_CHECK(TAG, new_job->label, { esp_gmf_oal_free(new_job); return ESP_GMF_ERR_MEMORY_LACK;});
    new_job->func = job;
    new_job->ctx = ctx;
    new_job->times = times;
    new_job->is_deleted = 0;

    bool is_empty = false;
    esp_gmf_job_stack_is_empty(tsk->start_stack, &is_empty);
    if (times == ESP_GMF_JOB_TIMES_INFINITE) {
        if (is_empty == false) {
            ESP_LOGI(TAG, "Insert a infinite job to head, clear before stack jobs, task:%p, item:%p, label:%s, func:%p, ctx:%p",
                     tsk, new_job, new_job->label, job, ctx);
            esp_gmf_job_stack_clear(tsk->start_stack);
        }
        esp_gmf_job_stack_push(tsk->start_stack, (uint32_t)new_job);
    }
    if (tsk->working == NULL) {
        tsk->working = new_job;
    } else {
        esp_gmf_job_t *head = tsk->working;
        esp_gmf_node_insert_prev((esp_gmf_node_t **)&tsk->working, (esp_gmf_node_t *)head, (esp_gmf_node_t *)new_job);
    }
    ESP_LOGI(TAG, "Insert head job to task:%p, item:%p, label:%s, func:%p, ctx:%p cnt:%d", tsk, new_job, new_job->label, job, ctx, get_jobs_num(tsk->working));
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_task_set_event_func(esp_gmf_task_handle_t handle, esp_gmf_event_cb cb, void *ctx)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    esp_gmf_oal_mutex_lock(tsk->lock);
    tsk->event_func = cb;
    tsk->ctx = ctx;
    esp_gmf_oal_mutex_unlock(tsk->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_task_set_strategy_func(esp_gmf_task_handle_t handle, esp_gmf_task_strategy_func func, void *ctx)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    tsk->_strategy_func = func;
    tsk->_strategy_ctx = ctx;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_task_set_pause_on_start(esp_gmf_task_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    esp_gmf_oal_mutex_lock(tsk->lock);
    TASK_SET_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_PAUSE_ON_START);
    esp_gmf_oal_mutex_unlock(tsk->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_task_run(esp_gmf_task_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    esp_gmf_oal_mutex_lock(tsk->lock);
    ESP_LOGD(TAG, "%s, %s-%p,st:%s", __func__, OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk, esp_gmf_event_get_state_str(tsk->state));
    if (tsk->state == ESP_GMF_EVENT_STATE_PAUSED || tsk->state == ESP_GMF_EVENT_STATE_RUNNING) {
        esp_gmf_oal_mutex_unlock(tsk->lock);
        ESP_LOGW(TAG, "Can't run on %s, [%s,%p]", esp_gmf_event_get_state_str(tsk->state), OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk);
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    if (!TASK_HAS_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_CREATED)) {
        esp_gmf_oal_mutex_unlock(tsk->lock);
        ESP_LOGW(TAG, "No task for run, %s, [%s,%p]", esp_gmf_event_get_state_str(tsk->state), OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk);
        return ESP_GMF_ERR_INVALID_STATE;
    }
    TASK_SET_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_RUN);
    xSemaphoreGive(tsk->block_sem);
    // Wait for run finished
    if (GMF_TASK_WAIT_FOR_STATE_BITS(tsk->event_group, GMF_TASK_RUN_BIT, tsk->api_sync_time) == false) {
        ESP_LOGE(TAG, "Run timeout,[%s,%p]", OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk);
        esp_gmf_oal_mutex_unlock(tsk->lock);
        return ESP_GMF_ERR_TIMEOUT;
    }
    esp_gmf_oal_mutex_unlock(tsk->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_task_stop(esp_gmf_task_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    esp_gmf_oal_mutex_lock(tsk->lock);
    ESP_LOGD(TAG, "%s, %s-%p, st:%s", __func__, OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk, esp_gmf_event_get_state_str(tsk->state));
    // There is transition state from action RUNNING to status RUNNING, check both
    if (!(tsk->state == ESP_GMF_EVENT_STATE_RUNNING || TASK_HAS_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_RUNNING)) &&
        (tsk->state != ESP_GMF_EVENT_STATE_PAUSED)) {
        esp_gmf_oal_mutex_unlock(tsk->lock);
        ESP_LOGD(TAG, "Already stopped, %s, [%s,%p]", esp_gmf_event_get_state_str(tsk->state), OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk);
        return ESP_GMF_ERR_OK;
    }
    if (!TASK_HAS_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_CREATED)) {
        esp_gmf_oal_mutex_unlock(tsk->lock);
        ESP_LOGW(TAG, "The task is not running, %s, [%s,%p]", esp_gmf_event_get_state_str(tsk->state), OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk);
        return ESP_GMF_ERR_INVALID_STATE;
    }
    if ((tsk->state == ESP_GMF_EVENT_STATE_NONE)) {
        esp_gmf_oal_mutex_unlock(tsk->lock);
        ESP_LOGW(TAG, "Can't stop on %s, [%s,%p]", esp_gmf_event_get_state_str(tsk->state), OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk);
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    TASK_SET_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_STOP);
    if (tsk->state == ESP_GMF_EVENT_STATE_PAUSED) {
        __esp_gmf_task_release_signal(tsk, portMAX_DELAY);
    }
    if (GMF_TASK_WAIT_FOR_STATE_BITS(tsk->event_group, GMF_TASK_STOP_BIT, tsk->api_sync_time) == false) {
        ESP_LOGW(TAG, "Stop timeout for [%s,%p], retrying...", OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk);
        esp_gmf_err_t ret = ESP_GMF_ERR_TIMEOUT;
        // Print timeout message if user timeout too small, wait more until task full quit
        if (GMF_TASK_WAIT_FOR_STATE_BITS(tsk->event_group, GMF_TASK_STOP_BIT, 0xFFFFFFFF)) {
            ret = ESP_GMF_ERR_OK;
        }
        esp_gmf_oal_mutex_unlock(tsk->lock);
        TASK_CLR_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_STOP);
        return ret;
    }
    TASK_CLR_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_STOP);
    esp_gmf_oal_mutex_unlock(tsk->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_task_pause(esp_gmf_task_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;

    esp_gmf_oal_mutex_lock(tsk->lock);
    ESP_LOGD(TAG, "%s, task:%s-%p, st:%s", __func__, OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk, esp_gmf_event_get_state_str(tsk->state));
    if ((tsk->state == ESP_GMF_EVENT_STATE_STOPPED)
        || (tsk->state == ESP_GMF_EVENT_STATE_PAUSED)
        || (tsk->state == ESP_GMF_EVENT_STATE_FINISHED)
        || (tsk->state == ESP_GMF_EVENT_STATE_ERROR)) {
        esp_gmf_oal_mutex_unlock(tsk->lock);
        ESP_LOGW(TAG, "Without pause on %s, [%s,%p]", esp_gmf_event_get_state_str(tsk->state), OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk);
        return ESP_GMF_ERR_OK;
    }

    if ((tsk->state != ESP_GMF_EVENT_STATE_RUNNING)) {
        esp_gmf_oal_mutex_unlock(tsk->lock);
        ESP_LOGW(TAG, "Can't pause on %s, [%s,%p]", esp_gmf_event_get_state_str(tsk->state), OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk);
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    TASK_SET_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_PAUSE);
    if (tsk->api_sync_time > 0) {
        if (GMF_TASK_WAIT_FOR_MULTI_STATE_BITS(tsk->event_group, GMF_TASK_PAUSE_BIT | GMF_TASK_STOP_BIT, tsk->api_sync_time) == false) {
            ESP_LOGE(TAG, "Pause timeout,[%s,%p]", OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk);
            esp_gmf_oal_mutex_unlock(tsk->lock);
            return ESP_GMF_ERR_TIMEOUT;
        }
    }
    TASK_CLR_ACTION(tsk->_actions, GMF_TASK_ACTION_TYPE_PAUSE);
    // Only clear pause bits
    GMF_TASK_CLR_STATE_BITS(tsk->event_group, GMF_TASK_PAUSE_BIT);
    esp_gmf_oal_mutex_unlock(tsk->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_task_resume(esp_gmf_task_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    esp_gmf_oal_mutex_lock(tsk->lock);
    ESP_LOGD(TAG, "%s, task:%s-%p,st:%s", __func__, OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk, esp_gmf_event_get_state_str(tsk->state));
    if ((tsk->state != ESP_GMF_EVENT_STATE_PAUSED)) {
        esp_gmf_oal_mutex_unlock(tsk->lock);
        ESP_LOGW(TAG, "Can't resume on %s, [%s,%p]", esp_gmf_event_get_state_str(tsk->state), OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk);
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    __esp_gmf_task_release_signal(tsk, portMAX_DELAY);
    if (GMF_TASK_WAIT_FOR_MULTI_STATE_BITS(tsk->event_group, GMF_TASK_RESUME_BIT | GMF_TASK_STOP_BIT, tsk->api_sync_time) == false) {
        ESP_LOGE(TAG, "Resume timeout,[%s,%p]", OBJ_GET_TAG((esp_gmf_obj_handle_t)tsk), tsk);
        esp_gmf_oal_mutex_unlock(tsk->lock);
        return ESP_GMF_ERR_TIMEOUT;
    }
    GMF_TASK_CLR_STATE_BITS(tsk->event_group, GMF_TASK_RESUME_BIT);
    esp_gmf_oal_mutex_unlock(tsk->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_task_reset(esp_gmf_task_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    tsk->state = ESP_GMF_EVENT_STATE_INITIALIZED;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_task_set_timeout(esp_gmf_task_handle_t handle, int wait_ms)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    tsk->api_sync_time = wait_ms / portTICK_PERIOD_MS;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_task_get_state(esp_gmf_task_handle_t handle, esp_gmf_event_state_t *state)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_task_t *tsk = (esp_gmf_task_t *)handle;
    if (state) {
        *state = tsk->state;
        return ESP_GMF_ERR_OK;
    }
    return ESP_GMF_ERR_INVALID_ARG;
}

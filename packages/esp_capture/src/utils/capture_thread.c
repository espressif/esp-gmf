/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_capture.h"
#include "esp_gmf_oal_thread.h"
#include "capture_thread.h"
#include "esp_memory_utils.h"

static esp_capture_thread_scheduler_cb_t capture_scheduler = NULL;

void capture_thread_set_scheduler(esp_capture_thread_scheduler_cb_t scheduler)
{
    capture_scheduler = scheduler;
}

esp_capture_thread_scheduler_cb_t capture_thread_get_scheduler(void)
{
    return capture_scheduler;
}

void capture_thread_run_in_ram(void *arg)
{
    capture_thread_sync_run_arg_t *sync_arg = (capture_thread_sync_run_arg_t *)arg;
    if (sync_arg->body) {
        sync_arg->ret = sync_arg->body(sync_arg->arg);
    }
    if (sync_arg->sema) {
        capture_sema_unlock(sync_arg->sema);
    }
    capture_thread_destroy(NULL);
}

int capture_thread_create(capture_thread_handle_t *handle, const char *name, esp_capture_thread_schedule_cfg_t *cfg,
                          void (*body)(void *arg), void *arg)
{
    return esp_gmf_oal_thread_create((esp_gmf_oal_thread_t *)handle, name, body, arg, cfg->stack_size,
                                     cfg->priority, cfg->stack_in_ext, cfg->core_id);
}

int capture_thread_create_from_scheduler(capture_thread_handle_t *handle, const char *name,
                                         void (*body)(void *arg), void *arg)
{
    esp_capture_thread_schedule_cfg_t cfg = CAPTURE_DEFAULT_SCHEDULER();
    *handle = NULL;
    if (capture_scheduler) {
        capture_scheduler(name, &cfg);
    }
    return esp_gmf_oal_thread_create((esp_gmf_oal_thread_t *)handle, name, body, arg, cfg.stack_size,
                                     cfg.priority, cfg.stack_in_ext, cfg.core_id);
}

void capture_thread_get_scheduler_cfg(const char *name, esp_capture_thread_schedule_cfg_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    esp_capture_thread_schedule_cfg_t default_cfg = CAPTURE_DEFAULT_SCHEDULER();
    *cfg = default_cfg;
    if (name == NULL) {
        // Get current task name
        name = pcTaskGetName(NULL);
    }
    if (capture_scheduler) {
        capture_scheduler(name, cfg);
    }
}

bool capture_thread_is_stack_in_ram(void)
{
    uint8_t *task_stack = pxTaskGetStackStart(NULL);
    return (task_stack && esp_ptr_internal(task_stack));
}

void capture_thread_destroy(capture_thread_handle_t thread)
{
    esp_gmf_oal_thread_delete((esp_gmf_oal_thread_t)thread);
}

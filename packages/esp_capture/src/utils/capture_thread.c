/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_capture.h"
#include "esp_gmf_oal_thread.h"
#include "capture_thread.h"

#define CAPTURE_DEFAULT_SCHEDULER() {   \
    .priority = 5, .stack_size = 4096,  \
}

static esp_capture_thread_scheduler_cb_t capture_scheduler = NULL;

void capture_thread_set_scheduler(esp_capture_thread_scheduler_cb_t scheduler)
{
    capture_scheduler = scheduler;
}

esp_capture_thread_scheduler_cb_t capture_thread_get_scheduler(void)
{
    return capture_scheduler;
}

int capture_thread_create_from_scheduler(capture_thread_handle_t *handle, const char *name,
                                         void (*body)(void *arg), void *arg)
{
    esp_capture_thread_schedule_cfg_t cfg = CAPTURE_DEFAULT_SCHEDULER();
    *handle = NULL;
    if (capture_scheduler) {
        capture_scheduler(name, &cfg);
    }
    esp_gmf_err_t ret = esp_gmf_oal_thread_create((esp_gmf_oal_thread_t *)handle, name, body, arg, cfg.stack_size,
                                                  cfg.priority, cfg.stack_in_ext, cfg.core_id);
    return ret;
}

void capture_thread_destroy(capture_thread_handle_t thread)
{
    esp_gmf_oal_thread_delete((esp_gmf_oal_thread_t)thread);
}

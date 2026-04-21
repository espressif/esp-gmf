/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef int esp_gmf_err_t;
#define ESP_GMF_ERR_OK  0

typedef void *esp_gmf_oal_thread_t;
typedef void (*esp_gmf_oal_thread_func_t)(void *arg);

// Signature matches how esp_video_render uses it.
esp_gmf_err_t esp_gmf_oal_thread_create(esp_gmf_oal_thread_t *out_handle,
                                        const char *name,
                                        esp_gmf_oal_thread_func_t entry,
                                        void *arg,
                                        uint32_t stack_size,
                                        int prio,
                                        bool pinned,
                                        int core_id);

void esp_gmf_oal_thread_delete(esp_gmf_oal_thread_t handle);

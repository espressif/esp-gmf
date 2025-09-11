/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_gmf_oal_thread.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_sys.h"

static bool monitor_run;

static void sys_monitor_task(void *para)
{
    while (monitor_run) {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        if (monitor_run == false) {
            break;
        }
        esp_gmf_oal_sys_get_real_time_stats(1000, false);
        ESP_GMF_MEM_SHOW("MONITOR");
    }
    esp_gmf_oal_thread_delete(NULL);
}

void esp_gmf_app_sys_monitor_start(void)
{
    monitor_run = true;
    esp_gmf_oal_thread_create(NULL, "sys_monitor", sys_monitor_task, NULL, (4 * 1024), 1, true, 0);
}

void esp_gmf_app_sys_monitor_stop(void)
{
    monitor_run = false;
}

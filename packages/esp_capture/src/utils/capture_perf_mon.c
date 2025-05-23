/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "capture_mem.h"
#include "capture_os.h"
#include "capture_perf_mon.h"

#ifdef CONFIG_ESP_CAPTURE_ENABLE_PERF_MON
static bool  perf_mon_enable = false;
static char *mon_buffer;
static int   mon_size;
static int   mon_fill;
static void *mon_mutex;

static void capture_perf_monitor_dump(void)
{
    capture_mutex_lock(mon_mutex, CAPTURE_MAX_LOCK_TIME);
    if (mon_fill) {
        printf("%s", (char *)mon_buffer);
    }
    capture_mutex_unlock(mon_mutex);
}

void capture_perf_monitor_enable(bool enable)
{
    if (perf_mon_enable == enable) {
        if (enable) {
            // Allow restart monitor
            mon_fill = 0;
        }
        return;
    }
    if (enable) {
        mon_buffer = capture_calloc(1, CAPTURE_PERF_MON_BUFF_SIZE);
        if (mon_buffer == NULL) {
            return;
        }
        mon_size = CAPTURE_PERF_MON_BUFF_SIZE;
        mon_fill = 0;
        capture_mutex_create(&mon_mutex);
        perf_mon_enable = true;
    } else {
        // Clear firstly
        perf_mon_enable = false;
        capture_perf_monitor_dump();
        if (mon_buffer != NULL) {
            capture_free(mon_buffer);
            mon_buffer = NULL;
        }
        if (mon_mutex) {
            capture_mutex_destroy(mon_mutex);
            mon_mutex = NULL;
        }
        mon_fill = 0;
    }
}

void capture_perf_monitor_add(uint8_t path, const char *desc, uint32_t start_time, uint32_t duration)
{
    if (perf_mon_enable == false || mon_fill >= mon_size) {
        return;
    }
    capture_mutex_lock(mon_mutex, CAPTURE_MAX_LOCK_TIME);
    int n = snprintf(mon_buffer + mon_fill, mon_size - mon_fill - 1,
                     "%d\t%lu\t%lu\t%s\n", path, start_time, duration, desc);
    if (n > 0 && (mon_fill + n) < mon_size) {
        mon_fill += n;
    } else {
        mon_fill = mon_size;
    }
    capture_mutex_unlock(mon_mutex);
}

#endif  /* CONFIG_ESP_CAPTURE_ENABLE_PERF_MON */

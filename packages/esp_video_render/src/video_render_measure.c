/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <freertos/FreeRTOS.h>
#include "video_render_measure.h"
#include "video_render_sys.h"
#include "esp_video_render_log.h"
#include "esp_timer.h"

#define MAX_MEASURE_ITEMS  50
#define MAX_MODULE_NAME    16
#define MAX_FUNC_NAME      16
#define PRINT_MARK_MODULE  0x01
#define PRINT_MARK_FUNC    0x02

typedef struct {
    char      module[MAX_MODULE_NAME];
    char      func[MAX_FUNC_NAME];
    uint64_t  start_us;
    uint64_t  duration_us;
    uint8_t   core;
    uint8_t   print_mark;
    uint16_t  count;
} measure_item_t;

typedef struct {
    uint64_t                     start_us;
    uint16_t                     item_count;
    video_render_mutex_handle_t  lock;
    measure_item_t               items[MAX_MEASURE_ITEMS];
} measure_context_t;

static measure_context_t *s_measure;
static video_render_mutex_handle_t s_measure_gate;

static void measure_gate_lock(void)
{
    if (s_measure_gate == NULL) {
        s_measure_gate = video_render_mutex_create();
    }
    if (s_measure_gate) {
        video_render_mutex_lock(s_measure_gate, VIDEO_RENDER_MAX_LOCK_TIME);
    }
}

static void measure_gate_unlock(void)
{
    if (s_measure_gate) {
        video_render_mutex_unlock(s_measure_gate);
    }
}

static int key_cmp(uint8_t core, const char *module, const char *func, const measure_item_t *item)
{
    if (core != item->core) {
        return (core < item->core) ? -1 : 1;
    }
    int mc = strcmp(module, item->module);
    if (mc != 0) {
        return mc;
    }
    return strcmp(func, item->func);
}

static int binary_search_item(measure_context_t *ctx, uint8_t core, const char *module, const char *func, bool *found)
{
    int lo = 0;
    int hi = (int)ctx->item_count - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        int cmp = key_cmp(core, module, func, &ctx->items[mid]);
        if (cmp == 0) {
            *found = true;
            return mid;
        }
        if (cmp < 0) {
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    *found = false;
    return lo;
}

static inline void format_func_name(char *out, size_t out_size, const char *func_fmt, va_list ap)
{
    vsnprintf(out, out_size, func_fmt, ap);
}

static measure_item_t *find_or_insert_item(measure_context_t *ctx, uint8_t core, const char *module, const char *func)
{
    bool found = false;
    int pos = binary_search_item(ctx, core, module, func, &found);
    if (found) {
        return &ctx->items[pos];
    }
    if (ctx->item_count >= MAX_MEASURE_ITEMS) {
        return NULL;
    }
    for (int i = (int)ctx->item_count; i > pos; i--) {
        ctx->items[i] = ctx->items[i - 1];
    }
    measure_item_t *item = &ctx->items[pos];
    memset(item, 0, sizeof(*item));
    item->core = core;
    snprintf(item->module, sizeof(item->module), "%s", module);
    snprintf(item->func, sizeof(item->func), "%s", func);
    ctx->item_count++;
    return item;
}

static void video_render_measure_print(measure_context_t *measure)
{
    uint64_t now_us = (uint64_t)esp_timer_get_time();
    uint64_t elapsed_us = (now_us > measure->start_us) ? (now_us - measure->start_us) : 0;
    if (elapsed_us == 0) {
        return;
    }
    for (uint16_t i = 0; i < measure->item_count; i++) {
        measure->items[i].print_mark = 0;
    }

    printf("\n=== Video Render Measure === \n");
    for (int core = 0; core < 2; core++) {
        uint64_t core_total = 0;
        for (uint16_t i = 0; i < measure->item_count; i++) {
            if (measure->items[i].core == (uint8_t)core) {
                core_total += measure->items[i].duration_us;
            }
        }
        if (core_total == 0) {
            continue;
        }
        printf("[core%d] total=%" PRIu64 "us\n", core, core_total);

        while (1) {
            int best_idx = -1;
            uint64_t best_mod_total = 0;
            uint32_t best_mod_calls = 0;
            for (uint16_t i = 0; i < measure->item_count; i++) {
                measure_item_t *it = &measure->items[i];
                if (it->core != (uint8_t)core || (it->print_mark & PRINT_MARK_MODULE)) {
                    continue;
                }
                uint64_t mod_total = 0;
                uint32_t mod_calls = 0;
                for (uint16_t j = i; j < measure->item_count; j++) {
                    measure_item_t *jt = &measure->items[j];
                    if (jt->core == (uint8_t)core && strcmp(jt->module, it->module) == 0) {
                        mod_total += jt->duration_us;
                        mod_calls += jt->count;
                    }
                }
                if (mod_total > best_mod_total) {
                    best_idx = (int)i;
                    best_mod_total = mod_total;
                    best_mod_calls = mod_calls;
                }
            }
            if (best_idx < 0) {
                break;
            }

            measure_item_t *best = &measure->items[best_idx];
            for (uint16_t i = 0; i < measure->item_count; i++) {
                measure_item_t *it = &measure->items[i];
                if (it->core == (uint8_t)core && strcmp(it->module, best->module) == 0) {
                    it->print_mark |= PRINT_MARK_MODULE;
                    it->print_mark &= ~PRINT_MARK_FUNC;
                }
            }
            double mod_pct_total = (double)best_mod_total * 100.0 / (double)elapsed_us;
            printf("  [%s] total=%" PRIu64 "us (%.2f%% cpu), calls=%" PRIu32 "\n",
                   best->module, best_mod_total, mod_pct_total, best_mod_calls);

            while (1) {
                int best_func_idx = -1;
                uint64_t best_func_us = 0;
                for (uint16_t i = 0; i < measure->item_count; i++) {
                    measure_item_t *it = &measure->items[i];
                    if (it->core != (uint8_t)core || strcmp(it->module, best->module) != 0 ||
                        (it->print_mark & PRINT_MARK_FUNC)) {
                        continue;
                    }
                    if (it->duration_us > best_func_us) {
                        best_func_us = it->duration_us;
                        best_func_idx = (int)i;
                    }
                }
                if (best_func_idx < 0) {
                    break;
                }
                measure_item_t *fit = &measure->items[best_func_idx];
                fit->print_mark |= PRINT_MARK_FUNC;
                double func_pct_total = (double)fit->duration_us * 100.0 / (double)elapsed_us;
                uint64_t avg = fit->count ? (fit->duration_us / fit->count) : 0;
                printf("    - %-16s %10" PRIu64 "us (%6.2f%% cpu) calls=%" PRIu16 " avg=%" PRIu64 "us\n",
                       fit->func, fit->duration_us, func_pct_total, fit->count, avg);
            }
        }
    }
    printf("============================\n");
}

void video_render_measure_enable(bool enable)
{
    measure_gate_lock();
    if ((enable && s_measure) || (!enable && !s_measure)) {
        measure_gate_unlock();
        return;
    }
    if (enable) {
        measure_context_t *measure = (measure_context_t *)video_render_calloc(1, sizeof(measure_context_t));
        if (measure == NULL) {
            measure_gate_unlock();
            return;
        }
        measure->lock = video_render_mutex_create();
        if (measure->lock == NULL) {
            video_render_free(measure);
            measure_gate_unlock();
            return;
        }
        measure->start_us = (uint64_t)esp_timer_get_time();
        s_measure = measure;
        measure_gate_unlock();
    } else {
        measure_context_t *measure = s_measure;
        s_measure = NULL;
        measure_gate_unlock();
        /* Wait for any in-flight measure_begin/end that already took `measure` and ctx->lock. */
        video_render_mutex_lock(measure->lock, VIDEO_RENDER_MAX_LOCK_TIME);
        video_render_mutex_unlock(measure->lock);
        video_render_measure_print(measure);
        if (measure->lock) {
            video_render_mutex_destroy(measure->lock);
            measure->lock = NULL;
        }
        video_render_free(measure);
    }
}

void video_render_measure_begin(const char *module, const char *func_fmt, ...)
{
    if (module == NULL || func_fmt == NULL) {
        return;
    }
    char func[MAX_FUNC_NAME];
    func[0] = '\0';
    va_list ap;
    va_start(ap, func_fmt);
    format_func_name(func, sizeof(func), func_fmt, ap);
    va_end(ap);

    measure_gate_lock();
    measure_context_t *ctx = s_measure;
    if (ctx == NULL) {
        measure_gate_unlock();
        return;
    }
    video_render_mutex_lock(ctx->lock, VIDEO_RENDER_MAX_LOCK_TIME);
    measure_gate_unlock();
    uint8_t core = (uint8_t)xPortGetCoreID();
    measure_item_t *item = find_or_insert_item(ctx, core, module, func);
    if (item) {
        item->start_us = (uint64_t)esp_timer_get_time();
    }
    video_render_mutex_unlock(ctx->lock);
}

void video_render_measure_end(const char *module, const char *func_fmt, ...)
{
    if (module == NULL || func_fmt == NULL) {
        return;
    }
    char func[MAX_FUNC_NAME];
    func[0] = '\0';
    va_list ap;
    va_start(ap, func_fmt);
    format_func_name(func, sizeof(func), func_fmt, ap);
    va_end(ap);

    measure_gate_lock();
    measure_context_t *ctx = s_measure;
    if (ctx == NULL) {
        measure_gate_unlock();
        return;
    }
    video_render_mutex_lock(ctx->lock, VIDEO_RENDER_MAX_LOCK_TIME);
    measure_gate_unlock();
    bool found = false;
    uint8_t core = (uint8_t)xPortGetCoreID();
    int pos = binary_search_item(ctx, core, module, func, &found);
    if (found) {
        measure_item_t *item = &ctx->items[pos];
        uint64_t now = (uint64_t)esp_timer_get_time();
        if (now >= item->start_us) {
            item->duration_us += (now - item->start_us);
        }
        item->count++;
        item->start_us = 0;
    }
    video_render_mutex_unlock(ctx->lock);
}

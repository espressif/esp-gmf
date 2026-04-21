#pragma once

#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"

typedef uint32_t EventBits_t;

typedef struct {
    pthread_mutex_t  m;
    pthread_cond_t   cv;
    EventBits_t      bits;
} emu_event_group_t;

typedef void *EventGroupHandle_t;

static inline EventGroupHandle_t xEventGroupCreate(void)
{
    emu_event_group_t *g = (emu_event_group_t *)calloc(1, sizeof(*g));
    if (!g) {
        return NULL;
    }
    pthread_mutex_init(&g->m, NULL);
    pthread_cond_init(&g->cv, NULL);
    g->bits = 0;
    return (EventGroupHandle_t)g;
}

static inline void vEventGroupDelete(EventGroupHandle_t h)
{
    if (!h) {
        return;
    }
    emu_event_group_t *g = (emu_event_group_t *)h;
    pthread_cond_destroy(&g->cv);
    pthread_mutex_destroy(&g->m);
    free(g);
}

static inline EventBits_t xEventGroupSetBits(EventGroupHandle_t h, const EventBits_t bits_to_set)
{
    emu_event_group_t *g = (emu_event_group_t *)h;
    pthread_mutex_lock(&g->m);
    g->bits |= bits_to_set;
    pthread_cond_broadcast(&g->cv);
    EventBits_t out = g->bits;
    pthread_mutex_unlock(&g->m);
    return out;
}

static inline BaseType_t xEventGroupSetBitsFromISR(EventGroupHandle_t h, const EventBits_t bits_to_set, BaseType_t *pxHigherPriorityTaskWoken)
{
    (void)pxHigherPriorityTaskWoken;
    xEventGroupSetBits(h, bits_to_set);
    return pdTRUE;
}

static inline EventBits_t xEventGroupClearBits(EventGroupHandle_t h, const EventBits_t bits_to_clear)
{
    emu_event_group_t *g = (emu_event_group_t *)h;
    pthread_mutex_lock(&g->m);
    EventBits_t out = g->bits;
    g->bits &= ~bits_to_clear;
    pthread_mutex_unlock(&g->m);
    return out;
}

static inline EventBits_t xEventGroupWaitBits(EventGroupHandle_t h,
                                              const EventBits_t bits_to_wait_for,
                                              const BaseType_t clear_on_exit,
                                              const BaseType_t wait_for_all_bits,
                                              TickType_t ticks_to_wait)
{
    emu_event_group_t *g = (emu_event_group_t *)h;
    pthread_mutex_lock(&g->m);

    // ticks_to_wait is ms in our emu config (portTICK_PERIOD_MS=1)
    const uint32_t timeout_ms = (ticks_to_wait == portMAX_DELAY) ? 0xFFFFFFFFu : (uint32_t)ticks_to_wait;

    if (timeout_ms == 0xFFFFFFFFu) {
        while (wait_for_all_bits ? ((g->bits & bits_to_wait_for) != bits_to_wait_for)
                                 : ((g->bits & bits_to_wait_for) == 0)) {
            pthread_cond_wait(&g->cv, &g->m);
        }
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += (time_t)(timeout_ms / 1000);
        ts.tv_nsec += (long)((timeout_ms % 1000) * 1000000L);
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }
        while (wait_for_all_bits ? ((g->bits & bits_to_wait_for) != bits_to_wait_for)
                                 : ((g->bits & bits_to_wait_for) == 0)) {
            if (pthread_cond_timedwait(&g->cv, &g->m, &ts) != 0) {
                break;
            }
        }
    }

    EventBits_t out = g->bits;
    // Only clear bits if the wait condition was actually satisfied (matches FreeRTOS semantics)
    bool condition_met = wait_for_all_bits ? ((out & bits_to_wait_for) == bits_to_wait_for)
                                           : ((out & bits_to_wait_for) != 0);
    if (clear_on_exit && condition_met) {
        g->bits &= ~bits_to_wait_for;
    }
    pthread_mutex_unlock(&g->m);
    return out;
}

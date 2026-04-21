#pragma once

#include <pthread.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"

typedef void *SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_t *m = (pthread_mutex_t *)calloc(1, sizeof(*m));
    if (!m) {
        pthread_mutexattr_destroy(&attr);
        return NULL;
    }
    pthread_mutex_init(m, &attr);
    pthread_mutexattr_destroy(&attr);
    return (SemaphoreHandle_t)m;
}

static inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t h, TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
    if (!h) {
        return pdFALSE;
    }
    return (pthread_mutex_lock((pthread_mutex_t *)h) == 0) ? pdTRUE : pdFALSE;
}

static inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t h)
{
    if (!h) {
        return pdFALSE;
    }
    return (pthread_mutex_unlock((pthread_mutex_t *)h) == 0) ? pdTRUE : pdFALSE;
}

static inline void vSemaphoreDelete(SemaphoreHandle_t h)
{
    if (!h) {
        return;
    }
    pthread_mutex_destroy((pthread_mutex_t *)h);
    free(h);
}

#pragma once

#include <time.h>
#include "freertos/FreeRTOS.h"

static inline void vTaskDelay(TickType_t ticks)
{
    // In our emu config, 1 tick == 1 ms.
    const uint32_t ms = (uint32_t)ticks;
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000L);
    nanosleep(&ts, NULL);
}

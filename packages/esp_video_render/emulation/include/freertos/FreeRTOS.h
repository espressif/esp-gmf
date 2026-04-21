#pragma once

// Very small FreeRTOS compatibility layer for Linux emulation.

#include <stdint.h>
#include <stdbool.h>

typedef int BaseType_t;
typedef uint32_t TickType_t;

#define pdTRUE   1
#define pdFALSE  0

// 1ms tick for emulation
#define portTICK_PERIOD_MS  1
#define portMAX_DELAY       ((TickType_t)0xFFFFFFFFu)

static inline bool xPortInIsrContext(void)  {  return false;  }
#define portYIELD_FROM_ISR(x)  do {  \
    (void)(x);                       \
} while (0)

static BaseType_t xPortGetCoreID(void)  {  return 0;  }

// Many ESP-IDF headers assume these are transitively available from FreeRTOS.h.
#include "freertos/semphr.h"
#include "freertos/task.h"

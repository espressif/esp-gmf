/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include "esp_timer.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define LOG_LOCAL_LEVEL    ESP_LOG_INFO
#define CONFIG_LOG_COLORS  1

typedef enum {
    ESP_LOG_NONE,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE
} esp_log_level_t;

#define LOGOUT(tag, format, ...)  printf(format, (int)esp_timer_get_time() / 1000, tag, ##__VA_ARGS__);

#if CONFIG_LOG_COLORS
#define LOG_COLOR_BLACK   "30"
#define LOG_COLOR_RED     "31"
#define LOG_COLOR_GREEN   "32"
#define LOG_COLOR_BROWN   "33"
#define LOG_COLOR_BLUE    "34"
#define LOG_COLOR_PURPLE  "35"
#define LOG_COLOR_CYAN    "36"
#define LOG_COLOR(COLOR)  "\033[0;" COLOR "m"
#define LOG_BOLD(COLOR)   "\033[1;" COLOR "m"
#define LOG_RESET_COLOR   "\033[0m"
#define LOG_COLOR_E       LOG_COLOR(LOG_COLOR_RED)
#define LOG_COLOR_W       LOG_COLOR(LOG_COLOR_BROWN)
#define LOG_COLOR_I       LOG_COLOR(LOG_COLOR_GREEN)
#define LOG_COLOR_D
#define LOG_COLOR_V
#else
#define LOG_COLOR_E
#define LOG_COLOR_W
#define LOG_COLOR_I
#define LOG_COLOR_D
#define LOG_COLOR_V
#define LOG_RESET_COLOR
#endif  /* CONFIG_LOG_COLORS */

#define LOG_FORMAT(letter, format)  LOG_COLOR_##letter "[%d]" #letter " %s " format LOG_RESET_COLOR "\n"

#define ESP_LOGE(tag, format, ...)  if (LOG_LOCAL_LEVEL >= ESP_LOG_ERROR) {  \
    LOGOUT(tag, LOG_FORMAT(E, format), ##__VA_ARGS__);                       \
}

#define ESP_LOGW(tag, format, ...)  if (LOG_LOCAL_LEVEL >= ESP_LOG_WARN) {  \
    LOGOUT(tag, LOG_FORMAT(W, format), ##__VA_ARGS__);                      \
}

#define ESP_LOGI(tag, format, ...)  if (LOG_LOCAL_LEVEL >= ESP_LOG_INFO) {  \
    LOGOUT(tag, LOG_FORMAT(I, format), ##__VA_ARGS__);                      \
}

#define ESP_LOGD(tag, format, ...)  if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG) {  \
    LOGOUT(tag, LOG_FORMAT(D, format), ##__VA_ARGS__);                       \
}

#define ESP_LOGV(tag, format, ...)  if (LOG_LOCAL_LEVEL >= ESP_LOG_VERBOSE) {  \
    LOGOUT(tag, LOG_FORMAT(V, format), ##__VA_ARGS__);                         \
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */

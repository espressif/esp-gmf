/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_video_render_log.h"
#include "esp_video_render_types.h"
#include <stdio.h>
#include <stdarg.h>

static esp_video_render_log_level_t s_log_level = ESP_VIDEO_RENDER_LOG_LEVEL_INFO;

void esp_video_render_set_log_level(esp_video_render_log_level_t level)
{
    if (level > ESP_VIDEO_RENDER_LOG_LEVEL_VERBOSE) {
        return;
    }
    s_log_level = level;
}

esp_video_render_log_level_t esp_video_render_get_log_level(void)
{
    return s_log_level;
}

void video_render_printf(esp_video_render_log_level_t level, const char *format, ...)
{
    if (level > s_log_level) {
        return;  // Log level too high, skip
    }
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

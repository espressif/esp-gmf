/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Video render log levels
 */
typedef enum {
    ESP_VIDEO_RENDER_LOG_LEVEL_NONE    = 0,  /*!< No logs */
    ESP_VIDEO_RENDER_LOG_LEVEL_ERROR   = 1,  /*!< Error logs only */
    ESP_VIDEO_RENDER_LOG_LEVEL_WARN    = 2,  /*!< Warning and error logs */
    ESP_VIDEO_RENDER_LOG_LEVEL_INFO    = 3,  /*!< Info, warning and error logs */
    ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG   = 4,  /*!< Debug, info, warning and error logs */
    ESP_VIDEO_RENDER_LOG_LEVEL_VERBOSE = 5,  /*!< All logs */
} esp_video_render_log_level_t;

/**
 * @brief  Set video render log level
 *
 * @param[in]  level  Log level to set
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK  On success
 */
void esp_video_render_set_log_level(esp_video_render_log_level_t level);

/**
 * @brief  Get current video render log level
 *
 * @return
 *       - Current  log level
 */
esp_video_render_log_level_t esp_video_render_get_log_level(void);

/**
 * @brief  Video render printf function with log level control
 *
 * @param[in]  level     Log level for this message
 * @param[in]  format    Format string (printf-style)
 * @param[in]  Variable  arguments
 */
void video_render_printf(esp_video_render_log_level_t level, const char *format, ...);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

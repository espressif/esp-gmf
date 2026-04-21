/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  If user want to cancel measure meant just not call MEASURE_END
 */
#define MEASURE_BEGIN(module, func_fmt, ...)  video_render_measure_begin(module, func_fmt, ##__VA_ARGS__)
#define MEASURE_END(module, func_fmt, ...)    video_render_measure_end(module, func_fmt, ##__VA_ARGS__)

#define MEASURE_BLOCK(body, module, func_fmt, ...)  do {          \
    video_render_measure_begin(module, func_fmt, ##__VA_ARGS__);  \
    body;                                                         \
    video_render_measure_end(module, func_fmt, ##__VA_ARGS__);    \
} while (0)

/**
 * @brief  Enable video render measurement
 *
 * @param[in]  enable  Whether to enable measurement
 */
void video_render_measure_enable(bool enable);

/**
 * @brief  Begin video render measurement
 *
 * @param[in]  module    Module name
 * @param[in]  func_fmt  Function format string
 * @param[in]  Variable  arguments
 */
void video_render_measure_begin(const char *module, const char *func_fmt, ...);

/**
 * @brief  End video render measurement
 *
 * @param[in]  module    Module name
 * @param[in]  func_fmt  Function format string
 * @param[in]  Variable  arguments
 */
void video_render_measure_end(const char *module, const char *func_fmt, ...);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

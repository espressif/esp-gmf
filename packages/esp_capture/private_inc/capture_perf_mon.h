/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_timer.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Defines the buffer size for storing monitor data
 */
#define CAPTURE_PERF_MON_BUFF_SIZE (2048)

/**
 * @brief  Macro for measuring performance of a monitored procedure
 *
 * @note  If `CONFIG_ESP_CAPTURE_ENABLE_PERF_MON` is disabled, this macro does nothing
 */
#ifdef CONFIG_ESP_CAPTURE_ENABLE_PERF_MON
#define CAPTURE_PERF_MON(path, desc, body) do {                                                                \
    uint32_t _start_time = (uint32_t)(esp_timer_get_time() / 1000);                                            \
    body;                                                                                                      \
    capture_perf_monitor_add(path, desc, _start_time, (uint32_t)(esp_timer_get_time() / 1000) - _start_time);  \
} while (0)
#else
#define CAPTURE_PERF_MON(path, desc, body) body;
#endif  /* CONFIG_ESP_CAPTURE_ENABLE_PERF_MON */

/**
 * @brief  Enables or disables the performance monitor for capture
 *
 * @note  If enabled is set to `false`, the performance data will be printed before disabling the monitor
 *
 * @param[in]  enable  Flag indicating whether to enable the performance monitor (`true` to enable, `false` to disable)
 */
void capture_perf_monitor_enable(bool enable);

/**
 * @brief  Adds performance data to the monitor
 *
 * @param[in]  path        The path or module associated with the procedure
 * @param[in]  desc        A description of the procedure being monitored
 * @param[in]  start_time  The start time of the procedure
 * @param[in]  duration    The duration of the procedure
 */
void capture_perf_monitor_add(uint8_t path, const char *desc, uint32_t start_time, uint32_t duration);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

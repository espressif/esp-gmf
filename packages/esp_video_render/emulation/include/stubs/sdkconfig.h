/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

// Minimal sdkconfig for Linux emulation build.
// Only define what esp_video_render sources actually use.

#ifndef CONFIG_ESP_VIDEO_RENDER_BLEND_THREAD_PRIORITY
#define CONFIG_ESP_VIDEO_RENDER_BLEND_THREAD_PRIORITY  5
#endif  /* CONFIG_ESP_VIDEO_RENDER_BLEND_THREAD_PRIORITY */

#ifndef CONFIG_ESP_VIDEO_RENDER_BLEND_THREAD_CORE_ID
#define CONFIG_ESP_VIDEO_RENDER_BLEND_THREAD_CORE_ID  0
#endif  /* CONFIG_ESP_VIDEO_RENDER_BLEND_THREAD_CORE_ID */

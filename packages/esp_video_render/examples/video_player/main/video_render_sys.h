/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_video_render.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

int video_render_sys_create(uint8_t fps);
esp_video_render_handle_t video_render_sys_get(void);
void video_render_sys_destroy(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

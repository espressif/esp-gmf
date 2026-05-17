/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_gmf_element.h"
#include "player_sync.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define DEFAULT_PLAYER_VIDEO_RENDER_CONFIG()  {  \
    .render_handle  = NULL,                      \
    .width          = 0,                         \
    .height         = 0,                         \
    .display_width  = 0,                         \
    .display_height = 0,                         \
    .decoded_format = 0,                         \
    .fps            = 0,                         \
    .sync_handle    = NULL                       \
}

typedef struct {
    void                 *render_handle;  /*!< esp_video_render_handle_t from esp_video_render */
    uint16_t              width;
    uint16_t              height;
    uint16_t              display_width;
    uint16_t              display_height;
    uint32_t              decoded_format;
    uint32_t              fps;
    player_sync_handle_t  sync_handle;
} player_video_render_config_t;

esp_gmf_err_t player_video_render_init(player_video_render_config_t *config, esp_gmf_element_handle_t *handle);
esp_gmf_err_t player_video_render_flush_enable(esp_gmf_element_handle_t handle, bool enable);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

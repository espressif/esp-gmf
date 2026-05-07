/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include "esp_vui_widget.h"
#include "esp_vui_container.h"
#include "esp_video_render_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Initialize image widget
 *
 * @param[in]  container  Container handle
 * @param[in]  image      Image data
 * @param[in]  pos        Widget position (relative to container)
 *
 * @return
 *       - NULL    Not enough memory or invalid argument
 *       - Others  Widget pointer on success
 */
esp_vui_widget_t *esp_vui_image_widget_init(esp_vui_container_handle_t container,
                                            esp_video_render_img_t *image,
                                            const esp_video_render_pos_t *pos);

/**
 * @brief  Enable or disable transparent-color blending for image widget
 *
 * @note  When enabled, pixels around `trans_color` are treated as transparent.
 *
 * @param[in]  widget       Image widget handle
 * @param[in]  enable       True to enable transparent-color mode
 * @param[in]  trans_color  Transparent key color
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_image_widget_set_transparent_color(esp_vui_widget_t *widget,
                                                                  bool enable,
                                                                  esp_video_render_clr_t *trans_color);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

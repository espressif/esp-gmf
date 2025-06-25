
/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_capture.h"
#include "esp_capture_sink.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Capture path handle
 */
typedef struct capture_path_t *capture_path_handle_t;

/**
 * @brief  Get capture sink configuration use path handle
 *
 * @param[in]  path  Capture path
 *
 * @return
 *       - Capture sink configuration
 */
const esp_capture_sink_cfg_t *capture_path_get_sink_cfg(capture_path_handle_t path);

/**
 * @brief  Get path type use path handle
 *
 * @param[in]  path  Capture path
 *
 * @return
 *       - Capture path type
 */
uint8_t capture_path_get_path_type(capture_path_handle_t path);

/**
 * @brief  Capture path release share queue
 *
 * @param[in]  path   Capture path
 * @param[in]  frame  Frame to release
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Not supported
 */
esp_capture_err_t capture_path_release_share(capture_path_handle_t path, esp_capture_stream_frame_t *frame);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

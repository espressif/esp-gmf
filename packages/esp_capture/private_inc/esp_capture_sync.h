/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include "esp_capture_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Capture sync handle
 */
typedef void *esp_capture_sync_handle_t;

/**
 * @brief  Creates a synchronization handle for capture
 *
 * @param[in]   mode    The synchronization mode to be used
 * @param[out]  handle  Pointer to stored synchronization handle
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK           Success to create synchronization handle
 *       - ESP_CAPTURE_ERR_NO_MEM       No enough memory for synchronization instance
 *       - ESP_CAPTURE_ERR_INVALID_ARG  Input argument is invalid
 */
esp_capture_err_t esp_capture_sync_create(esp_capture_sync_mode_t mode, esp_capture_sync_handle_t *handle);

/**
 * @brief  Updates the synchronization time using audio current time
 *
 * @param[in]  handle   The synchronization handle
 * @param[in]  aud_pts  Current audio presentation timestamp
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK           Success to update audio current time
 *       - ESP_CAPTURE_ERR_INVALID_ARG  Input argument is invalid
 */
esp_capture_err_t esp_capture_sync_audio_update(esp_capture_sync_handle_t handle, uint32_t aud_pts);

/**
 * @brief  Turn on capture synchronization
 *
 * @param[in]  handle  The synchronization handle
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK           Success to turn on
 *       - ESP_CAPTURE_ERR_INVALID_ARG  Input argument is invalid
 */
esp_capture_err_t esp_capture_sync_on(esp_capture_sync_handle_t handle);

/**
 * @brief  Get capture synchronization mode
 *
 * @param[in]  handle  The synchronization handle
 *
 * @return
 *       - The current synchronization mode
 */
esp_capture_sync_mode_t esp_capture_sync_get_mode(esp_capture_sync_handle_t handle);

/**
 * @brief  Get current synchronization time of the capture system
 *
 * @param[in]   handle  The synchronization handle
 * @param[out]  pts     Pointer to synchronization time will be stored
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK           Success to get current synchronization time
 *       - ESP_CAPTURE_ERR_INVALID_ARG  Handle is invalid
 */
esp_capture_err_t esp_capture_sync_get_current(esp_capture_sync_handle_t handle, uint32_t *pts);

/**
 * @brief  Turn off capture synchronization
 *
 * @param[in]  handle  The synchronization handle
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK           Success to turn off
 *       - ESP_CAPTURE_ERR_INVALID_ARG  Handle is invalid
 */
esp_capture_err_t esp_capture_sync_off(esp_capture_sync_handle_t handle);

/**
 * @brief  Destroy the synchronization handle for the ESP capture system
 *
 * @param[in]  handle  The synchronization handle
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK           On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG  Handle is invalid
 */
esp_capture_err_t esp_capture_sync_destroy(esp_capture_sync_handle_t handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdbool.h>
#include "esp_gmf_element.h"
#include "esp_gmf_video_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Video PPA (Pixel Processing Accelerator) is a compact module
 *         Currently it only support on ESP32P4 and including following video processing:
 *         1) Color convert
 *         2) Scale
 *         3) Rotate
 *         5) Crop
 *         These operations can be mixed so that can do them all together for better performance
 */

/**
 * @brief  Initializes the GMF Video PPA
 *
 * @param[in]   config  No need to set
 * @param[out]  handle  Video PPA handle to store
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_video_ppa_init(void *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Set video PPA destination resolution
 *
 * @note  This API should only called before element running
 *
 * @param[in]  handle  Video PPA handle
 * @param[in]  res     Video PPA output resolution
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_video_ppa_set_dst_resolution(esp_gmf_element_handle_t handle, esp_gmf_video_resolution_t *res);

/**
 * @brief  Set video PPA destination format
 *
 * @note  This API should only called before element running
 *
 * @param[in]  handle  Video PPA handle
 * @param[in]  format  Video PPA destination format (GMF FourCC representation of video format)
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_video_ppa_set_dst_format(esp_gmf_element_handle_t handle, uint32_t format);

/**
 * @brief  Set video PPA cropped region
 *
 * @note  This API should only called before element running
 *
 * @param[in]  handle  Video PPA handle
 * @param[in]  rgn     Region to be kept in original video frame
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_video_ppa_set_cropped_rgn(esp_gmf_element_handle_t handle, esp_gmf_video_rgn_t *rgn);

/**
 * @brief  Set video PPA rotation degree
 *
 * @note  This API should only called before element running
 *
 * @param[in]  handle  Video PPA handle
 * @param[in]  degree  Currently only support (0, 90, 180, 270) unit(one degree)
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 */
esp_gmf_err_t esp_gmf_video_ppa_set_rotation(esp_gmf_element_handle_t handle, uint16_t degree);

/**
 * @brief  Enable full-speed PPA convert (async convert into an internal data queue)
 *
 * @note  Only allowed before element running / after stopped.
 *        Full-speed mode is intended when `vid_ppa` is the first element in the pipeline
 *        so the async task can freely acquire input frames.
 *        On open failure of the async path, the element falls back to normal mode.
 *
 * @param[in]  handle  Video PPA handle
 * @param[in]  enable  true to enable full-speed mode
 *
 * @return
 *       - ESP_GMF_ERR_OK              Success
 *       - ESP_GMF_ERR_INVALID_ARG     Invalid configuration provided
 *       - ESP_GMF_ERR_INVALID_STATE   Element is running or paused
 */
esp_gmf_err_t esp_gmf_video_ppa_set_full_speed(esp_gmf_element_handle_t handle, bool enable);

#ifdef __cplusplus
}
#endif

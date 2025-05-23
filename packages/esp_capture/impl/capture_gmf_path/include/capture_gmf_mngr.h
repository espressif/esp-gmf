/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "capture_pipeline_builder.h"
#include "esp_capture_path_mngr.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  This file provides template implementation of capture path manager using ESP-GMF framework
 *
 * @note  This path manager serves as:
 *        - Upper layer: Provides a complete capture path manager implementation
 *                       built on ESP-GMF infrastructure
 *        - Lower layer: Controls and configures ESP-GMF pipelines through the
 *                       pipeline builder interface (`esp_capture_pipeline_builder_if_t`)
 */

/**
 * @brief  Audio path manager configuration structure
 */
typedef struct {
    esp_capture_pipeline_builder_if_t  *pipeline_builder;  /*!< Audio pipeline builder interface */
} esp_capture_audio_path_mngr_cfg_t;

/**
 * @brief  Video path manager configuration structure
 */
typedef struct {
    esp_capture_pipeline_builder_if_t  *pipeline_builder;  /*!< Video pipeline builder interface */
} esp_capture_video_path_mngr_cfg_t;

/**
 * @brief  Create a new GMF audio path manager instance
 *
 * @note  If path manager is built outside capture, user need call `free` to free the memory occupied
 *        After unused by capture (`esp_capture_close` called)
 *
 * @param[in]  cfg  Audio path manager configuration
 *
 * @return
 *       - NULL    Not enough memory to create the manager
 *       - Others  Audio path manager instance
 */
esp_capture_audio_path_mngr_if_t *esp_capture_new_gmf_audio_mngr(esp_capture_audio_path_mngr_cfg_t *cfg);

/**
 * @brief  Create a new GMF video path manager instance
 *
 * @note  If path manager is built outside capture, user need call `free` to free the memory occupied
 *        After unused by capture (`esp_capture_close` called)
 *
 * @param[in]  cfg  Video path manager configuration
 *
 * @return
 *       - NULL    Not enough memory to create the manager
 *       - Others  Video path manager instance
 */
esp_capture_video_path_mngr_if_t *esp_capture_new_gmf_video_mngr(esp_capture_video_path_mngr_cfg_t *cfg);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

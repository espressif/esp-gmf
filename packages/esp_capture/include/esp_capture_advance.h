/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_capture_path_mngr.h"
#include "esp_capture_sink.h"
#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  This file support advanced capture control in 2 levels
 *         1. Use `esp_capture_open` to auto build capture system, but want to add extra control
 *           1.1 Use `esp_capture_register_element` to add customized process element
 *           1.2 Use `esp_capture_sink_build_pipeline` to customized the capture processing pipeline
 *           1.3 Use `esp_capture_sink_get_element_by_tag` to do extra element settings
 *         2. Use `esp_capture_advance_open` to buildup customized capture path manager
 *           2.1 Implement `esp_capture_path_mngr_if_t` for full capture path control
 *           2.2 Implement `esp_capture_pipeline_builder_if_t` to buildup customized pipeline
 *               And reuse `esp_capture_new_gmf_audio/video_path` to create capture path manager
 */

/**
 * @brief  Advanced configuration for capture
 */
typedef struct {
    esp_capture_sync_mode_t            sync_mode;   /*!< Capture sync mode */
    esp_capture_audio_path_mngr_if_t  *audio_path;  /*!< Audio path manager interface */
    esp_capture_video_path_mngr_if_t  *video_path;  /*!< Video path manager interface */
} esp_capture_advance_cfg_t;

/**
 * @brief  Register element to capture internal element pool
 *
 * @note  This API is specially used for users who what to use customized element with same capability as the default one
 *        Or add new element into pre-created internal pool
 *        The registered element can be used by multiple capture sink pipelines
 *        Only support to register when capture not started
 *        After register to pool, if return `ESP_CAPTURE_ERR_OK`, the ownership of the element is belong to capture
 *        User no need to do destroy, destroy will auto processed when call `esp_capture_close`
 *
 * @param[in]  capture      Capture handle
 * @param[in]  stream_type  Register element to which stream pool
 * @param[in]  element      Element handle
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             Register element success
 *       - ESP_CAPTURE_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_CAPTURE_ERR_INVALID_STATE  Not support register under this state
 */
esp_capture_err_t esp_capture_register_element(esp_capture_handle_t capture, esp_capture_stream_type_t stream_type,
                                               esp_gmf_element_handle_t element);

/**
 * @brief  Buildup capture process pipelines for capture sink
 *
 * @note  This API is specially used for users who what to customized processing pipeline for one sink in simple way
 *        User only need to provide the connection ship by element names (elements should be existed in pool)
 *        Only support to build pipeline before capture start (for sink only support build once)
 *        The built pipeline will be destroyed in `esp_capture_close`
 *
 * @param[in]  sink          Capture sink handle
 * @param[in]  stream_type   Stream type for the sink pipeline
 * @param[in]  element_tags  Elements tag name
 * @param[in]  element_num   Numbers of elements
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK           Buildup sink process pipeline success
 *       - ESP_CAPTURE_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_CAPTURE_ERR_NO_MEM       No enough memory
 *       - ESP_CAPTURE_ERR_NOT_FOUND    Element not find in capture element pool
 */
esp_capture_err_t esp_capture_sink_build_pipeline(esp_capture_sink_handle_t sink, esp_capture_stream_type_t stream_type,
                                                  const char **element_tags, uint8_t element_num);

/**
 * @brief  Get capture sink processing element handle by tag
 *
 * @note  User can use this API to get the processing element handle and do setting to it directly
 *
 * @param[in]  sink         Capture sink handle
 * @param[in]  stream_type  Stream type for the sink group
 * @param[in]  element_tag  Element tag name
 * @param[in]  element      Element handle to store
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK           Get element success
 *       - ESP_CAPTURE_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_CAPTURE_ERR_NOT_FOUND    Element not find in capture element pool
 */
esp_capture_err_t esp_capture_sink_get_element_by_tag(esp_capture_sink_handle_t sink,
                                                      esp_capture_stream_type_t stream_type,
                                                      const char *element_tag, esp_gmf_element_handle_t *element);

/**
 * @brief  Open capture in advanced mode
 *
 * @note  In advanced mode, user can implement their own `esp_capture_video_path_mngr_if_t`
 *        Or implement `esp_capture_pipeline_builder_if_t` for their owner pipeline structure
 *        So that can build up more complicated capture system
 *
 * @param[in]   cfg      Advanced capture configuration
 * @param[out]  capture  Pointer to output capture system handle
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK            Success to open capture
 *       - ESP_CAPTURE_ERR_INVALID_ARG   Invalid input argument
 *       - ESP_CAPTURE_ERR_NO_MEM        No enough memory for capture instance
 *       - ESP_CAPTURE_ERR_NO_RESOURCES  No related resources
 */
esp_capture_err_t esp_capture_advance_open(esp_capture_advance_cfg_t *cfg, esp_capture_handle_t *capture);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

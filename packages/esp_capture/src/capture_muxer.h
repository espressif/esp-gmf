/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "capture_path.h"
#include "msg_q.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Mux path handle
 */
typedef struct capture_muxer_path_t *capture_muxer_path_handle_t;

/**
 * @brief  Open muxer for capture
 *
 * @param[in]  path       Capture path handle
 * @param[in]  muxer_cfg  Muxer configuration
 * @param[in]  h          Muxer path handle to store
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_NO_MEM         Not enough memory
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Path interface not provided
 */
esp_capture_err_t capture_muxer_open(capture_path_handle_t path, esp_capture_muxer_cfg_t *muxer_cfg,
                                     capture_muxer_path_handle_t *h);

/**
 * @brief  Prepare capture muxer
 *
 * @param[in]  muxer  Muxer path handle
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK      On success
 *       - ESP_CAPTURE_ERR_NO_MEM  Not enough memory
 */
esp_capture_err_t capture_muxer_prepare(capture_muxer_path_handle_t muxer);

/**
 * @brief  Get muxer output message queue handle
 *
 * @param[in]  muxer  Muxer path handle
 *
 * @return
 *       - NULL    Muxer queue not existed
 *       - Others  Message queue handle
 */
msg_q_handle_t capture_muxer_get_muxer_q(capture_muxer_path_handle_t muxer);

/**
 * @brief  Enable muxer for capture
 *
 * @param[in]  muxer   Muxer path handle
 * @param[in]  enable  Whether enable muxer path or not
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK            On success
 *       - ESP_CAPTURE_ERR_NO_RESOURCES  No resources
 */
esp_capture_err_t capture_muxer_enable(capture_muxer_path_handle_t muxer, bool enable);

/**
 * @brief  Disable streaming output for muxer
 *
 * @param[in]  muxer  Muxer path handle
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK            On success
 *       - ESP_CAPTURE_ERR_NO_RESOURCES  No resources
 */
esp_capture_err_t capture_muxer_disable_streaming(capture_muxer_path_handle_t muxer);

/**
 * @brief  Start muxer path
 *
 * @param[in]  muxer  Muxer path handle
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK            On success
 *       - ESP_CAPTURE_ERR_NO_RESOURCES  No resources
 */
esp_capture_err_t capture_muxer_start(capture_muxer_path_handle_t muxer);

/**
 * @brief  Check muxer path prepared for certain stream type
 *
 * @param[in]  muxer        Muxer path handle
 * @param[in]  stream_type  Stream type
 *
 * @return
 *       - true   Ready to receive for the stream type
 *       - false  Not prepared or prepared fail
 */
bool capture_muxer_stream_prepared(capture_muxer_path_handle_t muxer, esp_capture_stream_type_t stream_type);

/**
 * @brief  Acquire frame from muxer path
 *
 * @param[in]  muxer    Muxer path handle
 * @param[in]  frame    Frame data
 * @param[in]  no_wait  Whether receive without wait
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  No supported
 *       - ESP_CAPTURE_ERR_NOT_FOUND      No found
 */
esp_capture_err_t capture_muxer_acquire_frame(capture_muxer_path_handle_t muxer, esp_capture_stream_frame_t *frame, bool no_wait);

/**
 * @brief  Release frame from muxer path
 *
 * @param[in]  muxer  Muxer path handle
 * @param[in]  frame  Frame data
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  No supported
 */
esp_capture_err_t capture_muxer_release_frame(capture_muxer_path_handle_t muxer, esp_capture_stream_frame_t *frame);

/**
 * @brief  Stop muxer path
 *
 * @param[in]  muxer  Muxer path handle
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK  On success
 */
esp_capture_err_t capture_muxer_stop(capture_muxer_path_handle_t muxer);

/**
 * @brief  Close muxer path
 *
 * @param[in]  muxer  Muxer path handle
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK  On success
 */
esp_capture_err_t capture_muxer_close(capture_muxer_path_handle_t muxer);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

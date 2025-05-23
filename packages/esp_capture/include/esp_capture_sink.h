/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_capture_types.h"
#include "esp_capture_audio_src_if.h"
#include "esp_capture_video_src_if.h"
#include "esp_capture_path_mngr.h"
#include "esp_capture.h"
#include "esp_muxer.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Capture sink handle
 *
 * @note  A capture may contain multiple capture sinks
 *        Each sink can have its special audio and video settings
 *        Each sink can be configured to connect to muxer or not
 */
typedef void *esp_capture_sink_handle_t;

/**
 * @brief  Capture sink configuration
 */
typedef struct {
    esp_capture_audio_info_t  audio_info;  /*!< Audio sink information */
    esp_capture_video_info_t  video_info;  /*!< Video sink information */
} esp_capture_sink_cfg_t;

/**
 * @brief  Capture run mode
 *
 * @note  Capture run mode is used to control capture sink run behavior
 */
typedef enum {
    ESP_CAPTURE_RUN_MODE_DISABLE = 0,  /*!< Disable capture sink, not run anymore */
    ESP_CAPTURE_RUN_MODE_ALWAYS  = 1,  /*!< Enable capture sink, run always */
    ESP_CAPTURE_RUN_MODE_ONESHOT = 2   /*!< Enable capture once, use scenario like capture one image */
} esp_capture_run_mode_t;

/**
 * @brief  Capture muxer mask
 *
 * @note  Capture muxer mask is used control whether enable audio or video muxer
 */
typedef enum {
    ESP_CAPTURE_MUXER_MASK_ALL   = 0,  /*!< Mux for both audio and video */
    ESP_CAPTURE_MUXER_MASK_AUDIO = 1,  /*!< Mux for audio stream only */
    ESP_CAPTURE_MUXER_MASK_VIDEO = 2,  /*!< Mux for video stream only */
} esp_capture_muxer_mask_t;

/**
 * @brief  Muxer configuration
 *
 * @note  This structure wrapper for configuration for `esp_muxer`
 *        Meanwhile provide filter function to only mux certain stream
 */
typedef struct {
    esp_muxer_config_t       *base_config;  /*!< Base muxer configuration */
    uint32_t                  cfg_size;     /*!< Actual muxer configuration size
                                                 Example: For MP4 muxer, use `sizeof(mp4_muxer_config_t)`*/
    esp_capture_muxer_mask_t  muxer_mask;   /*!< Specifying which streams to mux (select audio/video/both) */
} esp_capture_muxer_cfg_t;

/**
 * @brief  Setup capture sink to use certain sink settings
 *
 * @note  Only support do setup when capture not started (`esp_capture_start` not called yet)
 *        Setup to an existed path will get the existed sink handle
 *
 * @param[in]   capture      Capture handle
 * @param[in]   sink_idx     Sink index
 * @param[in]   sink_info    Sink setting for audio and video stream
 * @param[out]  sink_handle  Pointer to output capture sink handle
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_CAPTURE_ERR_NO_MEM         No enough memory for capture instance
 *       - ESP_CAPTURE_ERR_INVALID_STATE  Path already added
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Not supported to do path setup (path interface not provided)
 */
esp_capture_err_t esp_capture_sink_setup(esp_capture_handle_t capture, uint8_t sink_idx,
                                         esp_capture_sink_cfg_t *sink_info, esp_capture_sink_handle_t *sink_handle);

/**
 * @brief  Add a muxer to the capture sink
 *
 * @note  This function must be called before starting the capture
 *        Only one muxer can be added per capture sink
 *
 * @param[in]  sink       Handle of the capture sink to which the muxer will be added
 * @param[in]  muxer_cfg  Pointer to muxer configuration
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG    Invalid input argument (null pointer or invalid size)
 *       - ESP_CAPTURE_ERR_INVALID_STATE  Muxer already added or capture already started
 */
esp_capture_err_t esp_capture_sink_add_muxer(esp_capture_sink_handle_t sink, esp_capture_muxer_cfg_t *muxer_cfg);

/**
 * @brief  Add overlay to capture sink
 *
 * @param[in]  sink     Capture sink handle
 * @param[in]  overlay  Overlay interface
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Path interface not provided
 */
esp_capture_err_t esp_capture_sink_add_overlay(esp_capture_sink_handle_t sink, esp_capture_overlay_if_t *overlay);

/**
 * @brief  Enable muxer for capture sink
 *
 * @param[in]  sink    Capture sink handle
 * @param[in]  enable  Enable muxer or not
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Path interface not provided
 */
esp_capture_err_t esp_capture_sink_enable_muxer(esp_capture_sink_handle_t sink, bool enable);

/**
 * @brief  Enable overlay for capture sink
 *
 * @note  User can enable overlay at any time, even after `esp_capture_start`
 *        When disabled, video frame will not mixed with overlay frame any more
 *
 * @param[in]  sink    Capture sink handle
 * @param[in]  enable  Enable overlay or not
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Path interface not provided
 */
esp_capture_err_t esp_capture_sink_enable_overlay(esp_capture_sink_handle_t sink, bool enable);

/**
 * @brief  Enable capture sink
 *
 * @note  User can enable capture sink at any time, even after `esp_capture_start`
 *
 * @param[in]  sink      Capture sink handle
 * @param[in]  run_type  Run type for capture sink
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Path interface not provided
 */
esp_capture_err_t esp_capture_sink_enable(esp_capture_sink_handle_t sink, esp_capture_run_mode_t run_type);

/**
 * @brief  Disables capture sink output for a specified stream type
 *
 * @note  By default, all streams in the sink are output (if supported)
 *        This API provides static control to disable a stream - once disabled,
 *        it cannot be re-enabled without reconfiguring the sink using `esp_capture_sink_setup`
 *        This API must be called before capture starts
 *
 * @note  Typical use cases:
 *        - Muxer-only applications where the user doesn't need to fetch audio/video stream data,
 *          but only wants to store it in a file
 *
 * @param[in]  sink         Handle of the capture sink
 * @param[in]  stream_type  Type of the capture stream to disable
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_CAPTURE_ERR_INVALID_STATE  Capture is already running
 */
esp_capture_err_t esp_capture_sink_disable_stream(esp_capture_sink_handle_t sink, esp_capture_stream_type_t stream_type);

/**
 * @brief  Set stream bitrate for capture sink
 *
 * @param[in]  h            Capture sink handle
 * @param[in]  stream_type  Capture stream type
 * @param[in]  bitrate      Stream bitrate to set
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Path interface not provided
 */
esp_capture_err_t esp_capture_sink_set_bitrate(esp_capture_sink_handle_t h, esp_capture_stream_type_t stream_type,
                                               uint32_t bitrate);

/**
 * @brief  Acquire stream data from capture sink
 *
 * @note  Stream data is internally managed by capture, user need not provide memory to hold it
 *        Meanwhile after use done, must call `esp_capture_release_stream` to release stream data
 *        User need set `frame->stream_type` to specify which stream type to acquire
 *
 * @param[in]      sink     Capture sink handle
 * @param[in,out]  frame    Capture frame information
 * @param[in]      no_wait  Whether need wait for frame data, set to `true` to not wait
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Path interface not provided
 */
esp_capture_err_t esp_capture_sink_acquire_frame(esp_capture_sink_handle_t sink, esp_capture_stream_frame_t *frame,
                                                 bool no_wait);

/**
 * @brief  Release stream data from capture sink
 *
 * @note  User need make sure frame data, size, stream_type is same as the one acquired from `esp_capture_sink_acquire_frame`
 *
 * @param[in]  sink   Capture sink handle
 * @param[in]  frame  Capture frame information
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Wrong stream type
 *       - ESP_CAPTURE_ERR_INVALID_STATE  Capture sink not enable yet
 */
esp_capture_err_t esp_capture_sink_release_frame(esp_capture_sink_handle_t sink, esp_capture_stream_frame_t *frame);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

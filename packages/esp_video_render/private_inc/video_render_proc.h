/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_video_render_types.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Video render process type
 *
 * @note  Users can add customized process elements into audio render process
 *        Then can get element handle through `esp_video_render_stream_get/mixed_element` and do extra settings
 *        Definition of process types are aligned with https://github.com/espressif/esp-gmf/blob/main/gmf_core/helpers/include/esp_gmf_caps_def.h
 */
typedef enum {
    ESP_VIDEO_RENDER_PROC_NONE   = 0,
    ESP_VIDEO_RENDER_PROC_DEC    = 0x0000434544444956ULL,  /*!< Video decoder process */
    ESP_VIDEO_RENDER_PROC_ENC    = 0x0000434E45444956ULL,  /*!< Video encoder process */
    ESP_VIDEO_RENDER_PROC_CCVT   = 0x0054564343444956ULL,
    ESP_VIDEO_RENDER_PROC_CROP   = 0x00504F5243444956ULL,
    ESP_VIDEO_RENDER_PROC_ROTATE = 0x4554415452444956ULL,
    ESP_VIDEO_RENDER_PROC_SCALE  = 0x454C414353444956ULL,
    ESP_VIDEO_RENDER_PROC_OVLY   = 0x00594C564F444956ULL,
    ESP_VIDEO_RENDER_PROC_FPS    = 0x0000535046444956ULL
} esp_video_render_proc_type_t;

/**
 * @brief  Video render processor handle
 */
typedef void *video_render_proc_handle_t;

/**
 * @brief  Video render write callback
 *
 * @param[in]  frame  Frame to write
 * @param[in]  ctx    User context
 *
 * @return
 *       - 0       On success
 *       - Others  Error code
 */
typedef int (*esp_video_render_write_cb_t)(esp_video_render_frame_t *frame, void *ctx);

/**
 * @brief  Create video render processor
 *
 * @param[in]   pool  GMF pool handle
 * @param[out]  proc  Video render processor handle to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM       Not enough memory
 */
esp_video_render_err_t video_render_proc_create(esp_gmf_pool_handle_t pool, video_render_proc_handle_t *proc);

/**
 * @brief  Add processor element to video render processor
 *
 * @param[in]  proc       Video processor handle
 * @param[in]  proc_type  Array of processor types
 * @param[in]  proc_num   Number of processors
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK             On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM         Not enough memory
 *       - ESP_VIDEO_RENDER_ERR_INVALID_STATE  Not allowed to add when running
 */
esp_video_render_err_t video_render_proc_add(video_render_proc_handle_t proc, esp_video_render_proc_type_t *proc_type,
                                             uint8_t proc_num);

/**
 * @brief  Open video processor
 *
 * @param[in]  proc            Video processor handle
 * @param[in]  in_frame_info   Input frame information
 * @param[in]  out_frame_info  Output frame information
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK             On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_RESOURCE    Not enough resource
 *       - ESP_VIDEO_RENDER_ERR_INVALID_STATE  Not allowed to open when running
 */
esp_video_render_err_t video_render_proc_open(video_render_proc_handle_t proc,
                                              esp_video_render_frame_info_t *in_frame_info,
                                              esp_video_render_frame_info_t *out_frame_info);

/**
 * @brief  Get element handle from video processor by processor type
 *
 * @param[in]  proc  Video processor handle
 * @param[in]  type  Video processor type
 *
 * @return
 *       - NULL    Element does not exist
 *       - Others  Video processor element handle
 */
esp_gmf_element_handle_t video_render_proc_get_element(video_render_proc_handle_t proc, esp_video_render_proc_type_t type);

/**
 * @brief  Get output frame information
 *
 * @param[in]   proc  Video processor handle
 * @param[out]  info  Output frame information to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t video_render_proc_get_out_frame_info(video_render_proc_handle_t proc, esp_video_render_frame_info_t *info);
/**
 * @brief  Set output writer for video processor
 *
 * @param[in]  proc    Video processor handle
 * @param[in]  writer  Output writer callback
 * @param[in]  ctx     Writer context
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t video_render_proc_set_writer(video_render_proc_handle_t proc, esp_video_render_write_cb_t writer, void *ctx);

/**
 * @brief  Set output port callback for video processor
 *
 * @param[in]  handle   Video processor handle
 * @param[in]  acquire  Callback to acquire output buffer
 * @param[in]  release  Callback to release output buffer
 * @param[in]  ctx      Writer context
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t video_render_proc_set_out_port(video_render_proc_handle_t handle, port_acquire acquire,
                                                      port_release release, void *ctx);

/**
 * @brief  Write frame data into video processor
 *
 * @note  Currently all processor runs in input context
 *
 * @param[in]  proc   Video processor handle
 * @param[in]  frame  Frame data to write
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK             On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_INVALID_STATE  Processor not in valid state
 *       - ESP_VIDEO_RENDER_ERR_FAIL           Failed to process data
 */
esp_video_render_err_t video_render_proc_write(video_render_proc_handle_t proc, esp_video_render_frame_t *frame);

/**
 * @brief  Close video processor
 *
 * @param[in]  proc  Video processor handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t video_render_proc_close(video_render_proc_handle_t proc);

/**
 * @brief  Destroy video processor
 *
 * @param[in]  proc  Video processor handle
 */
void video_render_proc_destroy(video_render_proc_handle_t proc);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

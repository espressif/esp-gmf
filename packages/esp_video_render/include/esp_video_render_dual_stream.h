/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_video_render_types.h"
#include "esp_video_render.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Dual stream is a special video render application
 *         It support decodes 2 video streams and render on dual or one display
 *         When stream output resolution is same as display, it will use display frame buffer directly
 *         When render async, an extra cache buffer will be added to balance decode and render speed
 */

/**
 * @brief  Configuration for dual-stream video rendering
 *
 * @note  Supports displaying dual stream on one or multiple displays (per `render` setting)
 *        - Creates one video render stream per eye
 *        - Uses a dedicated decode task for frame processing
 *        - Buffering controlled by `frame_count * max_frame_size`
 *        - When `fps > 0`, enforces precise frame rate control
 *        - For high FPS, enables parallel render/decode when `render_async = true`
 *          especiallly when rendering is time consuming
 */
typedef struct {
    esp_video_render_handle_t    render[2];       /*!< Render handles for each stream */
    esp_video_render_task_cfg_t  task_cfg[2];     /*!< Task configuration for async decode tasks */
    uint32_t                     frame_count;     /*!< Frame buffer capacity (count) */
    uint32_t                     max_frame_size;  /*!< Maximum single frame size (bytes), default is 20KB */
    uint8_t                      fps;             /*!< Target FPS (0 = unlimited) */
    bool                         render_async;    /*!< Enable parallel render while decoding */
} esp_video_render_dual_stream_cfg_t;

/**
 * @brief  Video render dual stream handle
 */
typedef void *esp_video_render_dual_stream_handle_t;

/**
 * @brief  Open dual stream video render
 *
 * @param[in]   cfg     Dual stream configuration
 * @param[out]  handle  Dual stream handle to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM       Not enough memory
 */
esp_video_render_err_t esp_video_render_dual_stream_open(esp_video_render_dual_stream_cfg_t *cfg, esp_video_render_dual_stream_handle_t *handle);

/**
 * @brief  Set display rectangle for eye
 *
 * @param[in]  handle      Dual stream handle
 * @param[in]  stream_idx  Stream index (0 for first stream, 1 for second stream)
 * @param[in]  rect        Display rectangle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_dual_stream_set_display_rect(esp_video_render_dual_stream_handle_t handle,
                                                                     uint8_t stream_idx,
                                                                     esp_video_render_rect_t *rect);

/**
 * @brief  Get buffer for eye
 *
 * @param[in]   handle      Dual stream handle
 * @param[in]   stream_idx  Stream index (0 for first stream, 1 for second stream)
 * @param[out]  frame       Frame buffer to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_RESOURCE  No buffer available
 */
esp_video_render_err_t esp_video_render_dual_stream_get_buffer(esp_video_render_dual_stream_handle_t handle,
                                                               uint8_t stream_idx,
                                                               esp_video_render_frame_t *frame);

/**
 * @brief  Send buffers for both streams
 *
 * @param[in]  handle   Dual stream handle
 * @param[in]  frame_a  Frame buffer for first stream
 * @param[in]  frame_b  Frame buffer for second stream
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK             On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_INVALID_STATE  Invalid state
 */
esp_video_render_err_t esp_video_render_dual_stream_send_buffer(esp_video_render_dual_stream_handle_t handle,
                                                                esp_video_render_frame_t *frame_a,
                                                                esp_video_render_frame_t *frame_b);

/**
 * @brief  Release buffer for stream
 *
 * @param[in]  handle      Dual stream handle
 * @param[in]  stream_idx  Stream index (0 for first stream, 1 for second stream)
 * @param[in]  frame       Frame buffer to release
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_dual_stream_release_buffer(esp_video_render_dual_stream_handle_t handle,
                                                                   uint8_t stream_idx,
                                                                   esp_video_render_frame_t *frame);

/**
 * @brief  Close dual stream video render
 *
 * @param[in]  handle  Dual stream handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_dual_stream_close(esp_video_render_dual_stream_handle_t handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

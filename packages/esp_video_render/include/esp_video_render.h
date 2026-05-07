/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_video_render_types.h"
#include "esp_video_render_backend.h"
#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Video render configuration
 */
typedef struct {
    void    *pool;  /*!< GMF elements pool */
    uint8_t  fps;   /*!< Video render FPS */
} esp_video_render_cfg_t;

/**
 * @brief  Video render handle
 *
 * @note  Video render handle means handle to an video render system
 *        Video render system hierarchy:
 *          - Only has one render backend (allowed to change when no stream is opened)
 *          - One video render system can have multiple streams
 *          - Each stream can have one streaming input (optional)
 *          - Each stream can have one overlay (optional)
 *          - Each stream can have at least one streaming input or one overlay
 *          - Each overlay can have one or multiple containers
 *          - Each container can have one or multiple widgets
 */
typedef void *esp_video_render_handle_t;

/**
 * @brief  Video render stream handle
 */
typedef void *esp_video_render_stream_handle_t;

/**
 * @brief  Video render backend configuration
 */
typedef struct {
    const esp_video_render_backend_ops_t *ops;       /*!< Backend operations */
    void                                 *cfg;       /*!< Backend configuration */
    int                                   cfg_size;  /*!< Configuration size */
} esp_video_render_backend_cfg_t;

/**
 * @brief  Video render stream information
 */
typedef struct {
    esp_video_render_frame_info_t  info;    /*!< Frame information */
    bool                           cached;  /*!< Whether to use cached buffer
                                                 Turn on cache will waste some memory
                                                 When streaming FPS and render FPS mismatched
                                                 Use cached buffer to avoid processor and renderer waiting for each other */
} esp_video_render_stream_info_t;

/**
 * @brief  Video render event type
 *
 * @note  Render events will help to check whether render is actually running
 *        So that when no render stream is running, the device can be suspended to save energy
 */
typedef enum {
    ESP_VIDEO_RENDER_EVENT_TYPE_NONE   = 0,  /*!< No event */
    ESP_VIDEO_RENDER_EVENT_TYPE_OPENED = 1,  /*!< Render is opened (at least one stream is opened) */
    ESP_VIDEO_RENDER_EVENT_TYPE_CLOSED = 2,  /*!< Render is closed (all streams are closed) */
    ESP_VIDEO_RENDER_EVENT_TYPE_VSYNC  = 3,  /*!< Indication for draw finished */
} esp_video_render_event_type_t;

/**
 * @brief  Video render event callback
 *
 * @param[in]  event_type  Video render event type
 * @param[in]  ctx         Event context
 *
 * @return
 *       - 0       On success
 *       - Others  Failed to handle event
 */
typedef int (*esp_video_render_event_cb_t)(esp_video_render_event_type_t event_type, void *ctx);

/**
 * @brief  Create video render
 *
 * @param[in]   cfg     Video render configuration
 * @param[out]  render  Video render handle to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM       Not enough memory
 */
esp_video_render_err_t esp_video_render_create(esp_video_render_cfg_t *cfg, esp_video_render_handle_t *render);

/**
 * @brief  Reconfigures the video render task parameters
 *
 * @note  This function must be called when no video stream is active (no stream is opened yet)
 *        No need to set if blend in sync mode
 *        When not called, will use default configuration inherit from Kconfig
 *
 * @param[in]  render    Video render handle
 * @param[in]  task_cfg  Pointer to task configuration
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK             On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_INVALID_STATE  Called when any stream is opened
 */
esp_video_render_err_t esp_video_render_task_reconfigure(esp_video_render_handle_t render,
                                                         esp_video_render_task_cfg_t *task_cfg);

/**
 * @brief  Set event callback for video render
 *
 * @param[in]  render    Video render handle
 * @param[in]  event_cb  Video render event callback
 * @param[in]  ctx       User context
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_set_event_cb(esp_video_render_handle_t render,
                                                     esp_video_render_event_cb_t event_cb, void *ctx);
/**
 * @brief  Set display backend for video render
 *
 * @note  Only allow to set when no streaming is opened yet or all streams are closed
 *        Allow overwrite the existed backend, when overwrite, the existed backend will be deinitialized
 *
 * @param[in]  render   Video render handle
 * @param[in]  backend  Backend configuration
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM       Not enough memory
 */
esp_video_render_err_t esp_video_render_set_display(esp_video_render_handle_t render,
                                                    esp_video_render_backend_cfg_t *backend);

/**
 * @brief  Get display information
 *
 * @param[in]   render   Video render handle
 * @param[out]  display  Display information to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_get_display_info(esp_video_render_handle_t render,
                                                         esp_video_render_disp_info_t *display);

/**
 * @brief  Set background image for video render
 *
 * @note  Image can either be encoded or raw data
 *         - When raw data is provided by user make sure it is valid during the whole video render process
 *         - When image is encoded, it will be decoded in sync when this API is called
 *           The decoded data will not freed until new background is set or video render is destroyed
 *
 * @param[in]  render  Video render handle
 * @param[in]  img     Background image (set NULL to clear background)
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM       Not enough memory
 */
esp_video_render_err_t esp_video_render_set_bg_image(esp_video_render_handle_t render,
                                                     esp_video_render_img_t *img);

/**
 * @brief  Set background color for video render
 *
 * @param[in]  render  Video render handle
 * @param[in]  color   Background color
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_set_bg_color(esp_video_render_handle_t render,
                                                     esp_video_render_clr_t *color);

/**
 * @brief  Open video render stream
 *
 * @note  If `stream_info->fps` is non-zero,
 *        video renderer will throttle the input rate to stay at or below the specified FPS
 *
 * @param[in]   render       Video render handle
 * @param[in]   stream_info  Stream input information
 * @param[out]  stream       Stream handle to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK             On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED  Not supported for backend not ready
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM         Not enough memory
 */
esp_video_render_err_t esp_video_render_stream_open(esp_video_render_handle_t render,
                                                    esp_video_render_stream_info_t *stream_info,
                                                    esp_video_render_stream_handle_t *stream);

/**
 * @brief  Set asynchronous rendering for stream
 *
 * @note  In default if only one stream exists when call `esp_video_render_write_xx`
 *        It will render in current task to save resource
 *        To force render in other task, call this API:
 *          - Once render in async mode, it will keep rendering in async mode until all streams are closed
 *          - The async render behavior is auto clearup after all streams are closed also
 *
 * @param[in]  stream  Stream handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_RESOURCE  No resource for render task
 */
esp_video_render_err_t esp_video_render_stream_render_async(esp_video_render_stream_handle_t stream);

/**
 * @brief  Get overlay handle for stream
 *
 * @note  Overlay is binded to a stream, if stream has video it will blend on top of it
 *        In this ways overlay is not changed until video is changed
 *
 *        Overlay can also acquired even no video is being wrote
 *        It is a general overlay that can blend onto final frame buffer directly
 *
 * @param[in]   stream   Stream handle
 * @param[out]  overlay  Overlay handle to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM       Not enough memory
 */
esp_video_render_err_t esp_video_render_stream_get_overlay(esp_video_render_stream_handle_t stream,
                                                           esp_vui_overlay_handle_t *overlay);

/**
 * @brief  Set source rectangle for stream
 *
 * @param[in]  stream    Stream handle
 * @param[in]  src_rect  Source rectangle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_stream_set_src_rect(esp_video_render_stream_handle_t stream,
                                                            esp_video_render_rect_t *src_rect);

/**
 * @brief  Set display rectangle for stream
 *
 * @param[in]  stream     Stream handle
 * @param[in]  disp_rect  Display rectangle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_stream_set_disp_rect(esp_video_render_stream_handle_t stream,
                                                             esp_video_render_rect_t *disp_rect);

/**
 * @brief  Set z-order (layer order) for stream
 *
 * @param[in]  stream  Stream handle
 * @param[in]  zorder  Z-order value (higher value renders on top)
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_stream_set_zorder(esp_video_render_stream_handle_t stream,
                                                          uint8_t zorder);

/**
 * @brief  Set rotation angle for stream
 *
 * @param[in]  stream  Stream handle
 * @param[in]  degree  Rotation angle in degrees (0, 90, 180, 270)
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK             On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED  Rotation angle not supported
 */
esp_video_render_err_t esp_video_render_stream_set_rotate(esp_video_render_stream_handle_t stream,
                                                          int16_t degree);

/**
 * @brief  Set visibility for stream
 *
 * @param[in]  stream   Stream handle
 * @param[in]  visible  True to make stream visible, false to hide
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_stream_set_visible(esp_video_render_stream_handle_t stream,
                                                           bool visible);

/**
 * @brief  Set alpha transparency for stream
 *
 * @param[in]  handle  Stream handle
 * @param[in]  alpha   Alpha value (0-255, 0=transparent, 255=opaque)
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_stream_set_alpha(esp_video_render_stream_handle_t handle,
                                                         uint8_t alpha);

/**
 * @brief  Acquire frame buffer for stream
 *
 * @note  Frame buffer is globally shared by all streams (resources in backend)
 *        Not recommended for share use for multiple streams
 *        Once acquired OK, must use `esp_video_render_stream_release_fb` to release
 *
 * @param[in]   stream  Stream handle
 * @param[out]  fb      Frame buffer to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_RESOURCE  No frame buffer available
 */
esp_video_render_err_t esp_video_render_stream_acquire_fb(esp_video_render_stream_handle_t stream,
                                                          esp_video_render_fb_t *fb);

/**
 * @brief  Write frame buffer to stream
 *
 * @param[in]  stream  Stream handle
 * @param[in]  fb      Frame buffer to write
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK             On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_INVALID_STATE  Stream not in valid state
 */
esp_video_render_err_t esp_video_render_stream_write_fb(esp_video_render_stream_handle_t stream,
                                                        esp_video_render_fb_t *fb);

/**
 * @brief  Release frame buffer for stream
 *
 * @param[in]  stream  Stream handle
 * @param[in]  fb      Frame buffer to release
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_stream_release_fb(esp_video_render_stream_handle_t stream,
                                                          esp_video_render_fb_t *fb);

/**
 * @brief  Lock stream for thread-safe access
 *
 * @note  When video frame data is frame user and maybe overwrote to protect the memory consistence
 *        User need call this API to lock the stream buffer,
 *        changed it then call `esp_video_render_stream_unlock` to unlock
 *
 * @param[in]  stream  Stream handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_TIMEOUT      Lock timeout
 */
esp_video_render_err_t esp_video_render_stream_lock(esp_video_render_stream_handle_t stream);

/**
 * @brief  Write frame to stream
 *
 * @param[in]  stream  Stream handle
 * @param[in]  frame   Frame to write
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK             On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_INVALID_STATE  Stream not in valid state
 */
esp_video_render_err_t esp_video_render_stream_write(esp_video_render_stream_handle_t stream,
                                                     esp_video_render_frame_t *frame);

/**
 * @brief  Unlock stream
 *
 * @param[in]  stream  Stream handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_stream_unlock(esp_video_render_stream_handle_t stream);

/**
 * @brief  Lock stream for composition (thread-safe)
 *
 * @note  When overlay existed, may be multiple widgets updated
 *        To protect the widget update consistency, user need lock the stream for composition
 *        Lock at once and update all widgets all together can avoid partial render which not expected
 *
 * @param[in]  stream  Stream handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_TIMEOUT      Lock timeout
 */
esp_video_render_err_t esp_video_render_stream_compose_lock(esp_video_render_stream_handle_t stream);

/**
 * @brief  Unlock stream composition lock
 *
 * @param[in]  stream  Stream handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_stream_compose_unlock(esp_video_render_stream_handle_t stream);

/**
 * @brief  Close video render stream
 *
 * @param[in]  stream  Stream handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_stream_close(esp_video_render_stream_handle_t stream);

/**
 * @brief  Get blender handle for render usage
 *
 * @param[in]   render   Video render handle
 * @param[out]  blender  Blender handle to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_get_blender(esp_video_render_handle_t render,
                                                    esp_video_render_blend_handle_t *blender);

/**
 * @brief  Get GMF elements pool handle
 *
 * @param[in]   render  Video render handle
 * @param[out]  pool    Pool handle to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_get_pool(esp_video_render_handle_t render, void **pool);

/**
 * @brief  Destroy video render
 *
 * @note  Do not call any render related API after render is destroyed
 *
 * @param[in]  render  Video render handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_destroy(esp_video_render_handle_t render);

/**
 * @brief  Enable video render measurement
 *
 * @note  Measurement will help to analyze the performance of video render
 *        It will print the measure results when disabled
 *        Recommend to enable it sleep some time and disable to get the results
 *
 * @param[in]  enable  Whether to enable measurement
 */
void esp_video_render_measure_enable(bool enable);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

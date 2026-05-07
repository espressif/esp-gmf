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
 * @brief  Video render processor handle
 *
 * @note  Video proc is a helper around the internal video-processing pipeline.
 *        You provide the in/out frame information, and it manages the
 *        processing path and frame flow for you.
 *
 *        Supported modes:
 *        - sync write -> direct out callback or framebuffer out
 *        - input worker -> async write through in queue
 *        - output worker -> out first enters cache, then worker calls `on_frame`
 *        - cache_only -> out stays in cache until `acquire_out()` / `release_out()`
 *        - framebuffer out -> `acquire_fb` / `on_fb` / `release_fb`
 *        - direct decode to framebuffer when out frame size matches
 *
 *        Rules:
 *        - no worker: `write()` runs processing in caller context
 *        - `set_in_worker()`: only changes how in enters the proc
 *        - `set_out_worker()`: enables out cache, with or without out worker task
 *        - `fb_out = true`: uses framebuffer callbacks instead of frame callback/cache
 *        - framebuffer out cannot be combined with out cache/worker
 *
 *        Conflict cases:
 *        - Not allow `set_out_worker()` when use frame buffer output `out_cfg.fb_out = true`
 *
 *        Graphic view:
 *
 *            write()
 *               |
 *               +--> in_worker = N -------------------------+
 *               |                                           |
 *               +--> in_worker = Y -> in queue -> in worker +
 *                                                           |
 *                                                           v
 *                                                     [video proc]
 *                                                           |
 *                    +--------------------------------------+--------------------------------------+
 *                    |                                                                             |
 *                    +--> fb_out = Y -> acquire_fb -> on_fb -> release_fb                          |
 *                    |                                                                             |
 *                    +--> fb_out = N -> out_worker = N -> on_frame                                 |
 *                    |                                                                             |
 *                    +--> fb_out = N -> out cache -> cache_only = Y -> acquire_out/release_out     |
 *                    |                                                                             |
 *                    +--> fb_out = N -> out cache -> cache_only = N -> out worker -> on_frame      |
 *                    |                                                                             |
 *                    +-----------------------------------------------------------------------------+
 */
typedef void *esp_video_render_proc_handle_t;

/**
 * @brief  Frame buffer configuration
 */
typedef struct {
    int (*acquire_fb)(esp_video_render_fb_t *fb, void *ctx);  /*!< Acquire frame buffer callback */
    int (*release_fb)(esp_video_render_fb_t *fb, void *ctx);  /*!< Release frame buffer callback */
} esp_video_render_proc_fb_cfg_t;

/**
 * @brief  Output configuration
 */
typedef struct {
    bool                            fb_out;  /*!< Output to frame buffer or not */
    esp_video_render_proc_fb_cfg_t  fb_cfg;  /*!< Configuration for frame buffer acquire and release */
    union {
        int (*on_frame)(esp_video_render_frame_t *frame, void *ctx);  /*!< On frame callback */
        int (*on_fb)(esp_video_render_fb_t *fb, void *ctx);           /*!< On frame buffer callback */
    };
    void *out_ctx;  /*!< Output context */
} esp_video_render_proc_out_cfg_t;

/**
 * @brief  Video render processor configuration
 */
typedef struct {
    esp_gmf_pool_handle_t            pool;            /*!< Pool handle */
    esp_video_render_frame_info_t    in_frame_info;   /*!< Input frame information */
    esp_video_render_frame_info_t    out_frame_info;  /*!< Output frame information */
    esp_video_render_proc_out_cfg_t  out_cfg;         /*!< Output configuration */
} esp_video_render_proc_cfg_t;

/**
 * @brief  Input worker configuration
 */
typedef struct {
    esp_video_render_task_cfg_t  task_cfg;       /*!< Input worker task configuration */
    uint32_t                     in_cache_size;  /*!< Input cache size */
} esp_video_render_proc_in_worker_cfg_t;

/**
 * @brief  Output worker configuration
 */
typedef struct {
    esp_video_render_task_cfg_t  task_cfg;        /*!< Output worker task configuration */
    uint32_t                     out_cache_size;  /*!< Output cache size */
    bool                         cache_only;      /*!< Cache only user need call `esp_video_render_proc_acquire_out` and release function to get frame data */
} esp_video_render_proc_out_worker_cfg_t;

/**
 * @brief  Open video processor
 *
 * @param[in]  proc            Video processor handle
 * @param[in]  in_frame_info   Input sample information
 * @param[in]  out_frame_info  Output sample information
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK             On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_RESOURCE    Not enough resource
 *       - ESP_VIDEO_RENDER_ERR_INVALID_STATE  Not allowed to open when running
 */
esp_video_render_err_t esp_video_render_proc_open(esp_video_render_proc_cfg_t *cfg, esp_video_render_proc_handle_t *proc);

/**
 * @brief  Set input worker for video processor
 *
 * @note  If video processor with input cache, use must set input worker to process frame data
 *        It will use the work task to read the cached data and do process in async way
 *
 * @param[in]  proc_hd  Video processor handle
 * @param[in]  cfg      Input worker configuration
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK             On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_INVALID_STATE  Processor not in valid state
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM         Not enough memory
 *       - ESP_VIDEO_RENDER_ERR_FAIL           Failed to create worker
 */
esp_video_render_err_t esp_video_render_proc_set_in_worker(esp_video_render_proc_handle_t proc_hd,
                                                           esp_video_render_proc_in_worker_cfg_t *cfg);

/**
 * @brief  Set output worker for video processor
 *
 * @note  If video processor with output cache, there are 2 ways to process output frame data
 *        1. Set output worker, so that user need only provide process function to process frame data
 *        2. User manually call `esp_video_render_proc_acquire_out`, process frame
 *           then call `esp_video_render_proc_release_out` to release frame
 *
 * @param[in]  proc_hd  Video processor handle
 * @param[in]  cfg      Output worker configuration
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK             On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_INVALID_STATE  Processor not in valid state
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM         Not enough memory
 *       - ESP_VIDEO_RENDER_ERR_FAIL           Failed to create worker
 */
esp_video_render_err_t esp_video_render_proc_set_out_worker(esp_video_render_proc_handle_t proc_hd,
                                                            esp_video_render_proc_out_worker_cfg_t *cfg);

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
esp_video_render_err_t esp_video_render_proc_write(esp_video_render_proc_handle_t proc,
                                                   esp_video_render_frame_t *frame);

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
esp_video_render_err_t esp_video_render_proc_get_out_frame_info(esp_video_render_proc_handle_t proc,
                                                                esp_video_render_frame_info_t *info);

/**
 * @brief  Acquire output frame from video processor
 *
 * @note  When output worker is not set, and output data is cached
 *        User can call this function to acquire the output frame from cache
 *        And call `esp_video_render_proc_release_out` to release frame after use
 *
 * @param[in]   proc   Video processor handle
 * @param[out]  frame  Output frame to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK             On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_INVALID_STATE  Processor not in valid state
 *       - ESP_VIDEO_RENDER_ERR_NO_RESOURCE    Not enough resource
 */
esp_video_render_err_t esp_video_render_proc_acquire_out(esp_video_render_proc_handle_t proc,
                                                         esp_video_render_frame_t **frame);

/**
 * @brief  Release output frame to video processor
 *
 * @param[in]  proc   Video processor handle
 * @param[in]  frame  Output frame to release
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK             On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_INVALID_STATE  Processor not in valid state
 *       - ESP_VIDEO_RENDER_ERR_FAIL           Failed to release frame
 */
esp_video_render_err_t esp_video_render_proc_release_out(esp_video_render_proc_handle_t proc,
                                                         esp_video_render_frame_t *frame);

/**
 * @brief  Close video processor
 *
 * @param[in]  proc  Video processor handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_proc_close(esp_video_render_proc_handle_t proc);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

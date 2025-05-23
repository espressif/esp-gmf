/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_capture_types.h"
#include "esp_capture_overlay_if.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  This file provides interface for capture path management system
 *
 * @note  Key concepts:
 *          - Capture Path: Processes data format conversion between source and sink
 *          - Path Manager: Manages multiple concurrent capture paths
 *
 *        Path characteristics:
 *          - Represents a processing group for all streams (audio/video/etc)
 *          - Controlled as a single unit (start/stop/enable operations)
 *          - Path index corresponds to capture sink index (logical grouping)
 *
 *        System workflow:
 *          - Uses this interface for path configuration and control
 *          - Handles frame data processing from source to final delivery
 *          - Provides processed data to end users
 */

/**
 * @brief  Capture path manager interface alias
 */
typedef struct esp_capture_path_mngr_if_t esp_capture_path_mngr_if_t;

/**
 * @brief  Pipeline builder configuration for capture path manager
 */
typedef struct {
    const char **element_tags;  /*!< Pipeline elements name (order from head to tail) */
    uint8_t      element_num;   /*!< Pipeline elements number */
} esp_capture_path_build_pipeline_cfg_t;

/**
 * @brief  Setting type for capture path manager
 */
typedef enum {
    ESP_CAPTURE_PATH_SET_TYPE_NONE             = 0,  /*!< Invalid set type */
    ESP_CAPTURE_PATH_SET_TYPE_RUN_ONCE         = 1,  /*!< Set run only once */
    ESP_CAPTURE_PATH_SET_TYPE_SYNC_HANDLE      = 2,  /*!< Set sync handle for audio or video source */
    ESP_CAPTURE_PATH_SET_TYPE_AUDIO_BITRATE    = 3,  /*!< Set for audio bitrate */
    ESP_CAPTURE_PATH_SET_TYPE_VIDEO_BITRATE    = 4,  /*!< Set for video bitrate */
    ESP_CAPTURE_PATH_SET_TYPE_VIDEO_FPS        = 5,  /*!< Set for video frame per second */
    ESP_CAPTURE_PATH_SET_TYPE_REGISTER_ELEMENT = 6,  /*!< Set for register element into internal pool */
    ESP_CAPTURE_PATH_SET_TYPE_BUILD_PIPELINE   = 7,  /*!< Set for build pipeline */
} esp_capture_path_set_type_t;

/**
 * @brief  Getting type for capture path
 */
typedef enum {
    ESP_CAPTURE_PATH_GET_TYPE_NONE = 0,  /*!< Invalid get type */
    ESP_CAPTURE_PATH_GET_ELEMENT   = 1,  /*!< Get element handle by element tag */
} esp_capture_path_get_type_t;

/**
 * @brief  Get element information for capture path
 */
typedef struct {
    const char *element_tag;  /*!< Tag for the element */
    void       *element_hd;   /*!< Returned element handle */
} esp_capture_path_element_get_info_t;

/**
 * @brief  Event of capture path
 */
typedef enum {
    ESP_CAPTURE_PATH_EVENT_TYPE_NONE            = 0,  /*!< Invalid event type */
    ESP_CAPTURE_PATH_EVENT_AUDIO_STARTED        = 1,  /*!< Event type for audio started */
    ESP_CAPTURE_PATH_EVENT_AUDIO_NOT_SUPPORT    = 2,  /*!< Event type for audio not supported */
    ESP_CAPTURE_PATH_EVENT_AUDIO_ERROR          = 3,  /*!< Event type for audio error */
    ESP_CAPTURE_PATH_EVENT_AUDIO_FINISHED       = 4,  /*!< Event type for audio finished */
    ESP_CAPTURE_PATH_EVENT_VIDEO_STARTED        = 5,  /*!< Event type for video started */
    ESP_CAPTURE_PATH_EVENT_VIDEO_NOT_SUPPORT    = 6,  /*!< Event type for video not supported */
    ESP_CAPTURE_PATH_EVENT_VIDEO_ERROR          = 7,  /*!< Event type for video error */
    ESP_CAPTURE_PATH_EVENT_VIDEO_FINISHED       = 8,  /*!< Event type for audio finished */
    ESP_CAPTURE_PATH_EVENT_AUDIO_PIPELINE_BUILT = 9,  /*!< Event type for audio pipeline build done */
    ESP_CAPTURE_PATH_EVENT_VIDEO_PIPELINE_BUILT = 10, /*!< Event type for video pipeline build done */
} esp_capture_path_event_type_t;

/**
 * @brief  Capture path configuration
 */
typedef struct {
    /**
     * @brief  Notify that a frame is available to capture
     *
     * @param[in]  src    Pointer to the source context
     * @param[in]  path   Path type where the frame was processed
     * @param[in]  frame  Pointer to the processed frame
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to send processed frame
     */
    esp_capture_err_t (*frame_avail)(void *src, uint8_t path, esp_capture_stream_frame_t *frame);

    /**
     * @brief  Notify path event to capture
     *
     * @param[in]  src    Pointer to the source context
     * @param[in]  path   Path type where the frame was processed
     * @param[in]  event  Pointer to the processed frame
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to notify path event
     */
    esp_capture_err_t (*event_cb)(void *src, uint8_t path, esp_capture_path_event_type_t event);

    /**
     * @brief  Pointer to the source context
     */
    void *src_ctx;
} esp_capture_path_cfg_t;

/**
 * @brief  Capture path manager interface
 */
struct esp_capture_path_mngr_if_t {
    /**
     * @brief  Open a capture path interface with specified configuration
     *
     * @param[in]  p    Pointer to the capture path interface
     * @param[in]  cfg  Pointer to the capture path configuration
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to open path manager
     */
    esp_capture_err_t (*open)(esp_capture_path_mngr_if_t *p, esp_capture_path_cfg_t *cfg);

    /**
     * @brief Add a new path to the capture path interface
     *
     * @note  Allow Call twice before start to change configurations
     *
     * @param[in]  p     Pointer to the capture path interface
     * @param[in]  path  Type of the path to be added
     * @param[in]  sink  Pointer to the sink configuration for the path
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to add path
     */
    esp_capture_err_t (*add_path)(esp_capture_path_mngr_if_t *p, uint8_t path, esp_capture_stream_info_t *sink);

    /**
     * @brief  Enable or disable a specific path
     *
     * @param[in]  p       Pointer to the capture path interface
     * @param[in]  path    Type of the path to be enabled or disabled
     * @param[in]  enable  Boolean flag to enable (true) or disable (false) the path
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to enable path
     */
    esp_capture_err_t (*enable_path)(esp_capture_path_mngr_if_t *p, uint8_t path, bool enable);

    /**
     * @brief Start the capture path interface
     *
     * @note  Once path manager started, all added path (enabled) will start
     *
     * @param[in]  p  Pointer to the capture path interface
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to start path manager
     */
    esp_capture_err_t (*start)(esp_capture_path_mngr_if_t *p);

    /**
     * @brief  Configure a specific path with given settings
     *
     * @param[in]  p         Pointer to the capture path interface
     * @param[in]  path      Type of the path to be configured
     * @param[in]  type      Type of the setting to be applied
     * @param[in]  cfg       Pointer to the configuration data
     * @param[in]  cfg_size  Size of the configuration data
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to set for path
     */
    esp_capture_err_t (*set)(esp_capture_path_mngr_if_t *p, uint8_t path, esp_capture_path_set_type_t type,
                             void *cfg, int cfg_size);

    /**
     * @brief  Get configuration from path manager
     *
     * @param[in]  p         Pointer to the capture path interface
     * @param[in]  path      Type of the path to be configured
     * @param[in]  type      Getting type
     * @param[in]  cfg       Pointer to the configuration data
     * @param[in]  cfg_size  Size of the configuration data
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to get configuration from path
     */
    esp_capture_err_t (*get)(esp_capture_path_mngr_if_t *p, uint8_t path, esp_capture_path_get_type_t type,
                             void *cfg, int cfg_size);

    /**
     * @brief  Return a frame back to the capture path interface
     *
     * @note  In the design when frame is generated, it notify to capture through `esp_capture_path_cfg_t.frame_avail`
     *        So that capture can get data instantly, when consumed call this API to release the frame related memory
     *
     * @param[in]  p      Pointer to the capture path interface
     * @param[in]  path   Type of the path for which the frame is returned
     * @param[in]  frame  Pointer to the frame to be returned
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to return frame
     */
    esp_capture_err_t (*return_frame)(esp_capture_path_mngr_if_t *p, uint8_t path, esp_capture_stream_frame_t *frame);

    /**
     * @brief  Stop the capture path interface
     *
     * @param[in]  p  Pointer to the capture path interface
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to stop path
     */
    esp_capture_err_t (*stop)(esp_capture_path_mngr_if_t *p);

    /**
     * @brief  Close the capture path interface
     *
     * @param[in]  p  Pointer to the capture path interface
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to close path
     */
    esp_capture_err_t (*close)(esp_capture_path_mngr_if_t *p);
};

/**
 * @brief  Audio capture path manager interface (typedef alias)
 */
typedef struct esp_capture_audio_path_mngr_if_t esp_capture_audio_path_mngr_if_t;

/**
 * @brief  Audio capture path manager interface
 */
struct esp_capture_audio_path_mngr_if_t {
    esp_capture_path_mngr_if_t base;  /*!< Base of path manager */
};

/**
 * @brief  Video capture path manager interface (typedef alias)
 */
typedef struct esp_capture_video_path_mngr_if_t esp_capture_video_path_mngr_if_t;

/**
 * @brief  Video capture path manager interface
 */
struct esp_capture_video_path_mngr_if_t {
    /**
     * @brief  Base of path manager
     */
    esp_capture_path_mngr_if_t base;

    /**
     * @brief Add an overlay to a specific path
     *
     * @param[in]  p        Pointer to the capture path interface
     * @param[in]  path     Type of the path to which the overlay will be added
     * @param[in]  overlay  Pointer to the overlay interface
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to add overlay
     */
    esp_capture_err_t (*add_overlay)(esp_capture_video_path_mngr_if_t *p, uint8_t path, esp_capture_overlay_if_t *overlay);

    /**
     * @brief Enable or disable an overlay on a specific path
     *
     * @param[in]  p       Pointer to the capture path interface
     * @param[in]  path    Type of the path where the overlay is applied
     * @param[in]  enable  Boolean flag to enable (true) or disable (false) the overlay
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to enable overlay
     */
    esp_capture_err_t (*enable_overlay)(esp_capture_video_path_mngr_if_t *p, uint8_t path, bool enable);
};

#ifdef __cplusplus
}
#endif  /* __cplusplus */

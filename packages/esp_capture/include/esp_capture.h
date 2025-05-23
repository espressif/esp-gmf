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

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Capture handle
 */
typedef void *esp_capture_handle_t;

/**
 * @brief  Capture event
 */
typedef enum {
    ESP_CAPTURE_EVENT_NONE                 = 0,  /*!< Default/initial state, indicates no event has occurred */
    ESP_CAPTURE_EVENT_STARTED              = 1,  /*!< Triggered when the capture system successfully starts */
    ESP_CAPTURE_EVENT_STOPPED              = 2,  /*!< Triggered when the capture system has been stopped */
    ESP_CAPTURE_EVENT_ERROR                = 3,  /*!< Triggered when an error occurs during capture */
    ESP_CAPTURE_EVENT_AUDIO_PIPELINE_BUILT = 4,  /*!< Triggered when the audio pipeline is successfully built in GMF capture path
                                                      This event allows users to configure pipeline elements before capture starts */
    ESP_CAPTURE_EVENT_VIDEO_PIPELINE_BUILT = 5,  /*!< Triggered when the video pipeline is successfully built in GMF capture path
                                                      This event allows users to configure pipeline elements before capture starts */
} esp_capture_event_t;

/**
 * @brief  Capture event callback
 */
typedef esp_capture_err_t (*esp_capture_event_cb_t)(esp_capture_event_t event, void *ctx);

/**
 * @brief  Capture configuration
 */
typedef struct {
    esp_capture_sync_mode_t      sync_mode;  /*!< Capture synchronized mode */
    esp_capture_audio_src_if_t  *audio_src;  /*!< Audio source interface */
    esp_capture_video_src_if_t  *video_src;  /*!< Video source interface */
} esp_capture_cfg_t;

/**
 * @brief  Capture thread scheduler configuration
 */
typedef struct {
    uint32_t  stack_size;        /*!< Thread reserve stack size */
    uint8_t   priority;          /*!< Thread priority */
    uint8_t   core_id     : 4;   /*!< CPU core id for thread to run */
    uint8_t   stack_in_ext : 1;  /*!< Whether put thread stack into PSRAM */
} esp_capture_thread_schedule_cfg_t;

/**
 * @brief  Capture thread scheduler callback function type
 *
 * @param[in]   name          Thread name
 * @param[out]  schedule_cfg  Thread scheduler configuration to be filled
 */
typedef void (*esp_capture_thread_scheduler_cb_t)(const char *name, esp_capture_thread_schedule_cfg_t *schedule_cfg);

/**
 * @brief  Set capture thread scheduler
 *
 * @note  Capture provide a unified scheduler for all created thread
 *        User can easily adjust the thread configuration in `thread_scheduler` callback
 *        Currently only support static scheduler, thread scheduler apply before running only once
 *        Better to call it before `esp_capture_start` so that scheduler take effect for each created thread
 *        If not provided, it will use default scheduler setting
 *
 *        Users can call `esp_gmf_oal_sys_get_real_time_stats()` to obtain task execution
 *        snapshots and performance metrics. This information can be used to analyze
 *        system performance and optimize scheduler settings for better resource utilization
 *
 * @param[in]  thread_scheduler  Thread scheduler callback
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK  On success
 */
esp_capture_err_t esp_capture_set_thread_scheduler(esp_capture_thread_scheduler_cb_t thread_scheduler);

/**
 * @brief  Open capture
 *
 * @param[in]   cfg      Capture configuration
 * @param[out]  capture  Pointer to output capture system handle
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK            On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG   Invalid input argument
 *       - ESP_CAPTURE_ERR_NO_MEM        No enough memory for capture instance
 *       - ESP_CAPTURE_ERR_NO_RESOURCES  No related resources
 */
esp_capture_err_t esp_capture_open(esp_capture_cfg_t *cfg, esp_capture_handle_t *capture);

/**
 * @brief  Set event callback for capture
 *
 * @param[in]  capture  Capture system handle
 * @param[in]  cb       Capture event callback
 * @param[in]  ctx      Capture event context
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK           On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG  Invalid input argument
 */
esp_capture_err_t esp_capture_set_event_cb(esp_capture_handle_t capture, esp_capture_event_cb_t cb, void *ctx);

/**
 * @brief  Start capture
 *
 * @note  If capture system contain multiple capture sink, all sinks (enabled) will be started
 *
 * @param[in]  capture  Capture system handle
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK           On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG  Invalid input argument
 */
esp_capture_err_t esp_capture_start(esp_capture_handle_t capture);

/**
 * @brief  Stop capture
 *
 * @note  All capture sinks will be stopped
 *
 * @param[in]  capture  Capture system handle
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK           On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG  Invalid input argument
 */
esp_capture_err_t esp_capture_stop(esp_capture_handle_t capture);

/**
 * @brief  Close capture
 *
 * @note  The whole capture system will be destroyed, all related capture paths will be destroyed also
 *
 * @param[in]  capture  Capture system handle
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK           On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG  Invalid input argument
 */
esp_capture_err_t esp_capture_close(esp_capture_handle_t capture);

/**
 * @brief  Enables performance monitoring for the capture process
 *
 * @note  This is a debug function that logs the time taken by each processor during the capture process to assess performance
 *        When `enable` is set to false, the performance data will be printed
 *        The monitoring primarily tracks the start and stop procedures
 *        To use this feature, enable `CONFIG_ESP_CAPTURE_ENABLE_PERF_MON` in menuconfig
 *
 * @param[in]  enable  If true, activates the performance monitor; if false, deactivates it
 */
void esp_capture_enable_perf_monitor(bool enable);

#ifdef __cplusplus
}
#endif  /* __cplusplus */


/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Run video capture for configured duration
 *
 * @param[in]  duration  Capture duration
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to run
 */
int video_capture_run(int duration);

/**
 * @brief  Run video capture in one shot mode for configured duration
 *
 * @note  One shot mode is suitable for application like take photo
 *        It output one image only if triggered once
 *
 * @param[in]  duration  Capture duration
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to run
 */
int video_capture_run_one_shot(int duration);

/**
 * @brief  Run video capture with overlay for configured duration
 *
 * @note  It use the internally supported text overlay to add test on to video stream
 *
 * @param[in]  duration  Capture duration (unit ms)
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to run
 */
int video_capture_run_with_overlay(int duration);

/**
 * @brief  Run video capture with muxer for configured duration
 *
 * @note  In this case it will capture video data and stored into MP4 file
 *
 * @param[in]  duration  Capture duration (unit ms)
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to run
 */
int video_capture_run_with_muxer(int duration);

/**
 * @brief  Run video capture with customized processor for configured duration
 *
 * @note  In this text code, it tries to add ALC element into process and control video level
 *
 * @param[in]  duration  Capture duration (unit ms)
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to run
 */
int video_capture_run_with_customized_process(int duration);

/**
 * @brief  Run video capture for dual path
 *
 * @param[in]  duration  Capture duration (unit ms)
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to run
 */
int video_capture_run_dual_path(int duration);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

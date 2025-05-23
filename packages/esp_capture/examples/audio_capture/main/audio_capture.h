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
 * @brief  Run audio capture for configured duration
 *
 * @param[in]  duration  Capture duration (unit ms)
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to run
 */
int audio_capture_run(int duration);

/**
 * @brief  Run audio capture with AEC for configured duration
 *
 * @param[in]  duration  Capture duration (unit ms)
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to run
 */
int audio_capture_run_with_aec(int duration);

/**
 * @brief  Run audio capture with muxer for configured duration
 *
 * @note  In this case it will capture audio data and stored into MP4 file
 *
 * @param[in]  duration  Capture duration (unit ms)
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to run
 */
int audio_capture_run_with_muxer(int duration);

/**
 * @brief  Run audio capture with customized processor for configured duration
 *
 * @note  In this text code, it tries to add ALC element into process and control audio level
 *
 * @param[in]  duration  Capture duration (unit ms)
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to run
 */
int audio_capture_run_with_customized_process(int duration);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

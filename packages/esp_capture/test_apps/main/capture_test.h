/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

void capture_set_skip_storage_test(bool skip_storage);

int auto_customized_audio_capture_test(int timeout, bool dual);

int demo_capture_one_shot(int timeout, bool dual_path);

int demo_capture_to_storage(int timeout, bool dual_path);

int demo_video_capture_with_overlay(int timeout, bool dual);

int auto_audio_only_path_test(int timeout, bool dual);

int auto_audio_only_bypass_test(int timeout, bool dual);

int manual_audio_only_path_test(int timeout, bool dual);

int advance_audio_only_path_test(int timeout, bool dual);

int auto_video_only_path_test(int timeout, bool dual);

int manual_video_only_path_test(int timeout, bool dual);

int advance_video_only_path_test(int timeout, bool dual);

int auto_av_path_test(int timeout, bool dual);

int auto_av_path_dynamic_enable_test(int timeout, bool dual);

int manual_av_path_test(int timeout, bool dual);

int advance_av_path_test(int timeout, bool dual);

int auto_av_muxer_path_test(int timeout, bool dual);

int advance_av_muxer_path_test(int timeout, bool dual);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_capture_sink.h"
#include "esp_capture_defaults.h"
#include "esp_capture_advance.h"
#include "capture_gmf_mngr.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef struct {
    int  audio_frame_count[2];
    int  video_frame_count[2];
    int  muxer_frame_count[2];
    int  audio_frame_size[2];
    int  video_frame_size[2];
    int  muxer_frame_size[2];
    int  audio_pts[2];
    int  video_pts[2];
    int  muxer_pts[2];
} capture_run_result_t;

typedef struct {
    esp_capture_audio_src_if_t        *aud_src;
    esp_capture_video_src_if_t        *vid_src;
    esp_capture_handle_t               capture;
    esp_capture_sink_handle_t          capture_sink[2];
    // Following are for expert mode only
    esp_capture_pipeline_builder_if_t *aud_builder;
    esp_capture_audio_path_mngr_if_t  *aud_path;
    esp_capture_pipeline_builder_if_t *vid_builder;
    esp_capture_video_path_mngr_if_t  *vid_path;
    capture_run_result_t               run_result;
} capture_sys_t;

void capture_use_fake_source(bool use_faked);

esp_capture_audio_src_if_t *create_audio_source(bool with_aec);

esp_capture_video_src_if_t *create_video_source(void);

int build_audio_only_capture_sys(capture_sys_t *capture_sys);

int build_video_only_capture_sys(capture_sys_t *capture_sys);

int build_av_capture_sys(capture_sys_t *capture_sys);

int build_advance_audio_only_capture_sys(capture_sys_t *capture_sys);

int build_advance_video_only_capture_sys(capture_sys_t *capture_sys);

int build_advance_av_capture_sys(capture_sys_t *capture_sys);

void read_with_timeout(capture_sys_t *capture_sys, bool dual_sink, int timeout);

int read_all_frames(capture_sys_t *capture_sys, bool dual_sink, int timeout);

void destroy_capture_sys(capture_sys_t *capture_sys);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Test API C++ compilation only, not as a example reference
 */
#include "esp_capture.h"
#include "esp_capture_sink.h"
#include "esp_capture_defaults.h"
#include "esp_capture_advance.h"
#include "esp_capture_version.h"
#include "capture_gmf_mngr.h"
#include "capture_builder.h"
#include "esp_audio_enc_default.h"
#include "esp_video_enc_default.h"
#include "mp4_muxer.h"

extern "C" void test_cxx_build(void)
{
    esp_capture_handle_t capture = NULL;
    esp_capture_video_src_if_t *video_src = NULL;
    esp_capture_audio_src_if_t *audio_src = NULL;
    do {
        audio_src = create_audio_source(false);
        video_src = create_video_source();

        // Open capture
        esp_capture_cfg_t capture_cfg = {
            .sync_mode = ESP_CAPTURE_SYNC_MODE_AUDIO,
            .audio_src = audio_src,
            .video_src = video_src,
        };
        esp_capture_open(&capture_cfg, &capture);
        esp_capture_sink_cfg_t sink_cfg = {
            .audio_info = {
                .format_id = ESP_CAPTURE_FMT_ID_AAC,
                .sample_rate = 16000,
                .channel = 2,
                .bits_per_sample = 16,
            },
            .video_info = {
                .format_id = ESP_CAPTURE_FMT_ID_H264,
                .width = 480,
                .height = 320,
                .fps = 30,
            },
        };
        esp_capture_sink_handle_t sink[2] = {NULL};
        esp_capture_sink_setup(capture, 0, &sink_cfg, &sink[0]);
        // Save record content into MP4 container, all data consumed by muxer only
        mp4_muxer_config_t mp4_cfg = {
            .base_config = {
                .muxer_type = ESP_MUXER_TYPE_MP4,
                .slice_duration = 60000,
                .url_pattern = NULL,
                .data_cb = NULL,
                .ctx = NULL,
                .ram_cache_size = 16384,
                .no_key_frame_verify = false,
            },
            .display_in_order = false,
            .moov_before_mdat = false,
        };
        esp_capture_muxer_cfg_t muxer_cfg = {
            .base_config = &mp4_cfg.base_config,
            .cfg_size = sizeof(mp4_cfg),
            .muxer_mask = ESP_CAPTURE_MUXER_MASK_AUDIO,
        };
        esp_capture_sink_add_muxer(sink[0], &muxer_cfg);
        esp_capture_sink_enable_muxer(sink[0], true);
        // Not allow get audio video stream data
        esp_capture_sink_disable_stream(sink[0], ESP_CAPTURE_STREAM_TYPE_AUDIO);
        esp_capture_sink_disable_stream(sink[0], ESP_CAPTURE_STREAM_TYPE_VIDEO);
        esp_capture_sink_enable(sink[0], ESP_CAPTURE_RUN_MODE_ALWAYS);
        // Start capture
        esp_capture_start(capture);
    } while (0);
    if (capture) {
        esp_capture_stop(capture);
        esp_capture_close(capture);
    }
    if (audio_src) {
        free(audio_src);
    }
    if (video_src) {
        free(video_src);
    }
}

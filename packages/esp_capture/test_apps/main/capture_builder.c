/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "esp_timer.h"
#include "esp_heap_trace.h"
#include "esp_heap_caps.h"
#include "esp_capture.h"
#include "esp_capture_sink.h"
#include "esp_capture_defaults.h"
#include "esp_capture_advance.h"
#include "capture_gmf_mngr.h"
#include "capture_builder.h"
#include "capture_fake_aud_src.h"
#include "capture_fake_vid_src.h"
#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
#include "dev_camera.h"
#endif
#include "dev_audio_codec.h"
#include "esp_board_device.h"
#include "esp_board_manager_defs.h"
#include "esp_log.h"

#define TAG "CAPTURE_BUILDER"

static bool use_fake_src = false;

void capture_use_fake_source(bool faked)
{
    use_fake_src = faked;
}

esp_capture_video_src_if_t *create_video_source(void)
{
    if (use_fake_src) {
        return esp_capture_new_video_fake_src(2);
    }
#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
    dev_camera_handle_t *camera_handle = NULL;
    int ret = esp_board_device_get_handle(ESP_BOARD_DEVICE_NAME_CAMERA, (void **)&camera_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get camera device");
        return NULL;
    }
    esp_capture_video_v4l2_src_cfg_t v4l2_cfg = {
        .buf_count = 2,
    };
    strncpy(v4l2_cfg.dev_name, camera_handle->dev_path, sizeof(v4l2_cfg.dev_name) - 1);
    return esp_capture_new_video_v4l2_src(&v4l2_cfg);
#else
    return NULL;
#endif
}

esp_capture_audio_src_if_t *create_audio_source(bool with_aec)
{
    if (use_fake_src) {
        return esp_capture_new_audio_fake_src();
    }
    dev_audio_codec_handles_t *codec_handle = NULL;
    esp_err_t ret = esp_board_device_get_handle(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, (void **)&codec_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to audio_adc device");
        return NULL;
    }
    // Test AEC source on esp32s3 and esp32p4
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
    if (with_aec) {
        esp_capture_audio_aec_src_cfg_t aec_cfg = {
            .record_handle = codec_handle->codec_dev,
#if CONFIG_IDF_TARGET_ESP32S3
            .channel = 4,
            .channel_mask = 1 | 2,
#endif  /* CONFIG_IDF_TARGET_ESP32S3 */
        };
        return esp_capture_new_audio_aec_src(&aec_cfg);
    }
#endif  /* CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4 */
    esp_capture_audio_dev_src_cfg_t codec_cfg = {
        .record_handle = codec_handle->codec_dev,
    };
    return esp_capture_new_audio_dev_src(&codec_cfg);
}

int build_audio_only_capture_sys(capture_sys_t *capture_sys)
{
    // Test AEC source on esp32s3 and esp32p4
    capture_sys->aud_src = create_audio_source(true);
    if (capture_sys->aud_src == NULL) {
        ESP_LOGE(TAG, "Fail to create audio source");
        return -1;
    }
    esp_capture_cfg_t capture_cfg = {
        .audio_src = capture_sys->aud_src,
    };
    esp_capture_open(&capture_cfg, &capture_sys->capture);
    if (capture_sys->capture == NULL) {
        ESP_LOGE(TAG, "Fail to create capture");
        return -1;
    }
    return 0;
}

int build_video_only_capture_sys(capture_sys_t *capture_sys)
{
    capture_sys->vid_src = create_video_source();
    if (capture_sys->vid_src == NULL) {
        ESP_LOGE(TAG, "Fail to create video source");
        return -1;
    }
    esp_capture_cfg_t capture_cfg = {
        .video_src = capture_sys->vid_src,
    };
    esp_capture_open(&capture_cfg, &capture_sys->capture);
    if (capture_sys->capture == NULL) {
        ESP_LOGE(TAG, "Fail to create capture");
        return -1;
    }
    return 0;
}

int build_av_capture_sys(capture_sys_t *capture_sys)
{
    capture_sys->aud_src = create_audio_source(false);
    if (capture_sys->aud_src == NULL) {
        ESP_LOGE(TAG, "Fail to create audio source");
        return -1;
    }
    capture_sys->vid_src = create_video_source();
    if (capture_sys->vid_src == NULL) {
        ESP_LOGE(TAG, "Fail to create video source");
        return -1;
    }
    esp_capture_cfg_t capture_cfg = {
        .sync_mode = ESP_CAPTURE_SYNC_MODE_AUDIO,
        .audio_src = capture_sys->aud_src,
        .video_src = capture_sys->vid_src,
    };
    esp_capture_open(&capture_cfg, &capture_sys->capture);
    if (capture_sys->capture == NULL) {
        ESP_LOGE(TAG, "Fail to create capture");
        return -1;
    }
    return 0;
}

static int build_advance_capture_video_path(capture_sys_t *capture_sys)
{
    capture_sys->vid_src = create_video_source();
    if (capture_sys->vid_src == NULL) {
        ESP_LOGE(TAG, "Fail to create video source");
        return -1;
    }
    // Use video source to create video pipelines
    esp_capture_video_src_if_t *vid_src[1] = {
        capture_sys->vid_src,
    };
    esp_capture_gmf_video_pipeline_cfg_t vid_pipe_cfg = {
        .vid_src = vid_src,
        .vid_src_num = 1,
        .vid_sink_num = 2,
    };
    capture_sys->vid_builder = esp_capture_create_video_pipeline(&vid_pipe_cfg);
    if (capture_sys->vid_builder == NULL) {
        ESP_LOGE(TAG, "Fail to create video pipeline");
        return -1;
    }
    esp_capture_video_path_mngr_cfg_t vid_path_mngr = {
        .pipeline_builder = capture_sys->vid_builder,
    };
    capture_sys->vid_path = esp_capture_new_gmf_video_mngr(&vid_path_mngr);
    if (capture_sys->vid_path == NULL) {
        ESP_LOGE(TAG, "Fail to create video path");
        return -1;
    }
    return 0;
}

static int build_advance_capture_audio_path(capture_sys_t *capture_sys)
{
    capture_sys->aud_src = create_audio_source(false);
    if (capture_sys->aud_src == NULL) {
        ESP_LOGE(TAG, "Fail to create audio source");
        return -1;
    }
    // Use audio source to create audio pipelines
    esp_capture_audio_src_if_t *aud_src[1] = {
        capture_sys->aud_src};
    esp_capture_gmf_audio_pipeline_cfg_t pipe_cfg = {
        .aud_src = aud_src,
        .aud_src_num = 1,
        .aud_sink_num = 2,
    };
    capture_sys->aud_builder = esp_capture_create_audio_pipeline(&pipe_cfg);
    if (capture_sys->aud_builder == NULL) {
        ESP_LOGE(TAG, "Fail to create audio pipeline builder");
        return -1;
    }
    // Use audio pipeline builder to create audio path
    esp_capture_audio_path_mngr_cfg_t path_mngr = {
        .pipeline_builder = capture_sys->aud_builder,
    };
    capture_sys->aud_path = esp_capture_new_gmf_audio_mngr(&path_mngr);
    if (capture_sys->aud_path == NULL) {
        ESP_LOGE(TAG, "Fail to create audio path");
        return -1;
    }
    return 0;
}

int build_advance_audio_only_capture_sys(capture_sys_t *capture_sys)
{
    int ret = build_advance_capture_audio_path(capture_sys);
    if (ret != 0) {
        return ret;
    }
    esp_capture_advance_cfg_t advance_cfg = {
        .sync_mode = ESP_CAPTURE_SYNC_MODE_AUDIO,
        .audio_path = capture_sys->aud_path,
    };
    esp_capture_advance_open(&advance_cfg, &capture_sys->capture);
    if (capture_sys->capture == NULL) {
        ESP_LOGE(TAG, "Fail to create advanced capture");
        return -1;
    }
    return 0;
}

int build_advance_video_only_capture_sys(capture_sys_t *capture_sys)
{
    int ret = build_advance_capture_video_path(capture_sys);
    if (ret != 0) {
        return ret;
    }
    esp_capture_advance_cfg_t advance_cfg = {
        .video_path = capture_sys->vid_path,
    };
    esp_capture_advance_open(&advance_cfg, &capture_sys->capture);
    if (capture_sys->capture == NULL) {
        ESP_LOGE(TAG, "Fail to create advanced capture");
        return -1;
    }
    return 0;
}

int build_advance_av_capture_sys(capture_sys_t *capture_sys)
{
    int ret = build_advance_capture_audio_path(capture_sys);
    if (ret != 0) {
        return ret;
    }
    // Allow no video case
    ret = build_advance_capture_video_path(capture_sys);
    esp_capture_advance_cfg_t advance_cfg = {
        .sync_mode = ESP_CAPTURE_SYNC_MODE_AUDIO,
        .video_path = capture_sys->vid_path,
        .audio_path = capture_sys->aud_path,
    };
    esp_capture_advance_open(&advance_cfg, &capture_sys->capture);
    if (capture_sys->capture == NULL) {
        ESP_LOGE(TAG, "Fail to create advanced capture");
        return -1;
    }
    return 0;
}

void read_with_timeout(capture_sys_t *capture_sys, bool dual_sink, int timeout)
{
    capture_run_result_t *res = &capture_sys->run_result;
    memset(res, 0, sizeof(capture_run_result_t));
    uint32_t start_time = esp_timer_get_time() / 1000;
    uint32_t cur_time = start_time;
    int sink_num = dual_sink ? 2 : 1;
    while (cur_time < start_time + timeout) {
        // Following code acquire frame without wait for all supported sink
        for (int i = 0; i < sink_num; i++) {
            esp_capture_stream_frame_t frame = {
                .stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO,
            };
            while (esp_capture_sink_acquire_frame(capture_sys->capture_sink[i], &frame, true) == ESP_CAPTURE_ERR_OK) {
                res->audio_frame_count[i]++;
                if (res->audio_frame_count[i] == 1) {
                    ESP_LOGI(TAG, "[%d] First audio frame received pts %d", i, (int)frame.pts);
                }
                res->audio_frame_size[i] += frame.size;
                res->audio_pts[i] = frame.pts;
                esp_capture_sink_release_frame(capture_sys->capture_sink[i], &frame);
            }
            frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;
            while (esp_capture_sink_acquire_frame(capture_sys->capture_sink[i], &frame, true) == ESP_CAPTURE_ERR_OK) {
                res->video_frame_count[i]++;
                if (res->video_frame_count[i] == 1) {
                    ESP_LOGI(TAG, "[%d] First video frame received pts %d", i, (int)frame.pts);
                }
                res->video_frame_size[i] += frame.size;
                res->video_pts[i] = frame.pts;
                esp_capture_sink_release_frame(capture_sys->capture_sink[i], &frame);
            }
            frame.stream_type = ESP_CAPTURE_STREAM_TYPE_MUXER;
            while (esp_capture_sink_acquire_frame(capture_sys->capture_sink[i], &frame, true) == ESP_CAPTURE_ERR_OK) {
                res->muxer_frame_count[i]++;
                if (res->muxer_frame_count[i] == 1) {
                    ESP_LOGI(TAG, "[%d] First muxed frame received pts %d", i, (int)frame.pts);
                }
                res->muxer_frame_size[i] += frame.size;
                res->muxer_pts[i] = frame.pts;
                esp_capture_sink_release_frame(capture_sys->capture_sink[i], &frame);
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
        cur_time = esp_timer_get_time() / 1000;
    }

    // Show capture results
    for (int i = 0; i < sink_num; i++) {
        if (res->audio_frame_count[i]) {
            ESP_LOGW(TAG, "Audio Path %d frame_count:%d frame_size:%d pts:%d", i,
                     res->audio_frame_count[i], res->audio_frame_size[i], res->audio_pts[i]);
        }
        if (res->video_frame_count[i]) {
            ESP_LOGW(TAG, "Video Path %d frame_count:%d frame_size:%d pts:%d", i,
                     res->video_frame_count[i], res->video_frame_size[i], res->video_pts[i]);
        }
        if (res->muxer_frame_count[i]) {
            ESP_LOGW(TAG, "Muxer Path %d frame_count:%d frame_size:%d pts:%d", i,
                     res->muxer_frame_count[i], res->muxer_frame_size[i], res->muxer_pts[i]);
        }
    }
}

int read_all_frames(capture_sys_t *capture_sys, bool dual_sink, int timeout)
{
    if (capture_sys->capture_sink[0]) {
        esp_capture_sink_enable(capture_sys->capture_sink[0], ESP_CAPTURE_RUN_MODE_ALWAYS);
    }
    if (dual_sink && capture_sys->capture_sink[1]) {
        esp_capture_sink_enable(capture_sys->capture_sink[1], ESP_CAPTURE_RUN_MODE_ALWAYS);
    }
    int ret = esp_capture_start(capture_sys->capture);
    if (ret != 0) {
        ESP_LOGE(TAG, "Fail to start capture");
        return -1;
    }
    read_with_timeout(capture_sys, dual_sink, timeout);
    esp_capture_stop(capture_sys->capture);
    return 0;
}

void destroy_capture_sys(capture_sys_t *capture_sys)
{
    if (capture_sys->capture) {
        esp_capture_close(capture_sys->capture);
        capture_sys->capture = NULL;
    }
    if (capture_sys->aud_src) {
        free(capture_sys->aud_src);
        capture_sys->aud_src = NULL;
    }
    if (capture_sys->vid_src) {
        free(capture_sys->vid_src);
        capture_sys->vid_src = NULL;
    }
    if (capture_sys->aud_builder) {
        esp_capture_destroy_pipeline(capture_sys->aud_builder);
        capture_sys->aud_builder = NULL;
    }
    if (capture_sys->aud_path) {
        free(capture_sys->aud_path);
        capture_sys->aud_path = NULL;
    }
    if (capture_sys->vid_builder) {
        esp_capture_destroy_pipeline(capture_sys->vid_builder);
        capture_sys->vid_builder = NULL;
    }
    if (capture_sys->vid_path) {
        free(capture_sys->vid_path);
        capture_sys->vid_path = NULL;
    }
}

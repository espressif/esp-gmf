/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "settings.h"
#include "mp4_muxer.h"
#include "esp_capture.h"
#include "esp_capture_defaults.h"
#include "esp_capture_sink.h"
#include "esp_gmf_video_overlay.h"
#include "video_capture.h"
#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
#include "dev_camera.h"
#endif
#include "dev_audio_codec.h"
#include "esp_board_device.h"
#include "esp_board_manager_defs.h"
// For advanced usage like customized process pipeline
#include "esp_capture_advance.h"


#define TAG "VIDEO_CAPTURE"

typedef struct {
    esp_capture_handle_t         capture;  /*!< Capture handle */
    esp_capture_audio_src_if_t  *aud_src;  /*!< Audio source interface for video capture */
    esp_capture_video_src_if_t  *vid_src;  /*!< Video source interface */
} video_capture_sys_t;

typedef struct {
    uint32_t aud_frames;
    uint32_t aud_total_frame_size;
    uint32_t vid_frames;
    uint32_t vid_total_frame_size;
} video_capture_res_t;

static esp_capture_video_src_if_t *create_video_source(void)
{
#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
    dev_camera_handle_t *camera_handle = NULL;
    esp_err_t ret = esp_board_device_get_handle(ESP_BOARD_DEVICE_NAME_CAMERA, (void **)&camera_handle);
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

static int build_video_capture(video_capture_sys_t *capture_sys)
{
    // Create video source firstly
    capture_sys->vid_src = create_video_source();
    if (capture_sys->vid_src == NULL) {
        ESP_LOGE(TAG, "Fail to create video source");
        return -1;
    }
    esp_codec_dev_handle_t record_handle = NULL;
    dev_audio_codec_handles_t *codec_handle = NULL;
    esp_err_t ret = esp_board_device_get_handle(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, (void **)&codec_handle);
    if (ret == ESP_OK) {
        record_handle = codec_handle->codec_dev;
    }
    if (record_handle) {
        esp_capture_audio_dev_src_cfg_t codec_cfg = {
            .record_handle = record_handle
        };
        capture_sys->aud_src = esp_capture_new_audio_dev_src(&codec_cfg);
        if (capture_sys->aud_src == NULL) {
            ESP_LOGE(TAG, "Fail to create audio source");
            return -1;
        }
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

static void destroy_video_capture(video_capture_sys_t *capture_sys)
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
}

static int read_all_frames(esp_capture_sink_handle_t sink[], video_capture_res_t res[], int sink_num)
{
    esp_capture_stream_frame_t frame = {0};
    for (int i = 0; i < sink_num; i++) {
        frame.stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO;
        // Acquire audio frame no-wait
        while (esp_capture_sink_acquire_frame(sink[i], &frame, true) == ESP_CAPTURE_ERR_OK) {
            res[i].aud_frames++;
            res[i].aud_total_frame_size += frame.size;
            esp_capture_sink_release_frame(sink[i], &frame);
        }
        frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;
        // Acquire video frame no-wait
        while (esp_capture_sink_acquire_frame(sink[i], &frame, true) == ESP_CAPTURE_ERR_OK) {
            res[i].vid_frames++;
            res[i].vid_total_frame_size += frame.size;
            esp_capture_sink_release_frame(sink[i], &frame);
        }
    }
    return 0;
}

static int read_video_frames(esp_capture_sink_handle_t sink[], int sink_num, int duration)
{
    video_capture_res_t res[sink_num];
    uint32_t start_time = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t cur_time = start_time;
    memset(res, 0, sizeof(res));
    // Read frames until duration reached
    do {
        read_all_frames(sink, res, sink_num);
        // Sleep 20ms to avoid busy-loop
        vTaskDelay(20 / portTICK_PERIOD_MS);
        cur_time = (uint32_t)(esp_timer_get_time() / 1000);
    } while (cur_time < start_time + duration);
    for (int i = 0; i < sink_num; i++) {
        ESP_LOGI(TAG, "Sink %d read audio %lu/%lu video %lu/%lu in %dms",
                 i, res[i].aud_total_frame_size, res[i].aud_frames,
                 res[i].vid_total_frame_size, res[i].vid_frames, duration);
    }
    return 0;
}

int video_capture_run(int duration)
{
    video_capture_sys_t capture_sys = {0};
    do {
        // Build capture system
        int ret = build_video_capture(&capture_sys);
        if (ret != 0) {
            break;
        }
        // Setup for sink
        esp_capture_sink_handle_t sink = NULL;
        esp_capture_sink_cfg_t sink_cfg = {
            .video_info = {
                .format_id = VIDEO_SINK0_FMT,
                .width = VIDEO_SINK0_WIDTH,
                .height = VIDEO_SINK0_HEIGHT,
                .fps = VIDEO_SINK0_FPS,
            },
            .audio_info = {
                .format_id = AUDIO_SINK0_FMT,
                .sample_rate = AUDIO_SINK0_SAMPLE_RATE,
                .channel = AUDIO_SINK0_CHANNEL,
                .bits_per_sample = 16,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &sink);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to setup sink");
            break;
        }
        // Enable sink and start
        esp_capture_sink_enable(sink, ESP_CAPTURE_RUN_MODE_ALWAYS);
        ret = esp_capture_start(capture_sys.capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to start video capture");
            break;
        }
        read_video_frames(&sink, 1, duration);
        ret = esp_capture_stop(capture_sys.capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to start video capture");
            break;
        }
    } while (0);
    destroy_video_capture(&capture_sys);
    return 0;
}

static int read_video_frames_for_one_shot(esp_capture_sink_handle_t sink, int duration)
{
    video_capture_res_t res;
    esp_capture_stream_frame_t frame = {
        .stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO,
    };
    uint32_t start_time = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t cur_time = start_time;
    memset(&res, 0, sizeof(res));
    // Read frames until duration reached
    do {
        esp_capture_sink_enable(sink, ESP_CAPTURE_RUN_MODE_ONESHOT);
        // Acquire in wait mode
        int ret = esp_capture_sink_acquire_frame(sink, &frame, false);
        if (ret == ESP_CAPTURE_ERR_OK) {
            res.vid_frames++;
            res.vid_total_frame_size += frame.size;
            // Add processing code here
            esp_capture_sink_release_frame(sink, &frame);
        } else {
            ESP_LOGW(TAG, "Failed to acquire frame, ret=%d", ret);
        }
        // Sleep 500ms for next one shot
        vTaskDelay(500 / portTICK_PERIOD_MS);
        cur_time = (uint32_t)(esp_timer_get_time() / 1000);
    } while (cur_time < start_time + duration);
    ESP_LOGI(TAG, "One shot capture: video frames %lu/%lu in %dms",
             res.vid_frames, res.vid_total_frame_size, duration);
    return 0;
}

int video_capture_run_one_shot(int duration)
{
    video_capture_sys_t capture_sys = {0};
    do {
        // Build capture system
        int ret = build_video_capture(&capture_sys);
        if (ret != 0) {
            break;
        }
        // Setup for sink
        esp_capture_sink_handle_t sink = NULL;
        esp_capture_sink_cfg_t sink_cfg = {
            .video_info = {
                .format_id = ESP_CAPTURE_FMT_ID_MJPEG,
                .width = VIDEO_SINK0_WIDTH,
                .height = VIDEO_SINK0_HEIGHT,
                .fps = VIDEO_SINK0_FPS,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &sink);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to setup sink");
            break;
        }
        // Enable sink and start
        esp_capture_sink_enable(sink, ESP_CAPTURE_RUN_MODE_ONESHOT);
        ret = esp_capture_start(capture_sys.capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to start video capture");
            break;
        }
        read_video_frames_for_one_shot(sink, duration);
        ret = esp_capture_stop(capture_sys.capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to start video capture");
            break;
        }
    } while (0);
    destroy_video_capture(&capture_sys);
    return 0;
}

#define FILE_SLICE_STORAGE_PATTERN "/sdcard/vid_%d.mp4"

static int check_file_size(int slice_idx)
{
    char file_path[64] = {0};
    snprintf(file_path, sizeof(file_path), FILE_SLICE_STORAGE_PATTERN, slice_idx);
    FILE *fp = fopen(file_path, "r");
    if (fp == NULL) {
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    int file_size = ftell(fp);
    fclose(fp);
    ESP_LOGI(TAG, "Storage to %s size %d", file_path, file_size);
    return file_size;
}

static int storage_slice_hdlr(char *file_path, int len, int slice_idx)
{
    snprintf(file_path, len, FILE_SLICE_STORAGE_PATTERN, slice_idx);
    ESP_LOGI(TAG, "Start to write to file %s", file_path);
    return 0;
}

int video_capture_run_with_muxer(int duration)
{
    video_capture_sys_t capture_sys = {0};
    int ret = 0;
    do {
        // Build capture system with AEC
        ret = build_video_capture(&capture_sys);
        if (ret != 0) {
            break;
        }
        // Setup for sink
        esp_capture_sink_handle_t sink = NULL;
        esp_capture_sink_cfg_t sink_cfg = {
            .video_info = {
                .format_id = VIDEO_SINK0_FMT,
                .width = VIDEO_SINK0_WIDTH,
                .height = VIDEO_SINK0_HEIGHT,
                .fps = VIDEO_SINK0_FPS,
            },
            .audio_info = {
                .format_id = AUDIO_SINK0_FMT,
                .sample_rate = AUDIO_SINK0_SAMPLE_RATE,
                .channel = AUDIO_SINK0_CHANNEL,
                .bits_per_sample = 16,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &sink);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to setup sink");
            break;
        }
        // Add muxer to sink and enable it
        mp4_muxer_config_t mp4_cfg = {
            .base_config = {
                .muxer_type = ESP_MUXER_TYPE_MP4,
                .url_pattern = storage_slice_hdlr,
                .slice_duration = 60000,
            },
        };
        esp_capture_muxer_cfg_t muxer_cfg = {
            .base_config = &mp4_cfg.base_config,
            .cfg_size = sizeof(mp4_cfg),
        };
        ret = esp_capture_sink_add_muxer(sink, &muxer_cfg);
        // Streaming while muxer no need special settings
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to add muxer return %d", ret);
            break;
        }
        esp_capture_sink_enable_muxer(sink, true);
        // Enable sink and start
        esp_capture_sink_enable(sink, ESP_CAPTURE_RUN_MODE_ALWAYS);
        ret = esp_capture_start(capture_sys.capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to start video capture");
            break;
        }
        read_video_frames(&sink, 1, duration);
        ret = esp_capture_stop(capture_sys.capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to start video capture");
            break;
        }
        check_file_size(0);
    } while (0);
    destroy_video_capture(&capture_sys);
    return ret;
}

static int read_overlay_frames(esp_capture_sink_handle_t sink, esp_capture_overlay_if_t *text_overlay, int duration)
{
    video_capture_res_t res = {0};
    uint32_t start_time = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t cur_time = start_time;
    uint32_t last_update = cur_time;
    // Read frames until duration reached
    esp_capture_rgn_t text_rgn = {
        .width = 100,
        .height = 30,
    };
    do {
        // Sleep 10ms to avoid busy-loop
        vTaskDelay(10 / portTICK_PERIOD_MS);
        read_all_frames(&sink, &res, 1);
        cur_time = (uint32_t)(esp_timer_get_time() / 1000);
        if (cur_time > last_update + 200) {
            // Update overlay text
            esp_capture_text_overlay_draw_info_t font_info = {
                .color = COLOR_RGB565_RED,
                .font_size = 12,
            };
            esp_capture_text_overlay_draw_start(text_overlay);
            esp_capture_text_overlay_clear(text_overlay, &text_rgn, COLOR_RGB565_CYAN);
            esp_capture_text_overlay_draw_text_fmt(text_overlay, &font_info, "PTS: %d\nText Overlay",
                                                   (int)cur_time - start_time);
            esp_capture_text_overlay_draw_finished(text_overlay);
            last_update = cur_time;
        }
    } while (cur_time < start_time + duration);
    ESP_LOGI(TAG, "Overlay get video %lu/%lu in %dms",
             res.vid_total_frame_size, res.vid_frames, duration);
    return 0;
}

int video_capture_run_with_overlay(int duration)
{
    video_capture_sys_t capture_sys = {0};
    esp_capture_overlay_if_t *text_overlay = NULL;
    do {
        // Build capture system
        int ret = build_video_capture(&capture_sys);
        if (ret != 0) {
            break;
        }
        // For overlay only support RGB565, we force input use RGB565
        if (capture_sys.vid_src) {
            esp_capture_video_info_t fixed_caps = {
                .format_id = ESP_CAPTURE_FMT_ID_RGB565,
                .width = VIDEO_SINK0_WIDTH,
                .height = VIDEO_SINK0_HEIGHT,
                .fps = VIDEO_SINK0_FPS,
            };
            capture_sys.vid_src->set_fixed_caps(capture_sys.vid_src, &fixed_caps);
        }
        // Setup for sink
        esp_capture_sink_handle_t sink = NULL;
        esp_capture_sink_cfg_t sink_cfg = {
            .video_info = {
                .format_id = VIDEO_SINK0_FMT,
                .width = VIDEO_SINK0_WIDTH,
                .height = VIDEO_SINK0_HEIGHT,
                .fps = VIDEO_SINK0_FPS,
            },
            .audio_info = {
                .format_id = AUDIO_SINK0_FMT,
                .sample_rate = AUDIO_SINK0_SAMPLE_RATE,
                .channel = AUDIO_SINK0_CHANNEL,
                .bits_per_sample = 16,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &sink);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to setup sink");
            break;
        }
        // Enable sink and start
        esp_capture_sink_enable(sink, ESP_CAPTURE_RUN_MODE_ALWAYS);
        // Create overlay, add overlay and enable it
        esp_capture_rgn_t text_rgn = {
            .x = 100,
            .y = 100,
            .width = 100,
            .height = 40,
        };
        text_overlay = esp_capture_new_text_overlay(&text_rgn);
        if (text_overlay == NULL) {
            ESP_LOGE(TAG, "Failed to create text overlay");
            break;
        }
        text_overlay->open(text_overlay);
        // Fill background
        esp_capture_text_overlay_draw_start(text_overlay);
        text_rgn.x = text_rgn.y = 0;
        esp_capture_text_overlay_clear(text_overlay, &text_rgn, COLOR_RGB565_CYAN);
        esp_capture_text_overlay_draw_finished(text_overlay);

        // Add overlay to sink
        ret = esp_capture_sink_add_overlay(sink, text_overlay);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to add overlay");
            break;
        }
        esp_capture_sink_enable_overlay(sink, true);
        ret = esp_capture_start(capture_sys.capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to start video capture");
            break;
        }
        read_overlay_frames(sink, text_overlay, duration);
        ret = esp_capture_stop(capture_sys.capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to stop video capture");
            break;
        }
    } while (0);
    destroy_video_capture(&capture_sys);
    if (text_overlay) {
        text_overlay->close(text_overlay);
    }
    return 0;
}

static esp_capture_err_t custom_pipe_event_hdlr(esp_capture_event_t event, void *ctx)
{
    if (event == ESP_CAPTURE_EVENT_VIDEO_PIPELINE_BUILT) {
        // Now we can do some pre-setting before pipeline run
    }
    return ESP_CAPTURE_ERR_OK;
}

int video_capture_run_with_customized_process(int duration)
{
    video_capture_sys_t capture_sys = {0};
    int ret = 0;
    do {
        // Build capture system
        ret = build_video_capture(&capture_sys);
        if (ret != 0) {
            break;
        }
        // Setup for sink
        esp_capture_sink_handle_t sink = NULL;
        esp_capture_sink_cfg_t sink_cfg = {
            .video_info = {
                .format_id = VIDEO_SINK0_FMT,
                .width = VIDEO_SINK0_WIDTH,
                .height = VIDEO_SINK0_HEIGHT,
                .fps = VIDEO_SINK0_FPS,
            },
            .audio_info = {
                .format_id = AUDIO_SINK0_FMT,
                .sample_rate = AUDIO_SINK0_SAMPLE_RATE,
                .channel = AUDIO_SINK0_CHANNEL,
                .bits_per_sample = 16,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &sink);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to setup sink");
            break;
        }
        esp_capture_set_event_cb(capture_sys.capture, custom_pipe_event_hdlr, sink);
        // We know one audio encoder is enough
        const char *aud_elements[] = {"aud_enc"};
        ret = esp_capture_sink_build_pipeline(sink, ESP_CAPTURE_STREAM_TYPE_AUDIO, aud_elements,
                                              sizeof(aud_elements) / sizeof(aud_elements[0]));
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to do manually build pipeline");
            break;
        }

        // We know that only need video encoder and video fps convert
        const char *vid_elements[] = {"vid_color_cvt", "vid_fps_cvt", "vid_enc"};
        ret = esp_capture_sink_build_pipeline(sink, ESP_CAPTURE_STREAM_TYPE_VIDEO,
                                              vid_elements, sizeof(vid_elements) / sizeof(vid_elements[0]));

        // Enable sink and start
        esp_capture_sink_enable(sink, ESP_CAPTURE_RUN_MODE_ALWAYS);
        ret = esp_capture_start(capture_sys.capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to start video capture");
            break;
        }
        read_video_frames(&sink, 1, duration);
        ret = esp_capture_stop(capture_sys.capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to start video capture");
            break;
        }
    } while (0);
    destroy_video_capture(&capture_sys);
    return ret;
}

int video_capture_run_dual_path(int duration)
{
    video_capture_sys_t capture_sys = {0};
    do {
        // Build capture system
        int ret = build_video_capture(&capture_sys);
        if (ret != 0) {
            break;
        }
        // Video source force to output RGB565 for currently not support convert from YUV422 to RGB565 use esp_camera
#ifndef CONFIG_IDF_TARGET_ESP32P4
        if (capture_sys.vid_src) {
            esp_capture_video_info_t fixed_caps = {
                .format_id = ESP_CAPTURE_FMT_ID_RGB565,
                .width = VIDEO_SINK0_WIDTH,
                .height = VIDEO_SINK0_HEIGHT,
                .fps = VIDEO_SINK0_FPS,
            };
            capture_sys.vid_src->set_fixed_caps(capture_sys.vid_src, &fixed_caps);
        }
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
        // Setup for sink
        esp_capture_sink_handle_t sink[2] = {NULL, NULL};
        esp_capture_sink_cfg_t sink_cfg = {
            .video_info = {
                .format_id = VIDEO_SINK0_FMT,
                .width = VIDEO_SINK0_WIDTH,
                .height = VIDEO_SINK0_HEIGHT,
                .fps = VIDEO_SINK0_FPS,
            },
            .audio_info = {
                .format_id = AUDIO_SINK0_FMT,
                .sample_rate = AUDIO_SINK0_SAMPLE_RATE,
                .channel = AUDIO_SINK0_CHANNEL,
                .bits_per_sample = 16,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &sink[0]);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to setup sink0");
            break;
        }
        // Enable sink and start
        esp_capture_sink_enable(sink[0], ESP_CAPTURE_RUN_MODE_ALWAYS);
        esp_capture_sink_cfg_t sink_cfg_1 = {
            .video_info = {
                .format_id = VIDEO_SINK1_FMT,
                .width = VIDEO_SINK1_WIDTH,
                .height = VIDEO_SINK1_HEIGHT,
                .fps = VIDEO_SINK1_FPS,
            },
            .audio_info = {
                .format_id = AUDIO_SINK1_FMT,
                .sample_rate = AUDIO_SINK1_SAMPLE_RATE,
                .channel = AUDIO_SINK1_CHANNEL,
                .bits_per_sample = 16,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 1, &sink_cfg_1, &sink[1]);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to setup sink1");
            break;
        }
        // Enable sink and start
        esp_capture_sink_enable(sink[1], ESP_CAPTURE_RUN_MODE_ALWAYS);
        ret = esp_capture_start(capture_sys.capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to start video capture");
            break;
        }
        read_video_frames(sink, 2, duration);
        ret = esp_capture_stop(capture_sys.capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to stop video capture");
            break;
        }
    } while (0);
    destroy_video_capture(&capture_sys);
    return 0;
}

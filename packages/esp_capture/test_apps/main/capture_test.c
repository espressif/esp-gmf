/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_capture.h"
#include "esp_capture_sink.h"
#include "esp_capture_defaults.h"
#include "esp_capture_advance.h"
#include "capture_gmf_mngr.h"
#include "esp_gmf_alc.h"
#include "esp_gmf_video_enc.h"
#include "esp_audio_enc_default.h"
#include "esp_video_enc_default.h"
#include "esp_timer.h"
#include "ts_muxer.h"
#include "mp4_muxer.h"
#include "freertos/FreeRTOS.h"
#include "esp_heap_trace.h"
#include "esp_heap_caps.h"
#include "esp_capture_version.h"
#include "settings.h"
#include "capture_builder.h"
#include "esp_log.h"

#define TAG "CAPTURE_TEST"

#define BREAK_ON_FAIL(ret) if (ret != 0) {                           \
    ESP_LOGE(TAG, "Fail on %s:%d ret:%d", __func__, __LINE__, ret);  \
    break;                                                           \
}

#define BREAK_ON_FALSE(ret) if (ret == false) {                      \
    ESP_LOGE(TAG, "Fail on %s:%d ret:%d", __func__, __LINE__, ret);  \
    break;                                                           \
}

#define RET_ON_FAIL(ret) if (ret != 0) {                             \
    ESP_LOGE(TAG, "Fail on %s:%d ret:%d", __func__, __LINE__, ret);  \
    return ret;                                                      \
}

#define ELEMS(arr)                      sizeof(arr) / sizeof(arr[0])
#define PTS_TOLERANCE                   400
#define PTS_IN_TOLERANCE(pts, duration) ((pts < duration + PTS_TOLERANCE) && (pts + duration > PTS_TOLERANCE))
#define TEST_RESULT_VERIFY_AUDIO        (1 << 0)
#define TEST_RESULT_VERIFY_VIDEO        (1 << 1)
#define TEST_RESULT_VERIFY_MUXER        (1 << 2)

static bool skip_storage_test = false;

void capture_set_skip_storage_test(bool skip_storage)
{
    skip_storage_test = skip_storage;
}

static esp_capture_err_t demo_custom_pipe_event_hdlr(esp_capture_event_t event, void *ctx)
{
    esp_capture_sink_handle_t sink = (esp_capture_sink_handle_t)ctx;
    if (event == ESP_CAPTURE_EVENT_AUDIO_PIPELINE_BUILT) {
        // Now we can do some setting before pipeline run
        esp_gmf_element_handle_t alc_hd = NULL;
        esp_capture_sink_get_element_by_tag(sink, ESP_CAPTURE_STREAM_TYPE_AUDIO, "aud_alc", &alc_hd);
        if (alc_hd) {
            int8_t old_gain = 0;
            esp_gmf_alc_get_gain(alc_hd, 0, &old_gain);
            esp_gmf_alc_set_gain(alc_hd, 0, old_gain + 5);
            ESP_LOGI(TAG, "Set ALC gain from %d to %d", old_gain, old_gain + 5);
        }
    }
    return ESP_CAPTURE_ERR_OK;
}

int auto_customized_audio_capture_test(int timeout, bool dual)
{
    esp_capture_handle_t capture = NULL;
    esp_capture_audio_src_if_t *audio_src = NULL;
    int ret = 0;
    do {
        // Create audio source
        audio_src = create_audio_source(false);
        if (audio_src == NULL) {
            ESP_LOGE(TAG, "Failed to create audio source");
            break;
        }
        // Open capture
        esp_capture_cfg_t capture_cfg = {
            .audio_src = audio_src,
        };
        ret = esp_capture_open(&capture_cfg, &capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to open capture");
            break;
        }
        // Add ALC element into capture POOL
        // Once element register success, capture take over the control of alc
        esp_ae_alc_cfg_t alc_cfg = DEFAULT_ESP_GMF_ALC_CONFIG();
        esp_gmf_element_handle_t alc_hd = NULL;
        esp_gmf_alc_init(&alc_cfg, &alc_hd);
        ret = esp_capture_register_element(capture, ESP_CAPTURE_STREAM_TYPE_AUDIO, alc_hd);
        if (ret != ESP_CAPTURE_ERR_OK) {
            esp_gmf_obj_delete(alc_hd);
        }
        // Do sink configuration for capture AAC
        esp_capture_sink_cfg_t sink_cfg = {
            .audio_info = {
                .format_id = ESP_CAPTURE_FMT_ID_AAC,
                .sample_rate = 16000,
                .channel = 2,
                .bits_per_sample = 16,
            },
        };
        esp_capture_sink_handle_t sink = NULL;
        ret = esp_capture_sink_setup(capture, 0, &sink_cfg, &sink);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to do sink setup");
            break;
        }
        esp_capture_set_event_cb(capture, demo_custom_pipe_event_hdlr, sink);
        const char *aud_elements[] = {"aud_ch_cvt", "aud_rate_cvt", "aud_alc", "aud_enc"};
        ret = esp_capture_sink_build_pipeline(sink, ESP_CAPTURE_STREAM_TYPE_AUDIO, aud_elements, ELEMS(aud_elements));
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to do manually build pipeline");
            break;
        }
        esp_capture_sink_enable(sink, ESP_CAPTURE_RUN_MODE_ALWAYS);
        // Start capture
        ret = esp_capture_start(capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to start capture");
            break;
        }
        // Try to acquire audio frame for one second
        uint32_t start_time = esp_timer_get_time() / 1000;
        uint32_t cur_time = start_time;
        esp_capture_stream_frame_t frame = {
            .stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO,
        };
        int frame_count = 0;
        uint32_t latest_pts = 0;
        while (cur_time < start_time + timeout) {
            // Acquire audio frame in sync mode
            ret = esp_capture_sink_acquire_frame(sink, &frame, false);
            if (ret != ESP_CAPTURE_ERR_OK) {
                ESP_LOGE(TAG, "Failed to acquire frame");
                break;
            }
            esp_capture_sink_release_frame(sink, &frame);
            cur_time = esp_timer_get_time() / 1000;
            latest_pts = frame.pts;
            frame_count++;
        }
        ESP_LOGI(TAG, "Frame count: %d", frame_count);
        // Check for PTS and frames
        if (frame_count == 0 || latest_pts == 0) {
            ESP_LOGE(TAG, "Failed to verify frame and PTS");
            ret = -1;
        }
    } while (0);
    if (capture) {
        esp_capture_stop(capture);
        esp_capture_close(capture);
    }
    if (audio_src) {
        free(audio_src);
    }
    return ret;
}

int demo_capture_one_shot(int timeout, bool dual_path)
{
    esp_capture_handle_t capture = NULL;
    int ret = 0;
    esp_capture_video_src_if_t *video_src = create_video_source();
    esp_capture_audio_src_if_t *audio_src = NULL;
    do {
        audio_src = create_audio_source(false);
        if (audio_src == NULL) {
            ESP_LOGE(TAG, "Failed to create audio source");
            break;
        }
        if (video_src == NULL) {
            ESP_LOGE(TAG, "Failed to create video source");
            break;
        }
#ifndef CONFIG_IDF_TARGET_ESP32P4
        // For camera support output MJPEG need add decoder to output YUV420
        // Here force to output RGB565
        esp_capture_video_info_t fixed_caps = {
            .format_id = ESP_CAPTURE_FMT_ID_RGB565,
            .width = VIDEO_WIDTH,
            .height = VIDEO_HEIGHT,
            .fps = VIDEO_FPS,
        };
        video_src->set_fixed_caps(video_src, &fixed_caps);
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
        // Open capture
        esp_capture_cfg_t capture_cfg = {
            .audio_src = audio_src,
            .video_src = video_src,
        };
        ret = esp_capture_open(&capture_cfg, &capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to open capture");
            break;
        }
        // Do sink configuration for capture MJPEG
        esp_capture_sink_cfg_t sink_cfg = {
            .audio_info = {
                .format_id = ESP_CAPTURE_FMT_ID_AAC,
                .sample_rate = 16000,
                .channel = 2,
                .bits_per_sample = 16,
            },
            .video_info = {
                .format_id = ESP_CAPTURE_FMT_ID_MJPEG,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            },
        };
        esp_capture_sink_handle_t sink[2] = {NULL};
        ret = esp_capture_sink_setup(capture, 0, &sink_cfg, &sink[0]);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to do sink setup");
            break;
        }
        esp_capture_sink_enable(sink[0], ESP_CAPTURE_RUN_MODE_ONESHOT);

        if (dual_path) {
            esp_capture_sink_cfg_t sink_cfg = {
                .audio_info = {
                    .format_id = ESP_CAPTURE_FMT_ID_G711A,
                    .sample_rate = 8000,
                    .channel = 1,
                    .bits_per_sample = 16,
                },
                .video_info = {
                    .format_id = ESP_CAPTURE_FMT_ID_H264,
                    .width = VIDEO_WIDTH,
                    .height = VIDEO_HEIGHT,
                    .fps = VIDEO_FPS,
                },
            };
            ret = esp_capture_sink_setup(capture, 1, &sink_cfg, &sink[1]);
            if (ret != ESP_CAPTURE_ERR_OK) {
                ESP_LOGE(TAG, "Failed to do sink setup");
                break;
            }
            esp_capture_sink_enable(sink[1], ESP_CAPTURE_RUN_MODE_ALWAYS);
        }

        // Start capture
        ret = esp_capture_start(capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to start capture");
            break;
        }
        // Try to acquire video frame for one second
        int audio_frame_count[2] = {0};
        int audio_frame_size[2] = {0};
        int video_frame_count[2] = {0};
        int video_frame_size[2] = {0};

        uint32_t start_time = esp_timer_get_time() / 1000;
        uint32_t cur_time = start_time;
        uint32_t trigger_time = start_time;
        esp_capture_stream_frame_t frame = {0};
        while (cur_time < start_time + timeout) {
            if (cur_time > trigger_time + 200) {
                // Trigger for one shot again every 200ms
                esp_capture_sink_enable(sink[0], ESP_CAPTURE_RUN_MODE_ONESHOT);
                trigger_time = cur_time;
            }
            for (int i = 0; i < (dual_path ? 2 : 1); i++) {
                frame.stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO;
                while (esp_capture_sink_acquire_frame(sink[i], &frame, true) == ESP_CAPTURE_ERR_OK) {
                    esp_capture_sink_release_frame(sink[i], &frame);
                    audio_frame_count[i]++;
                    audio_frame_size[i] += frame.size;
                }
                frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;
                while (esp_capture_sink_acquire_frame(sink[i], &frame, true) == ESP_CAPTURE_ERR_OK) {
                    esp_capture_sink_release_frame(sink[i], &frame);
                    video_frame_count[i]++;
                    video_frame_size[i] += frame.size;
                }
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
            cur_time = esp_timer_get_time() / 1000;
        }
        for (int i = 0; i < (dual_path ? 2 : 1); i++) {
            if (audio_frame_count[i]) {
                ESP_LOGI(TAG, "Audio Path %d frame_count:%d frame_size:%d", i, audio_frame_count[i], audio_frame_size[i]);
            }
            if (video_frame_count[i]) {
                ESP_LOGI(TAG, "Video Path %d frame_count:%d frame_size:%d", i, video_frame_count[i], video_frame_size[i]);
            }
        }
        for (int i = 0; i < (dual_path ? 2 : 1); i++) {
            if (audio_frame_count[i] == 0 || video_frame_count[i] == 0) {
                ESP_LOGE(TAG, "Failed to verify frame and PTS");
                ret = -1;
            }
        }
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
    return ret;
}

#define FILE_SLICE_STORAGE_PATTERN "/sdcard/J_%d.mp4"

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

int demo_capture_to_storage(int timeout, bool dual_path)
{
    if (skip_storage_test) {
        ESP_LOGW(TAG, "Skip %s test", __func__);
        return 0;
    }
    esp_capture_handle_t capture = NULL;
    int ret = 0;
    esp_capture_video_src_if_t *video_src = NULL;
    esp_capture_audio_src_if_t *audio_src = NULL;
    do {
        audio_src = create_audio_source(false);
        if (audio_src == NULL) {
            ESP_LOGE(TAG, "Failed to create audio source");
            break;
        }
        video_src = create_video_source();
        if (video_src == NULL) {
            ESP_LOGE(TAG, "Failed to create video source");
            break;
        }
        // Open capture
        esp_capture_cfg_t capture_cfg = {
            .sync_mode = ESP_CAPTURE_SYNC_MODE_AUDIO,
            .audio_src = audio_src,
            .video_src = video_src,
        };
        ret = esp_capture_open(&capture_cfg, &capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to open capture");
            break;
        }
        esp_capture_sink_cfg_t sink_cfg = {
            .audio_info = {
                .format_id = ESP_CAPTURE_FMT_ID_AAC,
                .sample_rate = 16000,
                .channel = 2,
                .bits_per_sample = 16,
            },
            .video_info = {
                .format_id = ESP_CAPTURE_FMT_ID_H264,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            },
        };
        esp_capture_sink_handle_t sink[2] = {NULL};
        ret = esp_capture_sink_setup(capture, 0, &sink_cfg, &sink[0]);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to do sink setup");
            break;
        }
        // Save record content into MP4 container, all data consumed by muxer only
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
        ret = esp_capture_sink_add_muxer(sink[0], &muxer_cfg);
        BREAK_ON_FAIL(ret);
        esp_capture_sink_enable_muxer(sink[0], true);
        // Not allow get audio video stream data
        esp_capture_sink_disable_stream(sink[0], ESP_CAPTURE_STREAM_TYPE_AUDIO);
        esp_capture_sink_disable_stream(sink[0], ESP_CAPTURE_STREAM_TYPE_VIDEO);
        esp_capture_sink_enable(sink[0], ESP_CAPTURE_RUN_MODE_ALWAYS);
        // Start capture
        ret = esp_capture_start(capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to start capture");
            break;
        }
        // Here just wait for record over the duration, no need to acquire frame
        uint32_t start_time = esp_timer_get_time() / 1000;
        uint32_t cur_time = start_time;
        while (cur_time < start_time + timeout) {
            vTaskDelay(50 / portTICK_PERIOD_MS);
            cur_time = esp_timer_get_time() / 1000;
        }
    } while (0);
    if (capture) {
        esp_capture_stop(capture);
        if (check_file_size(0) == 0) {
            ESP_LOGE(TAG, "Muxer not storage into file at all");
            ret = -1;
        }
        esp_capture_close(capture);
    }
    if (audio_src) {
        free(audio_src);
    }
    if (video_src) {
        free(video_src);
    }
    return ret;
}

int demo_video_capture_with_overlay(int timeout, bool dual)
{
    esp_capture_handle_t capture = NULL;
    int ret = 0;
    esp_capture_video_src_if_t *video_src = create_video_source();
    esp_capture_overlay_if_t *text_overlay = NULL;
    do {
        if (video_src == NULL) {
            ESP_LOGE(TAG, "Failed to create video source");
            break;
        }
        // Open capture
        esp_capture_cfg_t capture_cfg = {
            .video_src = video_src,
        };
        ret = esp_capture_open(&capture_cfg, &capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to open capture");
            break;
        }
#ifndef CONFIG_IDF_TARGET_ESP32P4
        esp_capture_video_info_t fixed_caps = {
            .format_id = ESP_CAPTURE_FMT_ID_RGB565,
            .width = VIDEO_WIDTH,
            .height = VIDEO_HEIGHT,
            .fps = VIDEO_FPS,
        };
        video_src->set_fixed_caps(video_src, &fixed_caps);
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
        // Do sink configuration for capture MJPEG
        esp_capture_sink_cfg_t sink_cfg = {
            .video_info = {
                .format_id = ESP_CAPTURE_FMT_ID_MJPEG,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            },
        };
        esp_capture_sink_handle_t sink = NULL;
        ret = esp_capture_sink_setup(capture, 0, &sink_cfg, &sink);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to do sink setup");
            break;
        }
        esp_capture_sink_enable(sink, ESP_CAPTURE_RUN_MODE_ALWAYS);

        // Create overlay
        uint32_t video_pts = 0;
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
        text_rgn.x = 0;
        text_rgn.y = 0;
        esp_capture_text_overlay_draw_start(text_overlay);
        esp_capture_text_overlay_clear(text_overlay, &text_rgn, COLOR_RGB565_CYAN);
        // Default only support font size 12
        esp_capture_text_overlay_draw_info_t font_info = {
            .color = COLOR_RGB565_RED,
            .font_size = 12,
            .x = 0,
            .y = 0,
        };
        esp_capture_text_overlay_draw_text_fmt(text_overlay, &font_info, "PTS: %d\nText Overlay", (int)video_pts);
        esp_capture_text_overlay_draw_finished(text_overlay);

        // Add overlay to sink
        ret = esp_capture_sink_add_overlay(sink, text_overlay);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to add overlay");
            break;
        }
        esp_capture_sink_enable_overlay(sink, true);

        // Start capture
        ret = esp_capture_start(capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to start capture");
            break;
        }
        // Try to acquire video frame for one second
        uint32_t start_time = esp_timer_get_time() / 1000;
        uint32_t cur_time = start_time;
        esp_capture_stream_frame_t frame = {
            .stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO,
        };
        int frame_count = 0;
        uint32_t last_pts = video_pts;
        uint8_t alpha = 0;
        while (cur_time < start_time + timeout) {
            // Acquire video frame in sync mode
            ret = esp_capture_sink_acquire_frame(sink, &frame, false);
            if (ret != ESP_CAPTURE_ERR_OK) {
                ESP_LOGE(TAG, "Failed to acquire frame");
                break;
            }
            esp_capture_sink_release_frame(sink, &frame);
            cur_time = esp_timer_get_time() / 1000;
            video_pts = frame.pts;
            // Redraw text overlay every 200ms
            if (video_pts > last_pts + 200) {
                text_rgn.width = 100;
                text_rgn.height = 30;
                esp_capture_text_overlay_draw_start(text_overlay);
                alpha++;
                text_overlay->set_alpha(text_overlay, alpha);
                esp_capture_text_overlay_clear(text_overlay, &text_rgn, COLOR_RGB565_CYAN);
                esp_capture_text_overlay_draw_text_fmt(text_overlay, &font_info, "PTS: %d\n%d Text Overlay", (int)video_pts, alpha);
                esp_capture_text_overlay_draw_finished(text_overlay);
                last_pts = video_pts;
            }
            frame_count++;
        }
        text_overlay->close(text_overlay);
        if (frame_count == 0 || last_pts == 0) {
            ESP_LOGE(TAG, "Failed to verify frame and PTS");
            ret = -1;
        }
        ESP_LOGI(TAG, "Frame count: %d", frame_count);
    } while (0);
    if (capture) {
        esp_capture_stop(capture);
        esp_capture_close(capture);
    }
    if (video_src) {
        free(video_src);
    }
    if (text_overlay) {
        free(text_overlay);
    }
    return ret;
}

static bool verify_test_result(capture_sys_t *capture_sys, bool dual, int flag, int duration)
{
    for (int i = 0; i < (dual ? 2 : 1); i++) {
        if (flag & TEST_RESULT_VERIFY_AUDIO) {
            if (capture_sys->run_result.audio_frame_size[i] == 0 ||
                capture_sys->run_result.audio_frame_count[i] == 0 ||
                capture_sys->run_result.audio_pts[i] == 0) {
                return false;
            }
        }
        if (flag & TEST_RESULT_VERIFY_VIDEO) {
            if (capture_sys->run_result.video_frame_count[i] == 0 ||
                capture_sys->run_result.video_frame_size[i] == 0 ||
                capture_sys->run_result.video_pts[i] == 0) {
                return false;
            }
        }
        if (flag & TEST_RESULT_VERIFY_MUXER) {
            if (capture_sys->run_result.muxer_frame_count[i] == 0 ||
                capture_sys->run_result.muxer_frame_size[i] == 0 ||
                capture_sys->run_result.muxer_pts[i] == 0) {
                return false;
            }
        }
    }
    return true;
}

static bool verify_test_result_for_path(capture_sys_t *capture_sys, int i, int flag, bool has_data)
{
    if (flag & TEST_RESULT_VERIFY_AUDIO) {
        if (capture_sys->run_result.audio_frame_size[i] == 0 ||
            capture_sys->run_result.audio_frame_count[i] == 0 ||
            capture_sys->run_result.audio_pts[i] == 0) {
            // Want data but not received
            if (has_data) {
                ESP_LOGE(TAG, "Sink %d audio not received", i);
                return false;
            }
        } else if (has_data == false) {
            // Received data but unwanted
            ESP_LOGE(TAG, "Why sink %d audio received", i);
            return false;
        }
    }
    if (flag & TEST_RESULT_VERIFY_VIDEO) {
        if (capture_sys->run_result.video_frame_count[i] == 0 ||
            capture_sys->run_result.video_frame_size[i] == 0 ||
            capture_sys->run_result.video_pts[i] == 0) {
            if (has_data) {
                ESP_LOGE(TAG, "Sink %d video not received", i);
                return false;
            }
        } else if (has_data == false) {
            // Received data but unwanted
            ESP_LOGE(TAG, "Why sink %d video received", i);
            return false;
        }
    }
    if (flag & TEST_RESULT_VERIFY_MUXER) {
        if (capture_sys->run_result.muxer_frame_count[i] == 0 ||
            capture_sys->run_result.muxer_frame_size[i] == 0 ||
            capture_sys->run_result.muxer_pts[i] == 0) {
            if (has_data) {
                ESP_LOGE(TAG, "Sink %d muxer not received", i);
                return false;
            }
        } else if (has_data == false) {
            // Received data but unwanted
            ESP_LOGE(TAG, "Why sink %d muxer received", i);
            return false;
        }
    }
    return true;
}

int auto_audio_only_path_test(int timeout, bool dual)
{
    capture_sys_t capture_sys = {0};
    int ret = 0;
    do {
        // Build up capture system
        ret = build_audio_only_capture_sys(&capture_sys);
        BREAK_ON_FAIL(ret);

        esp_capture_sink_cfg_t sink_cfg = {
            .audio_info = {
                .format_id = ESP_CAPTURE_FMT_ID_AAC,
                .sample_rate = 48000,
                .channel = 2,
                .bits_per_sample = 16,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &capture_sys.capture_sink[0]);
        BREAK_ON_FAIL(ret);
        if (dual) {
            esp_capture_sink_cfg_t sink_cfg_1 = {
                .audio_info = {
                    .format_id = ESP_CAPTURE_FMT_ID_G711A,
                    .sample_rate = 8000,
                    .channel = 1,
                    .bits_per_sample = 16,
                },
            };
            ret = esp_capture_sink_setup(capture_sys.capture, 1, &sink_cfg_1, &capture_sys.capture_sink[1]);
            BREAK_ON_FAIL(ret);
        }
        ret = read_all_frames(&capture_sys, dual, timeout);
        BREAK_ON_FAIL(ret);
        if (!verify_test_result(&capture_sys, dual, TEST_RESULT_VERIFY_AUDIO, timeout)) {
            ESP_LOGE(TAG, "Failed to verify frame and PTS");
            ret = -1;
        }
    } while (0);
    destroy_capture_sys(&capture_sys);
    return ret;
}

int auto_audio_only_bypass_test(int timeout, bool dual)
{
    capture_sys_t capture_sys = {0};
    int ret = 0;
    do {
        // Build up capture system
        ret = build_audio_only_capture_sys(&capture_sys);
        BREAK_ON_FAIL(ret);

        esp_capture_sink_cfg_t sink_cfg = {
            .audio_info = {
                .format_id = ESP_CAPTURE_FMT_ID_PCM,
                .sample_rate = 48000,
                .channel = 2,
                .bits_per_sample = 16,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &capture_sys.capture_sink[0]);
        BREAK_ON_FAIL(ret);
        if (dual) {
            esp_capture_sink_cfg_t sink_cfg_1 = {
                .audio_info = {
                    .format_id = ESP_CAPTURE_FMT_ID_PCM,
                    .sample_rate = 8000,
                    .channel = 1,
                    .bits_per_sample = 16,
                },
            };
            ret = esp_capture_sink_setup(capture_sys.capture, 1, &sink_cfg_1, &capture_sys.capture_sink[1]);
            BREAK_ON_FAIL(ret);
        }
        ret = read_all_frames(&capture_sys, dual, timeout);
        BREAK_ON_FAIL(ret);
        if (!verify_test_result(&capture_sys, dual, TEST_RESULT_VERIFY_AUDIO, timeout)) {
            ESP_LOGE(TAG, "Failed to verify frame and PTS");
            ret = -1;
        }
    } while (0);
    destroy_capture_sys(&capture_sys);
    return ret;
}

int manual_audio_only_path_test(int timeout, bool dual)
{
    capture_sys_t capture_sys = {0};
    int ret = 0;
    do {
        // Build up capture system
        ret = build_audio_only_capture_sys(&capture_sys);
        BREAK_ON_FAIL(ret);

        esp_capture_sink_cfg_t sink_cfg = {
            .audio_info = {
                .format_id = ESP_CAPTURE_FMT_ID_AAC,
                .sample_rate = 48000,
                .channel = 2,
                .bits_per_sample = 16,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &capture_sys.capture_sink[0]);
        BREAK_ON_FAIL(ret);
        const char *aud_elements[] = {"aud_ch_cvt", "aud_rate_cvt", "aud_enc"};
        ret = esp_capture_sink_build_pipeline(capture_sys.capture_sink[0], ESP_CAPTURE_STREAM_TYPE_AUDIO,
                                              aud_elements, ELEMS(aud_elements));
        BREAK_ON_FAIL(ret);
        if (dual) {
            esp_capture_sink_cfg_t sink_cfg_1 = {
                .audio_info = {
                    .format_id = ESP_CAPTURE_FMT_ID_G711A,
                    .sample_rate = 8000,
                    .channel = 1,
                    .bits_per_sample = 16,
                },
            };
            ret = esp_capture_sink_setup(capture_sys.capture, 1, &sink_cfg_1, &capture_sys.capture_sink[1]);
            BREAK_ON_FAIL(ret);
            // We know that only need add channel convert, sample rate convert and encoder for second path
            const char *aud_elements_1[] = {"aud_ch_cvt", "aud_rate_cvt", "aud_enc"};
            ret = esp_capture_sink_build_pipeline(capture_sys.capture_sink[1], ESP_CAPTURE_STREAM_TYPE_AUDIO,
                                                  aud_elements_1, ELEMS(aud_elements_1));
            BREAK_ON_FAIL(ret);
        }
        ret = read_all_frames(&capture_sys, dual, timeout);
        BREAK_ON_FAIL(ret);
        if (!verify_test_result(&capture_sys, dual, TEST_RESULT_VERIFY_AUDIO, timeout)) {
            ESP_LOGE(TAG, "Failed to verify frame and PTS");
            ret = -1;
        }
    } while (0);
    destroy_capture_sys(&capture_sys);
    return ret;
}

int advance_audio_only_path_test(int timeout, bool dual)
{
    capture_sys_t capture_sys = {0};
    int ret = 0;
    do {
        // Build up capture system
        ret = build_advance_audio_only_capture_sys(&capture_sys);
        BREAK_ON_FAIL(ret);

        esp_capture_sink_cfg_t sink_cfg = {
            .audio_info = {
                .format_id = ESP_CAPTURE_FMT_ID_AAC,
                .sample_rate = 48000,
                .channel = 2,
                .bits_per_sample = 16,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &capture_sys.capture_sink[0]);
        BREAK_ON_FAIL(ret);
        if (dual) {
            esp_capture_sink_cfg_t sink_cfg_1 = {
                .audio_info = {
                    .format_id = ESP_CAPTURE_FMT_ID_G711A,
                    .sample_rate = 8000,
                    .channel = 1,
                    .bits_per_sample = 16,
                },
            };
            ret = esp_capture_sink_setup(capture_sys.capture, 1, &sink_cfg_1, &capture_sys.capture_sink[1]);
            BREAK_ON_FAIL(ret);
        }
        ret = read_all_frames(&capture_sys, dual, timeout);
        BREAK_ON_FAIL(ret);
        if (!verify_test_result(&capture_sys, dual, TEST_RESULT_VERIFY_AUDIO, timeout)) {
            ESP_LOGE(TAG, "Failed to verify frame and PTS");
            ret = -1;
        }
    } while (0);
    destroy_capture_sys(&capture_sys);
    return ret;
}

int auto_video_only_path_test(int timeout, bool dual)
{
    capture_sys_t capture_sys = {0};
    int ret = 0;
    do {
        // Build up capture system
        ret = build_video_only_capture_sys(&capture_sys);
        BREAK_ON_FAIL(ret);

#ifndef CONFIG_IDF_TARGET_ESP32P4
        if (dual && capture_sys.vid_src) {
            esp_capture_video_info_t fixed_caps = {
                .format_id = ESP_CAPTURE_FMT_ID_RGB565,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            };
            capture_sys.vid_src->set_fixed_caps(capture_sys.vid_src, &fixed_caps);
        }
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
        esp_capture_sink_cfg_t sink_cfg = {
            .video_info = {
                .format_id = VIDEO_SINK_FMT_0,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &capture_sys.capture_sink[0]);
        BREAK_ON_FAIL(ret);
        if (dual) {
            esp_capture_sink_cfg_t sink_cfg_1 = {
                .video_info = {
                    .format_id = VIDEO_SINK_FMT_1,
                    .width = VIDEO_WIDTH,
                    .height = VIDEO_HEIGHT,
                    .fps = VIDEO_FPS / 2,
                },
            };
            ret = esp_capture_sink_setup(capture_sys.capture, 1, &sink_cfg_1, &capture_sys.capture_sink[1]);
            BREAK_ON_FAIL(ret);
        }
        ret = read_all_frames(&capture_sys, dual, timeout);
        BREAK_ON_FAIL(ret);
        if (!verify_test_result(&capture_sys, dual, TEST_RESULT_VERIFY_VIDEO, timeout)) {
            ESP_LOGE(TAG, "Failed to verify frame and PTS");
            ret = -1;
        }
    } while (0);
    destroy_capture_sys(&capture_sys);
    return ret;
}

int manual_video_only_path_test(int timeout, bool dual)
{
    capture_sys_t capture_sys = {0};
    int ret = 0;
    do {
        // Build up capture system
        ret = build_video_only_capture_sys(&capture_sys);
        BREAK_ON_FAIL(ret);
#ifndef CONFIG_IDF_TARGET_ESP32P4
        if (dual && capture_sys.vid_src) {
            esp_capture_video_info_t fixed_caps = {
                .format_id = ESP_CAPTURE_FMT_ID_RGB565,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            };
            capture_sys.vid_src->set_fixed_caps(capture_sys.vid_src, &fixed_caps);
        }
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
        esp_capture_sink_cfg_t sink_cfg = {
            .video_info = {
                .format_id = VIDEO_SINK_FMT_0,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &capture_sys.capture_sink[0]);
        BREAK_ON_FAIL(ret);
#if CONFIG_IDF_TARGET_ESP32P4
        // We know that only need encoder so we only add video encoder into it
        const char *vid_elements[] = {"vid_fps_cvt", "vid_enc"};
#else
        const char *vid_elements[] = {"vid_fps_cvt", "vid_color_cvt", "vid_enc"};
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
        ret = esp_capture_sink_build_pipeline(capture_sys.capture_sink[0], ESP_CAPTURE_STREAM_TYPE_VIDEO,
                                              vid_elements, ELEMS(vid_elements));
        BREAK_ON_FAIL(ret);
        if (dual) {
            esp_capture_sink_cfg_t sink_cfg_1 = {
                .video_info = {
                    .format_id = VIDEO_SINK_FMT_1,
                    .width = VIDEO_WIDTH,
                    .height = VIDEO_HEIGHT,
                    .fps = VIDEO_FPS / 2,
                },
            };
            ret = esp_capture_sink_setup(capture_sys.capture, 1, &sink_cfg_1, &capture_sys.capture_sink[1]);
            // TODO need test remove venc also works?
#if CONFIG_IDF_TARGET_ESP32P4
            const char *vid_elements_1[] = {"vid_fps_cvt", "vid_ppa", "vid_enc"};
#else
            const char *vid_elements_1[] = {"vid_fps_cvt", "vid_scale", "vid_color_cvt", "vid_enc"};
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
            ret = esp_capture_sink_build_pipeline(capture_sys.capture_sink[1], ESP_CAPTURE_STREAM_TYPE_VIDEO,
                                                  vid_elements_1, ELEMS(vid_elements_1));
            BREAK_ON_FAIL(ret);
        }
        ret = read_all_frames(&capture_sys, dual, timeout);
        BREAK_ON_FAIL(ret);
        if (!verify_test_result(&capture_sys, dual, TEST_RESULT_VERIFY_VIDEO, timeout)) {
            ESP_LOGE(TAG, "Failed to verify frame and PTS");
            ret = -1;
        }
    } while (0);
    destroy_capture_sys(&capture_sys);
    return ret;
}

int advance_video_only_path_test(int timeout, bool dual)
{
    capture_sys_t capture_sys = {0};
    int ret = 0;
    do {
        // Build up capture system
        ret = build_advance_video_only_capture_sys(&capture_sys);
        BREAK_ON_FAIL(ret);
#ifndef CONFIG_IDF_TARGET_ESP32P4
        if (capture_sys.vid_src) {
            esp_capture_video_info_t fixed_caps = {
                .format_id = ESP_CAPTURE_FMT_ID_RGB565,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            };
            capture_sys.vid_src->set_fixed_caps(capture_sys.vid_src, &fixed_caps);
        }
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */

        esp_capture_sink_cfg_t sink_cfg = {
            .video_info = {
                .format_id = VIDEO_SINK_FMT_0,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &capture_sys.capture_sink[0]);
        BREAK_ON_FAIL(ret);
        if (dual) {
            esp_capture_sink_cfg_t sink_cfg_1 = {
                .video_info = {
                    .format_id = VIDEO_SINK_FMT_1,
                    .width = VIDEO_WIDTH,
                    .height = VIDEO_HEIGHT,
                    .fps = VIDEO_FPS / 2,
                },
            };
            ret = esp_capture_sink_setup(capture_sys.capture, 1, &sink_cfg_1, &capture_sys.capture_sink[1]);
            BREAK_ON_FAIL(ret);
        }
        ret = read_all_frames(&capture_sys, dual, timeout);
        BREAK_ON_FAIL(ret);
        if (!verify_test_result(&capture_sys, dual, TEST_RESULT_VERIFY_VIDEO, timeout)) {
            ESP_LOGE(TAG, "Failed to verify frame and PTS");
            ret = -1;
        }
    } while (0);
    destroy_capture_sys(&capture_sys);
    return ret;
}

static esp_capture_err_t capture_event_hdlr(esp_capture_event_t event, void *ctx)
{
    capture_sys_t *capture_sys = (capture_sys_t *)ctx;
    switch (event) {
        default:
            break;
        case ESP_CAPTURE_EVENT_VIDEO_PIPELINE_BUILT: {
            // Do extra setting for video pipeline here
            if (VIDEO_SINK_FMT_0  == ESP_CAPTURE_FMT_ID_H264) {
                // Setting for GOP and QOP use video encoder element
                 esp_gmf_element_handle_t venc_hd = NULL;
                esp_capture_sink_get_element_by_tag(capture_sys->capture_sink[0], ESP_CAPTURE_STREAM_TYPE_VIDEO, "vid_enc", &venc_hd);
                if (venc_hd) {
                    esp_gmf_video_enc_set_bitrate(venc_hd, 2000000);
                    esp_gmf_video_enc_set_gop(venc_hd, 30);
                    esp_gmf_video_enc_set_qp(venc_hd, 10, 20);
                }
            }
            break;
        }
    }
    return ESP_CAPTURE_ERR_OK;
}


int auto_av_path_test(int timeout, bool dual)
{
    capture_sys_t capture_sys = {0};
    int ret = 0;
    do {
        // Build up capture system
        ret = build_av_capture_sys(&capture_sys);
        BREAK_ON_FAIL(ret);
#ifndef CONFIG_IDF_TARGET_ESP32P4
        if (dual && capture_sys.vid_src) {
            esp_capture_video_info_t fixed_caps = {
                .format_id = ESP_CAPTURE_FMT_ID_RGB565,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            };
            capture_sys.vid_src->set_fixed_caps(capture_sys.vid_src, &fixed_caps);
        }
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */

        esp_capture_sink_cfg_t sink_cfg = {
            .audio_info = {
                .format_id = ESP_CAPTURE_FMT_ID_AAC,
                .sample_rate = 48000,
                .channel = 2,
                .bits_per_sample = 16,
            },
            .video_info = {
                .format_id = VIDEO_SINK_FMT_0,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            },
        };
        ret = esp_capture_set_event_cb(capture_sys.capture, capture_event_hdlr, &capture_sys);
        BREAK_ON_FAIL(ret);
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &capture_sys.capture_sink[0]);
        BREAK_ON_FAIL(ret);
        // Do some pre-setting here
        uint32_t audio_bitrate = 48000 * 2 * 16 >> 4;
        ret = esp_capture_sink_set_bitrate(capture_sys.capture_sink[0], ESP_CAPTURE_STREAM_TYPE_AUDIO, audio_bitrate);
        BREAK_ON_FAIL(ret);
        uint32_t video_bitrate = VIDEO_WIDTH * VIDEO_HEIGHT * VIDEO_FPS >> 1;
        ret = esp_capture_sink_set_bitrate(capture_sys.capture_sink[0], ESP_CAPTURE_STREAM_TYPE_VIDEO, video_bitrate);
        BREAK_ON_FAIL(ret);
        if (dual) {
            esp_capture_sink_cfg_t sink_cfg_1 = {
                .audio_info = {
                    .format_id = ESP_CAPTURE_FMT_ID_G711A,
                    .sample_rate = 8000,
                    .channel = 1,
                    .bits_per_sample = 16,
                },
                .video_info = {
                    .format_id = VIDEO_SINK_FMT_1,
                    .width = VIDEO_WIDTH,
                    .height = VIDEO_HEIGHT,
                    .fps = VIDEO_FPS / 2,
                },
            };
            ret = esp_capture_sink_setup(capture_sys.capture, 1, &sink_cfg_1, &capture_sys.capture_sink[1]);
            BREAK_ON_FAIL(ret);
        }
        ret = read_all_frames(&capture_sys, dual, timeout);
        BREAK_ON_FAIL(ret);
        if (!verify_test_result(&capture_sys, dual, TEST_RESULT_VERIFY_VIDEO | TEST_RESULT_VERIFY_AUDIO, timeout)) {
            ESP_LOGE(TAG, "Failed to verify frame and PTS");
            ret = -1;
        }
        ESP_LOGW(TAG, "Rerun start and stop flow");
        // Restart
        ret = read_all_frames(&capture_sys, dual, timeout);
        BREAK_ON_FAIL(ret);
        if (!verify_test_result(&capture_sys, dual, TEST_RESULT_VERIFY_VIDEO | TEST_RESULT_VERIFY_AUDIO, timeout)) {
            ESP_LOGE(TAG, "Failed to verify frame and PTS");
            ret = -1;
        }
    } while (0);
    destroy_capture_sys(&capture_sys);
    return ret;
}

int auto_av_path_dynamic_enable_test(int timeout, bool dual)
{
    capture_sys_t capture_sys = {0};
    int ret = 0;
    do {
        // Build up capture system
        ret = build_av_capture_sys(&capture_sys);
        BREAK_ON_FAIL(ret);
#ifndef CONFIG_IDF_TARGET_ESP32P4
        if (dual && capture_sys.vid_src) {
            esp_capture_video_info_t fixed_caps = {
                .format_id = ESP_CAPTURE_FMT_ID_RGB565,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            };
            capture_sys.vid_src->set_fixed_caps(capture_sys.vid_src, &fixed_caps);
        }
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */

        esp_capture_sink_cfg_t sink_cfg = {
            .audio_info = {
                .format_id = ESP_CAPTURE_FMT_ID_AAC,
                .sample_rate = 48000,
                .channel = 2,
                .bits_per_sample = 16,
            },
            .video_info = {
                .format_id = VIDEO_SINK_FMT_0,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &capture_sys.capture_sink[0]);
        BREAK_ON_FAIL(ret);
        if (dual) {
            esp_capture_sink_cfg_t sink_cfg_1 = {
                .audio_info = {
                    .format_id = ESP_CAPTURE_FMT_ID_G711A,
                    .sample_rate = 8000,
                    .channel = 1,
                    .bits_per_sample = 16,
                },
                .video_info = {
                    .format_id = VIDEO_SINK_FMT_1,
                    .width = VIDEO_WIDTH,
                    .height = VIDEO_HEIGHT,
                    .fps = VIDEO_FPS / 2,
                },
            };
            ret = esp_capture_sink_setup(capture_sys.capture, 1, &sink_cfg_1, &capture_sys.capture_sink[1]);
            BREAK_ON_FAIL(ret);
        }
        if (dual) {
            // Enable 0 and disable 1
            if (capture_sys.capture_sink[0]) {
                ret = esp_capture_sink_enable(capture_sys.capture_sink[0], ESP_CAPTURE_RUN_MODE_ALWAYS);
                BREAK_ON_FAIL(ret);
            }
        }
        ret = esp_capture_start(capture_sys.capture);
        BREAK_ON_FAIL(ret);
        read_with_timeout(&capture_sys, dual, timeout);
        if (dual) {
            // Sink 0 have data sink not has
            ESP_LOGI(TAG, "Verify expect sink0 enabled sink1 disabled");
            ret = verify_test_result_for_path(&capture_sys, 0, TEST_RESULT_VERIFY_VIDEO | TEST_RESULT_VERIFY_AUDIO, true);
            BREAK_ON_FALSE(ret);
            ret = verify_test_result_for_path(&capture_sys, 1, TEST_RESULT_VERIFY_VIDEO | TEST_RESULT_VERIFY_AUDIO, false);
            BREAK_ON_FALSE(ret);
            esp_capture_sink_enable(capture_sys.capture_sink[0], ESP_CAPTURE_RUN_MODE_DISABLE);
            esp_capture_sink_enable(capture_sys.capture_sink[1], ESP_CAPTURE_RUN_MODE_ALWAYS);
        } else {
             // Sink 0 not enable yet
            ESP_LOGI(TAG, "Verify expect sink0 disabled");
            ret = verify_test_result_for_path(&capture_sys, 0, TEST_RESULT_VERIFY_VIDEO | TEST_RESULT_VERIFY_AUDIO, false);
            BREAK_ON_FALSE(ret);
            esp_capture_sink_enable(capture_sys.capture_sink[0], ESP_CAPTURE_RUN_MODE_ALWAYS);
        }

        read_with_timeout(&capture_sys, dual, timeout);
        if (dual) {
            // Sink 0 have data sink not has
            ESP_LOGI(TAG, "Verify expect sink0 disabled sink1 enabled");
            ret = verify_test_result_for_path(&capture_sys, 0, TEST_RESULT_VERIFY_VIDEO | TEST_RESULT_VERIFY_AUDIO, false);
            BREAK_ON_FALSE(ret);
            ret = verify_test_result_for_path(&capture_sys, 1, TEST_RESULT_VERIFY_VIDEO | TEST_RESULT_VERIFY_AUDIO, true);
            BREAK_ON_FALSE(ret);
            esp_capture_sink_enable(capture_sys.capture_sink[0], ESP_CAPTURE_RUN_MODE_ALWAYS);
            esp_capture_sink_enable(capture_sys.capture_sink[1], ESP_CAPTURE_RUN_MODE_DISABLE);
        } else {
             // Sink 0 not enable yet
            ESP_LOGI(TAG, "Verify expect sink0 enabled");
            ret = verify_test_result_for_path(&capture_sys, 0, TEST_RESULT_VERIFY_VIDEO | TEST_RESULT_VERIFY_AUDIO, true);
            BREAK_ON_FALSE(ret);
            esp_capture_sink_enable(capture_sys.capture_sink[0], ESP_CAPTURE_RUN_MODE_DISABLE);
        }
        read_with_timeout(&capture_sys, dual, timeout);
        if (dual) {
            // Sink 0 have data sink not has
            ESP_LOGI(TAG, "Verify expect sink0 enable sink1 disable");
            ret = verify_test_result_for_path(&capture_sys, 0, TEST_RESULT_VERIFY_VIDEO | TEST_RESULT_VERIFY_AUDIO, true);
            BREAK_ON_FALSE(ret);
            ret = verify_test_result_for_path(&capture_sys, 1, TEST_RESULT_VERIFY_VIDEO | TEST_RESULT_VERIFY_AUDIO, false);
            BREAK_ON_FALSE(ret);
        } else {
             // Sink 0 not enable yet
            ESP_LOGI(TAG, "Verify expect sink0 disabled");
            ret = verify_test_result_for_path(&capture_sys, 0, TEST_RESULT_VERIFY_VIDEO | TEST_RESULT_VERIFY_AUDIO, false);
            BREAK_ON_FALSE(ret);
        }
        ret = 0;
        esp_capture_stop(capture_sys.capture);
    } while (0);
    destroy_capture_sys(&capture_sys);
    return ret;
}

int manual_av_path_test(int timeout, bool dual)
{
    capture_sys_t capture_sys = {0};
    int ret = 0;
    do {
        // Build up capture system
        ret = build_av_capture_sys(&capture_sys);
        BREAK_ON_FAIL(ret);
#ifndef CONFIG_IDF_TARGET_ESP32P4
        if (dual && capture_sys.vid_src) {
            esp_capture_video_info_t fixed_caps = {
                .format_id = ESP_CAPTURE_FMT_ID_RGB565,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            };
            capture_sys.vid_src->set_fixed_caps(capture_sys.vid_src, &fixed_caps);
        }
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */

        esp_capture_sink_cfg_t sink_cfg = {
            .audio_info = {
                .format_id = ESP_CAPTURE_FMT_ID_AAC,
                .sample_rate = 48000,
                .channel = 2,
                .bits_per_sample = 16,
            },
            .video_info = {
                .format_id = VIDEO_SINK_FMT_0,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &capture_sys.capture_sink[0]);
        BREAK_ON_FAIL(ret);
        const char *aud_elements[] = {"aud_ch_cvt", "aud_rate_cvt", "aud_enc"};
        ret = esp_capture_sink_build_pipeline(capture_sys.capture_sink[0], ESP_CAPTURE_STREAM_TYPE_AUDIO,
                                              aud_elements, ELEMS(aud_elements));
        BREAK_ON_FAIL(ret);
#if CONFIG_IDF_TARGET_ESP32P4
        // We know that only need encoder so we only add video encoder into it
        const char *vid_elements[] = {"vid_fps_cvt", "vid_enc"};
#else
        const char *vid_elements[] = {"vid_fps_cvt", "vid_color_cvt", "vid_enc"};
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
        ret = esp_capture_sink_build_pipeline(capture_sys.capture_sink[0], ESP_CAPTURE_STREAM_TYPE_VIDEO,
                                              vid_elements, ELEMS(vid_elements));
        BREAK_ON_FAIL(ret);
        if (dual) {
            esp_capture_sink_cfg_t sink_cfg_1 = {
                .audio_info = {
                    .format_id = ESP_CAPTURE_FMT_ID_G711A,
                    .sample_rate = 8000,
                    .channel = 1,
                    .bits_per_sample = 16,
                },
                .video_info = {
                    .format_id = VIDEO_SINK_FMT_1,
                    .width = VIDEO_WIDTH,
                    .height = VIDEO_HEIGHT,
                    .fps = VIDEO_FPS / 2,
                },
            };
            ret = esp_capture_sink_setup(capture_sys.capture, 1, &sink_cfg_1, &capture_sys.capture_sink[1]);
            const char *aud_elements_1[] = {"aud_ch_cvt", "aud_rate_cvt", "aud_enc"};
            ret = esp_capture_sink_build_pipeline(capture_sys.capture_sink[1], ESP_CAPTURE_STREAM_TYPE_AUDIO,
                                                  aud_elements_1, ELEMS(aud_elements_1));
            BREAK_ON_FAIL(ret);
            // We know that only need encoder so we only add video encoder into it
#if CONFIG_IDF_TARGET_ESP32P4
            const char *vid_elements_1[] = {"vid_fps_cvt", "vid_ppa", "vid_enc"};
#else
            const char *vid_elements_1[] = {"vid_fps_cvt", "vid_scale", "vid_color_cvt", "vid_enc"};
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
            ret = esp_capture_sink_build_pipeline(capture_sys.capture_sink[1], ESP_CAPTURE_STREAM_TYPE_VIDEO,
                                                  vid_elements_1, ELEMS(vid_elements_1));
            BREAK_ON_FAIL(ret);
        }
        ret = read_all_frames(&capture_sys, dual, timeout);
        BREAK_ON_FAIL(ret);
        if (!verify_test_result(&capture_sys, dual, TEST_RESULT_VERIFY_VIDEO | TEST_RESULT_VERIFY_AUDIO, timeout)) {
            ESP_LOGE(TAG, "Failed to verify frame and PTS");
            ret = -1;
        }
        // Restart again
        ret = read_all_frames(&capture_sys, dual, timeout);
        BREAK_ON_FAIL(ret);
        if (!verify_test_result(&capture_sys, dual, TEST_RESULT_VERIFY_VIDEO | TEST_RESULT_VERIFY_AUDIO, timeout)) {
            ESP_LOGE(TAG, "Failed to verify frame and PTS");
            ret = -1;
        }
    } while (0);
    destroy_capture_sys(&capture_sys);
    return ret;
}

int advance_av_path_test(int timeout, bool dual)
{
    capture_sys_t capture_sys = {0};
    int ret = 0;
    do {
        // Build up capture system
        ret = build_advance_av_capture_sys(&capture_sys);
        BREAK_ON_FAIL(ret);
#ifndef CONFIG_IDF_TARGET_ESP32P4
        if (capture_sys.vid_src) {
            esp_capture_video_info_t fixed_caps = {
                .format_id = ESP_CAPTURE_FMT_ID_RGB565,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            };
            capture_sys.vid_src->set_fixed_caps(capture_sys.vid_src, &fixed_caps);
        }
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */

        esp_capture_sink_cfg_t sink_cfg = {
            .audio_info = {
                .format_id = ESP_CAPTURE_FMT_ID_AAC,
                .sample_rate = 48000,
                .channel = 2,
                .bits_per_sample = 16,
            },
            .video_info = {
                .format_id = VIDEO_SINK_FMT_0,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &capture_sys.capture_sink[0]);
        BREAK_ON_FAIL(ret);
        if (dual) {
            esp_capture_sink_cfg_t sink_cfg_1 = {
                .audio_info = {
                    .format_id = ESP_CAPTURE_FMT_ID_G711A,
                    .sample_rate = 8000,
                    .channel = 1,
                    .bits_per_sample = 16,
                },
                .video_info = {
                    .format_id = VIDEO_SINK_FMT_1,
                    .width = VIDEO_WIDTH,
                    .height = VIDEO_HEIGHT,
                    .fps = VIDEO_FPS / 2,
                },
            };
            ret = esp_capture_sink_setup(capture_sys.capture, 1, &sink_cfg_1, &capture_sys.capture_sink[1]);
            BREAK_ON_FAIL(ret);
        }
        ret = read_all_frames(&capture_sys, dual, timeout);
        BREAK_ON_FAIL(ret);
        if (!verify_test_result(&capture_sys, dual, TEST_RESULT_VERIFY_VIDEO | TEST_RESULT_VERIFY_AUDIO, timeout)) {
            ESP_LOGE(TAG, "Failed to verify frame and PTS");
            ret = -1;
        }
    } while (0);
    destroy_capture_sys(&capture_sys);
    return ret;
}

int auto_av_muxer_path_test(int timeout, bool dual)
{
    capture_sys_t capture_sys = {0};
    int ret = 0;
    do {
        // Build up capture system
        ret = build_av_capture_sys(&capture_sys);
        BREAK_ON_FAIL(ret);
        // Video source force to output RGB565 for currently not support convert from YUV422 to RGB565 use esp_camera
#ifndef CONFIG_IDF_TARGET_ESP32P4
        if (dual && capture_sys.vid_src) {
            esp_capture_video_info_t fixed_caps = {
                .format_id = ESP_CAPTURE_FMT_ID_RGB565,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            };
            capture_sys.vid_src->set_fixed_caps(capture_sys.vid_src, &fixed_caps);
        }
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */

        esp_capture_sink_cfg_t sink_cfg = {
            .audio_info = {
                .format_id = ESP_CAPTURE_FMT_ID_AAC,
                .sample_rate = 16000,
                .channel = 1,
                .bits_per_sample = 16,
            },
            .video_info = {
                .format_id = VIDEO_SINK_FMT_0,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &capture_sys.capture_sink[0]);
        BREAK_ON_FAIL(ret);
        // Currently only callback to user not store into file so not set url pattern
        ts_muxer_config_t ts_cfg = {
            .base_config = {
                .muxer_type = ESP_MUXER_TYPE_TS,
            },
        };
        esp_capture_muxer_cfg_t muxer_cfg = {
            .base_config = &ts_cfg.base_config,
            .cfg_size = sizeof(ts_cfg),
        };
        ret = esp_capture_sink_add_muxer(capture_sys.capture_sink[0], &muxer_cfg);
        BREAK_ON_FAIL(ret);
        esp_capture_sink_enable_muxer(capture_sys.capture_sink[0], true);
        if (dual) {
            esp_capture_sink_cfg_t sink_cfg_1 = {
                .audio_info = {
                    .format_id = ESP_CAPTURE_FMT_ID_AAC,
                    .sample_rate = 32000,
                    .channel = 1,
                    .bits_per_sample = 16,
                },
                .video_info = {
                    .format_id = VIDEO_SINK_FMT_1,
#ifdef CONFIG_IDF_TARGET_ESP32P4
                    .width = VIDEO_WIDTH / 2,
                    .height = VIDEO_HEIGHT / 2,
#else
                    .width = VIDEO_WIDTH,
                    .height = VIDEO_HEIGHT,
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
                    .fps = VIDEO_FPS / 2,
                },
            };
            ret = esp_capture_sink_setup(capture_sys.capture, 1, &sink_cfg_1, &capture_sys.capture_sink[1]);
            BREAK_ON_FAIL(ret);
            ret = esp_capture_sink_add_muxer(capture_sys.capture_sink[1], &muxer_cfg);
            BREAK_ON_FAIL(ret);
            esp_capture_sink_enable_muxer(capture_sys.capture_sink[1], true);
        }
        esp_capture_enable_perf_monitor(true);
        ret = read_all_frames(&capture_sys, dual, timeout);
        BREAK_ON_FAIL(ret);
        esp_capture_enable_perf_monitor(false);
        // Check for out
        if (!verify_test_result(&capture_sys, dual,
                                TEST_RESULT_VERIFY_VIDEO | TEST_RESULT_VERIFY_AUDIO | TEST_RESULT_VERIFY_MUXER,
                                timeout)) {
            ESP_LOGE(TAG, "Failed to verify frame and PTS");
            ret = -1;
        }
    } while (0);
    destroy_capture_sys(&capture_sys);
    return ret;
}

int advance_av_muxer_path_test(int timeout, bool dual)
{
    capture_sys_t capture_sys = {0};
    int ret = 0;
    do {
        // Build up capture system
        ret = build_advance_av_capture_sys(&capture_sys);
        BREAK_ON_FAIL(ret);
#ifndef CONFIG_IDF_TARGET_ESP32P4
        if (capture_sys.vid_src) {
            esp_capture_video_info_t fixed_caps = {
                .format_id = ESP_CAPTURE_FMT_ID_RGB565,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            };
            capture_sys.vid_src->set_fixed_caps(capture_sys.vid_src, &fixed_caps);
        }
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */

        esp_capture_sink_cfg_t sink_cfg = {
            .audio_info = {
                .format_id = ESP_CAPTURE_FMT_ID_AAC,
                .sample_rate = 16000,
                .channel = 1,
                .bits_per_sample = 16,
            },
            .video_info = {
                .format_id = VIDEO_SINK_FMT_0,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &capture_sys.capture_sink[0]);
        BREAK_ON_FAIL(ret);
        // Currently only callback to user not store into file so not set url pattern
        ts_muxer_config_t ts_cfg = {
            .base_config = {
                .muxer_type = ESP_MUXER_TYPE_TS,
            },
        };
        esp_capture_muxer_cfg_t muxer_cfg = {
            .base_config = &ts_cfg.base_config,
            .cfg_size = sizeof(ts_cfg),
        };
        ret = esp_capture_sink_add_muxer(capture_sys.capture_sink[0], &muxer_cfg);
        BREAK_ON_FAIL(ret);
        esp_capture_sink_enable_muxer(capture_sys.capture_sink[0], true);
        if (dual) {
            esp_capture_sink_cfg_t sink_cfg_1 = {
                .audio_info = {
                    .format_id = ESP_CAPTURE_FMT_ID_AAC,
                    .sample_rate = 32000,
                    .channel = 1,
                    .bits_per_sample = 16,
                },
                .video_info = {
                    .format_id = VIDEO_SINK_FMT_1,
#ifdef CONFIG_IDF_TARGET_ESP32P4
                    .width = VIDEO_WIDTH / 2,
                    .height = VIDEO_HEIGHT / 2,
#else
                    .width = VIDEO_WIDTH,
                    .height = VIDEO_HEIGHT,
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
                    .fps = VIDEO_FPS / 2,
                },
            };
            ret = esp_capture_sink_setup(capture_sys.capture, 1, &sink_cfg_1, &capture_sys.capture_sink[1]);
            BREAK_ON_FAIL(ret);
            ret = esp_capture_sink_add_muxer(capture_sys.capture_sink[1], &muxer_cfg);
            BREAK_ON_FAIL(ret);
            esp_capture_sink_enable_muxer(capture_sys.capture_sink[1], true);
        }
        ret = read_all_frames(&capture_sys, dual, timeout);
        BREAK_ON_FAIL(ret);
        if (!verify_test_result(&capture_sys, dual,
                                TEST_RESULT_VERIFY_VIDEO | TEST_RESULT_VERIFY_AUDIO | TEST_RESULT_VERIFY_MUXER,
                                timeout)) {
            ESP_LOGE(TAG, "Failed to verify frame and PTS");
            ret = -1;
        }
    } while (0);
    destroy_capture_sys(&capture_sys);
    return ret;
}

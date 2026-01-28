/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_timer.h"
#include "settings.h"
#include "esp_capture.h"
#include "esp_capture_defaults.h"
#include "esp_capture_sink.h"
// For advanced usage like customized process pipeline
#include "esp_capture_advance.h"
#include "esp_gmf_alc.h"
#include "mp4_muxer.h"
#include "esp_board_device.h"
#include "esp_board_manager_defs.h"
#include "dev_audio_codec.h"
#include "esp_log.h"

#define TAG "AUDIO_CAPTURE"

typedef struct {
    esp_capture_handle_t         capture;  /*!< Capture handle */
    esp_capture_audio_src_if_t  *aud_src;  /*!< Audio source interface */
} audio_capture_sys_t;

static int build_audio_capture(audio_capture_sys_t *capture_sys, bool with_aec)
{
    // Create audio source firstly
    dev_audio_codec_handles_t *codec_handle = NULL;
    esp_err_t ret = esp_board_device_get_handle(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, (void **)&codec_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get audio device");
        return -1;
    }
    // record_handle can be either get from esp_bsp by API `bsp_audio_codec_speaker_init` or use simple `codec_board` API
    if (with_aec) {
        // Test AEC source on esp32s3 and esp32p4
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
        esp_capture_audio_aec_src_cfg_t aec_cfg = {
            .record_handle = codec_handle->codec_dev,
#if CONFIG_IDF_TARGET_ESP32S3
            .channel = 4,
            .channel_mask = 1 | 2,
#endif  /* CONFIG_IDF_TARGET_ESP32S3 */
        };
        capture_sys->aud_src = esp_capture_new_audio_aec_src(&aec_cfg);
#else
        with_aec = false;
#endif  /* CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4 */
    }
    if (with_aec == false) {
        esp_capture_audio_dev_src_cfg_t codec_cfg = {
            .record_handle = codec_handle->codec_dev,
        };
        capture_sys->aud_src = esp_capture_new_audio_dev_src(&codec_cfg);
    }
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

static void destroy_audio_capture(audio_capture_sys_t *capture_sys)
{
    if (capture_sys->capture) {
        esp_capture_close(capture_sys->capture);
        capture_sys->capture = NULL;
    }
    if (capture_sys->aud_src) {
        free(capture_sys->aud_src);
        capture_sys->aud_src = NULL;
    }
}

static int read_audio_frames(esp_capture_sink_handle_t sink, int duration)
{
    uint32_t start_time = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t cur_time = start_time;
    uint32_t total_frames = 0;
    uint32_t total_frame_size = 0;
    // Read frames until duration reached
    esp_capture_stream_frame_t frame = {
        .stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO,
    };
    int ret = 0;
    do {
        // Here use wait read, if want to use no-wait acquire, change last param to `true` (need sleep before retry)
        ret = esp_capture_sink_acquire_frame(sink, &frame, false);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to acquire audio frame return %d", ret);
            ret = -1;
            break;
        }
        // Add some status monitor code
        total_frames++;
        total_frame_size += frame.size;
        esp_capture_sink_release_frame(sink, &frame);
        cur_time = (uint32_t)(esp_timer_get_time() / 1000);
    } while (cur_time < start_time + duration);
    ESP_LOGI(TAG, "Read frame get frames %lu/%lu in %dms", total_frame_size, total_frames, duration);
    return ret;
}

int audio_capture_run(int duration)
{
    audio_capture_sys_t capture_sys = {0};
    do {
        // Build capture system
        int ret = build_audio_capture(&capture_sys, false);
        if (ret != 0) {
            break;
        }
        // Setup for sink
        esp_capture_sink_handle_t sink = NULL;
        esp_capture_sink_cfg_t sink_cfg = {
            .audio_info = {
                .format_id = AUDIO_CAPTURE_FORMAT,
                .sample_rate = AUDIO_CAPTURE_SAMPLE_RATE,
                .channel = AUDIO_CAPTURE_CHANNEL,
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
            ESP_LOGE(TAG, "Fail to start audio capture");
            break;
        }
        read_audio_frames(sink, duration);
        ret = esp_capture_stop(capture_sys.capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to start audio capture");
            break;
        }
    } while (0);
    destroy_audio_capture(&capture_sys);
    return 0;
}

int audio_capture_run_with_aec(int duration)
{
    audio_capture_sys_t capture_sys = {0};
    int ret = 0;
    do {
        // Build capture system with AEC
        ret = build_audio_capture(&capture_sys, true);
        if (ret != 0) {
            break;
        }

        // Setup for sink
        esp_capture_sink_handle_t sink = NULL;
        esp_capture_sink_cfg_t sink_cfg = {
            .audio_info = {
                .format_id = AUDIO_CAPTURE_FORMAT,
                .sample_rate = AUDIO_CAPTURE_SAMPLE_RATE,
                .channel = AUDIO_CAPTURE_CHANNEL,
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
            ESP_LOGE(TAG, "Fail to start audio capture");
            break;
        }

        // Read audio frame until duration reached
        read_audio_frames(sink, duration);

        // Stop capture
        ret = esp_capture_stop(capture_sys.capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to start audio capture");
            break;
        }
    } while (0);
    destroy_audio_capture(&capture_sys);
    return 0;
}

#define FILE_SLICE_STORAGE_PATTERN "/sdcard/aud_%d.mp4"

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

int audio_capture_run_with_muxer(int duration)
{
    audio_capture_sys_t capture_sys = {0};
    int ret = 0;
    do {
        // Build capture system with AEC
        ret = build_audio_capture(&capture_sys, false);
        if (ret != 0) {
            break;
        }
        // Setup for sink
        esp_capture_sink_handle_t sink = NULL;
        esp_capture_sink_cfg_t sink_cfg = {
            .audio_info = {
                .format_id = AUDIO_CAPTURE_FORMAT,
                .sample_rate = AUDIO_CAPTURE_SAMPLE_RATE,
                .channel = AUDIO_CAPTURE_CHANNEL,
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
            ESP_LOGE(TAG, "Fail to start audio capture");
            break;
        }
        read_audio_frames(sink, duration);
        ret = esp_capture_stop(capture_sys.capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to start audio capture");
            break;
        }
        check_file_size(0);
    } while (0);
    destroy_audio_capture(&capture_sys);
    return ret;
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

int audio_capture_run_with_customized_process(int duration)
{
    audio_capture_sys_t capture_sys = {0};
    int ret = 0;
    do {
        // Build capture system
        ret = build_audio_capture(&capture_sys, false);
        if (ret != 0) {
            break;
        }
        // Setup for sink
        esp_capture_sink_handle_t sink = NULL;
        esp_capture_sink_cfg_t sink_cfg = {
            .audio_info = {
                .format_id = AUDIO_CAPTURE_FORMAT,
                .sample_rate = AUDIO_CAPTURE_SAMPLE_RATE,
                .channel = AUDIO_CAPTURE_CHANNEL,
                .bits_per_sample = 16,
            },
        };
        ret = esp_capture_sink_setup(capture_sys.capture, 0, &sink_cfg, &sink);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to setup sink");
            break;
        }
        // Following code demo how to add other elements into auto capture pool which contain
        // Add ALC element into capture POOL
        // Once element register success, capture take over the control of alc
        esp_ae_alc_cfg_t alc_cfg = DEFAULT_ESP_GMF_ALC_CONFIG();
        esp_gmf_element_handle_t alc_hd = NULL;
        esp_gmf_alc_init(&alc_cfg, &alc_hd);
        ret = esp_capture_register_element(capture_sys.capture, ESP_CAPTURE_STREAM_TYPE_AUDIO, alc_hd);
        if (ret != ESP_CAPTURE_ERR_OK) {
            esp_gmf_obj_delete(alc_hd);
            break;
        }
        esp_capture_set_event_cb(capture_sys.capture, demo_custom_pipe_event_hdlr, sink);
        const char *aud_elements[] = {"aud_ch_cvt", "aud_rate_cvt", "aud_alc", "aud_enc"};
        ret = esp_capture_sink_build_pipeline(sink, ESP_CAPTURE_STREAM_TYPE_AUDIO, aud_elements,
                                              sizeof(aud_elements) / sizeof(aud_elements[0]));
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Failed to do manually build pipeline");
            break;
        }

        // Enable sink and start
        esp_capture_sink_enable(sink, ESP_CAPTURE_RUN_MODE_ALWAYS);
        ret = esp_capture_start(capture_sys.capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to start audio capture");
            break;
        }
        read_audio_frames(sink, duration);
        ret = esp_capture_stop(capture_sys.capture);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to start audio capture");
            break;
        }
    } while (0);
    destroy_audio_capture(&capture_sys);
    return ret;
}

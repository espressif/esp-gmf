/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Multiple sinks with dynamic enable / one-shot. Edit CAPTURE_SINKS_SETTINGS in settings.h.
 *
 * Scenarios (shared build; each case does enable → start → run → stop):
 *   1) Streaming only
 *   2) Streaming + display (mute / unmute stream)
 *   3) Streaming + JPEG one-shot (fast)
 *   4) Streaming + JPEG one-shot (low mem: enable only when needed)
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "esp_board_device.h"
#include "esp_board_periph.h"
#include "esp_board_manager_defs.h"
#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
#include "dev_camera.h"
#endif
#include "dev_audio_codec.h"

#include "esp_capture.h"
#include "esp_capture_defaults.h"
#include "esp_capture_sink.h"
#include "esp_video_enc_default.h"
#include "esp_audio_enc_default.h"
#include "settings.h"

#define TAG "MULTI_SINK"
#define CAM_FB_NUM  2

static const esp_capture_sink_cfg_t s_sink_cfg[] = CAPTURE_SINKS_SETTINGS;
#define SINK_NUM  (sizeof(s_sink_cfg) / sizeof(s_sink_cfg[0]))

_Static_assert(SINK_NUM <= CONFIG_ESP_CAPTURE_MAX_SINK_NUM,
               "CAPTURE_SINKS_SETTINGS count exceeds CONFIG_ESP_CAPTURE_MAX_SINK_NUM");

typedef struct {
    esp_capture_handle_t         capture;
    esp_capture_video_src_if_t  *vid_src;
    esp_capture_audio_src_if_t  *aud_src;
    esp_capture_sink_handle_t    sink[SINK_NUM];
} capture_sys_t;

typedef struct {
    uint32_t ok;
    uint32_t bad;
    uint32_t frames;
} sink_stats_t;

static bool frame_ok_h264(const uint8_t *d, int n)
{
    if (n < 4 || d == NULL) {
        return false;
    }
    if (d[0] != 0 || d[1] != 0) {
        return false;
    }
    return (d[2] == 1 || (d[2] == 0 && d[3] == 1));
}

static bool frame_ok_jpeg(const uint8_t *d, int n)
{
    if (n < 4 || d == NULL) {
        return false;
    }
    return (d[0] == 0xFF && d[1] == 0xD8 && d[n - 2] == 0xFF && d[n - 1] == 0xD9);
}

static bool frame_ok_rgb565(const uint8_t *d, int n, uint16_t w, uint16_t h)
{
    (void)d;
    return (n > 0 && (uint32_t)n == (uint32_t)w * h * 2);
}

static bool verify_video(uint8_t sink_idx, const esp_capture_stream_frame_t *f)
{
    esp_capture_format_id_t fmt = s_sink_cfg[sink_idx].video_info.format_id;
    if (fmt == ESP_CAPTURE_FMT_ID_H264) {
        return frame_ok_h264(f->data, f->size);
    }
    if (fmt == ESP_CAPTURE_FMT_ID_MJPEG) {
        return frame_ok_jpeg(f->data, f->size);
    }
    if (fmt == ESP_CAPTURE_FMT_ID_RGB565) {
        return frame_ok_rgb565(f->data, f->size,
                               s_sink_cfg[sink_idx].video_info.width,
                               s_sink_cfg[sink_idx].video_info.height);
    }
    return (f->size > 0);
}

static esp_capture_video_src_if_t *create_video_source(void)
{
#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
    dev_camera_handle_t *cam = NULL;
    if (esp_board_device_get_handle(ESP_BOARD_DEVICE_NAME_CAMERA, (void **)&cam) != ESP_OK ||
            cam == NULL || cam->dev_path == NULL) {
        return NULL;
    }
    esp_capture_video_v4l2_src_cfg_t cfg = {.buf_count = CAM_FB_NUM};
    strncpy(cfg.dev_name, cam->dev_path, sizeof(cfg.dev_name) - 1);
    return esp_capture_new_video_v4l2_src(&cfg);
#else
    return NULL;
#endif
}

static esp_capture_audio_src_if_t *create_audio_source(void)
{
    dev_audio_codec_handles_t *codec = NULL;
    if (esp_board_device_get_handle(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, (void **)&codec) != ESP_OK ||
            codec == NULL || codec->codec_dev == NULL) {
        return NULL;
    }
    esp_capture_audio_dev_src_cfg_t cfg = {.record_handle = codec->codec_dev};
    return esp_capture_new_audio_dev_src(&cfg);
}

static int build_capture(capture_sys_t *sys)
{
    memset(sys, 0, sizeof(*sys));
    sys->vid_src = create_video_source();
    if (sys->vid_src == NULL) {
        ESP_LOGE(TAG, "No camera");
        return -1;
    }
    sys->aud_src = create_audio_source();

    esp_capture_video_info_t caps = {
        .format_id = ESP_CAPTURE_FMT_ID_RGB565,
        .width = s_sink_cfg[SINK_STREAM].video_info.width,
        .height = s_sink_cfg[SINK_STREAM].video_info.height,
        .fps = s_sink_cfg[SINK_STREAM].video_info.fps,
    };
    sys->vid_src->set_fixed_caps(sys->vid_src, &caps);

    esp_capture_cfg_t cfg = {
        .sync_mode = sys->aud_src ? ESP_CAPTURE_SYNC_MODE_AUDIO : ESP_CAPTURE_SYNC_MODE_SYSTEM,
        .audio_src = sys->aud_src,
        .video_src = sys->vid_src,
    };
    if (esp_capture_open(&cfg, &sys->capture) != ESP_CAPTURE_ERR_OK) {
        return -1;
    }

    for (uint8_t i = 0; i < SINK_NUM; i++) {
        esp_capture_sink_cfg_t scfg = s_sink_cfg[i];
        if (esp_capture_sink_setup(sys->capture, i, &scfg, &sys->sink[i]) != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "sink_setup %u failed", i);
            return -1;
        }
    }
    return 0;
}

static void destroy_capture(capture_sys_t *sys)
{
    if (sys->capture) {
        esp_capture_close(sys->capture);
    }
    free(sys->aud_src);
    free(sys->vid_src);
    memset(sys, 0, sizeof(*sys));
}

static void drain_sink(capture_sys_t *sys, uint8_t idx, sink_stats_t *st, bool want_audio)
{
    esp_capture_stream_frame_t frame = {0};
    if (want_audio) {
        frame.stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO;
        while (esp_capture_sink_acquire_frame(sys->sink[idx], &frame, true) == ESP_CAPTURE_ERR_OK) {
            st->frames++;
            if (frame.size > 0) {
                st->ok++;
            } else {
                st->bad++;
            }
            esp_capture_sink_release_frame(sys->sink[idx], &frame);
        }
    }
    frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;
    while (esp_capture_sink_acquire_frame(sys->sink[idx], &frame, true) == ESP_CAPTURE_ERR_OK) {
        st->frames++;
        if (verify_video(idx, &frame)) {
            st->ok++;
        } else {
            st->bad++;
            ESP_LOGW(TAG, "sink%u bad video size=%d", idx, frame.size);
        }
        esp_capture_sink_release_frame(sys->sink[idx], &frame);
    }
}

static void run_for(capture_sys_t *sys, int ms, bool stream, bool disp)
{
    sink_stats_t st[SINK_NUM] = {0};
    int64_t end = esp_timer_get_time() + (int64_t)ms * 1000;
    while (esp_timer_get_time() < end) {
        if (stream) {
            drain_sink(sys, SINK_STREAM, &st[SINK_STREAM], true);
        }
        if (disp) {
            drain_sink(sys, SINK_DISP, &st[SINK_DISP], false);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (stream) {
        ESP_LOGI(TAG, "  stream ok/bad/frames=%lu/%lu/%lu",
                 (unsigned long)st[SINK_STREAM].ok, (unsigned long)st[SINK_STREAM].bad,
                 (unsigned long)st[SINK_STREAM].frames);
    }
    if (disp) {
        ESP_LOGI(TAG, "  display ok/bad/frames=%lu/%lu/%lu",
                 (unsigned long)st[SINK_DISP].ok, (unsigned long)st[SINK_DISP].bad,
                 (unsigned long)st[SINK_DISP].frames);
    }
}

static int oneshot_jpeg(capture_sys_t *sys)
{
    esp_capture_sink_enable(sys->sink[SINK_SNAP], ESP_CAPTURE_RUN_MODE_ONESHOT);
    sink_stats_t st = {0};
    esp_capture_stream_frame_t frame = {.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO};
    int64_t end = esp_timer_get_time() + 5000 * 1000LL;
    while (esp_timer_get_time() < end) {
        drain_sink(sys, SINK_STREAM, &st, true);
        if (esp_capture_sink_acquire_frame(sys->sink[SINK_SNAP], &frame, true) == ESP_CAPTURE_ERR_OK) {
            bool ok = verify_video(SINK_SNAP, &frame);
            ESP_LOGI(TAG, "  JPEG size=%d %s", frame.size, ok ? "OK" : "BAD");
            esp_capture_sink_release_frame(sys->sink[SINK_SNAP], &frame);
            return ok ? 0 : -1;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGW(TAG, "  JPEG timeout");
    return -1;
}

static void scenario_streaming_only(capture_sys_t *sys)
{
    ESP_LOGI(TAG, "======== 1: streaming only ========");
    /* Step: enable streaming only, then start. */
    esp_capture_sink_enable(sys->sink[SINK_STREAM], ESP_CAPTURE_RUN_MODE_ALWAYS);
    esp_capture_sink_enable(sys->sink[SINK_SNAP], ESP_CAPTURE_RUN_MODE_DISABLE);
    esp_capture_sink_enable(sys->sink[SINK_DISP], ESP_CAPTURE_RUN_MODE_DISABLE);
    if (esp_capture_start(sys->capture) != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(TAG, "start failed");
        return;
    }
    run_for(sys, 30000, true, false);
    esp_capture_stop(sys->capture);
}

static void scenario_stream_and_display(capture_sys_t *sys)
{
    ESP_LOGI(TAG, "======== 2: streaming + display ========");
    /* Step: enable stream + display, then start. */
    esp_capture_sink_enable(sys->sink[SINK_STREAM], ESP_CAPTURE_RUN_MODE_ALWAYS);
    esp_capture_sink_enable(sys->sink[SINK_SNAP], ESP_CAPTURE_RUN_MODE_DISABLE);
    esp_capture_sink_enable(sys->sink[SINK_DISP], ESP_CAPTURE_RUN_MODE_ALWAYS);
    if (esp_capture_start(sys->capture) != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(TAG, "start failed");
        return;
    }

    ESP_LOGI(TAG, "Both 30s");
    run_for(sys, 30000, true, true);

    ESP_LOGI(TAG, "Mute stream 10s");
    esp_capture_sink_enable(sys->sink[SINK_STREAM], ESP_CAPTURE_RUN_MODE_DISABLE);
    run_for(sys, 10000, false, true);

    ESP_LOGI(TAG, "Unmute stream 10s");
    esp_capture_sink_enable(sys->sink[SINK_STREAM], ESP_CAPTURE_RUN_MODE_ALWAYS);
    run_for(sys, 10000, true, true);

    esp_capture_stop(sys->capture);
}

static void scenario_oneshot_fast(capture_sys_t *sys)
{
    ESP_LOGI(TAG, "======== 3: stream + JPEG one-shot (fast) ========");
    /* Step: stream always + arm first one-shot, then start. */
    esp_capture_sink_enable(sys->sink[SINK_STREAM], ESP_CAPTURE_RUN_MODE_ALWAYS);
    esp_capture_sink_enable(sys->sink[SINK_SNAP], ESP_CAPTURE_RUN_MODE_ONESHOT);
    esp_capture_sink_enable(sys->sink[SINK_DISP], ESP_CAPTURE_RUN_MODE_DISABLE);
    if (esp_capture_start(sys->capture) != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(TAG, "start failed");
        return;
    }

    ESP_LOGI(TAG, "Wait first JPEG within 10s");
    {
        sink_stats_t st = {0};
        bool got = false;
        int64_t end = esp_timer_get_time() + 10000 * 1000LL;
        while (esp_timer_get_time() < end) {
            drain_sink(sys, SINK_STREAM, &st, true);
            if (!got) {
                esp_capture_stream_frame_t f = {.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO};
                if (esp_capture_sink_acquire_frame(sys->sink[SINK_SNAP], &f, true) == ESP_CAPTURE_ERR_OK) {
                    ESP_LOGI(TAG, "  JPEG#1 size=%d %s", f.size,
                             verify_video(SINK_SNAP, &f) ? "OK" : "BAD");
                    esp_capture_sink_release_frame(sys->sink[SINK_SNAP], &f);
                    got = true;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (!got) {
            ESP_LOGW(TAG, "  first JPEG missing");
        }
    }
    run_for(sys, 10000, true, false);
    ESP_LOGI(TAG, "Second one-shot");
    oneshot_jpeg(sys);
    run_for(sys, 10000, true, false);
    esp_capture_stop(sys->capture);
}

static void scenario_oneshot_low_mem(capture_sys_t *sys)
{
    ESP_LOGI(TAG, "======== 4: stream + JPEG one-shot (low mem) ========");
    /* Step: only streaming on, then start (snapshot enabled later when needed). */
    esp_capture_sink_enable(sys->sink[SINK_STREAM], ESP_CAPTURE_RUN_MODE_ALWAYS);
    esp_capture_sink_enable(sys->sink[SINK_SNAP], ESP_CAPTURE_RUN_MODE_DISABLE);
    esp_capture_sink_enable(sys->sink[SINK_DISP], ESP_CAPTURE_RUN_MODE_DISABLE);
    if (esp_capture_start(sys->capture) != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(TAG, "start failed");
        return;
    }

    run_for(sys, 10000, true, false);

    ESP_LOGI(TAG, "Snapshot once");
    oneshot_jpeg(sys);
    esp_capture_sink_enable(sys->sink[SINK_SNAP], ESP_CAPTURE_RUN_MODE_DISABLE);
    run_for(sys, 10000, true, false);

    ESP_LOGI(TAG, "Snapshot once more");
    oneshot_jpeg(sys);
    esp_capture_sink_enable(sys->sink[SINK_SNAP], ESP_CAPTURE_RUN_MODE_DISABLE);
    run_for(sys, 10000, true, false);

    esp_capture_stop(sys->capture);
}

static void capture_scheduler(const char *name, esp_capture_thread_schedule_cfg_t *cfg)
{
    if (strncmp(name, "venc_", 5) == 0) {
        cfg->stack_size = 40 * 1024;
        cfg->priority = 1;
        cfg->core_id = (name[5] == '0') ? 0 : 1;
    } else if (strcmp(name, "aenc_0") == 0) {
        cfg->stack_size = 20 * 1024;
        cfg->priority = 2;
        cfg->core_id = 1;
    }
}

void app_main(void)
{
    esp_log_level_set(TAG, ESP_LOG_INFO);
#if CONFIG_IDF_TARGET_ESP32P4
    esp_board_periph_init(ESP_BOARD_PERIPH_NAME_LDO_MIPI);
#endif
    ESP_ERROR_CHECK(esp_board_device_init(ESP_BOARD_DEVICE_NAME_CAMERA));
    esp_board_device_init(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);

    esp_video_enc_register_default();
    esp_audio_enc_register_default();
    esp_capture_set_thread_scheduler(capture_scheduler);

    for (uint8_t i = 0; i < SINK_NUM; i++) {
        ESP_LOGI(TAG, "sink%u: fmt=%08lX %ux%u@%u audio=%08lX",
                 i, (unsigned long)s_sink_cfg[i].video_info.format_id,
                 s_sink_cfg[i].video_info.width, s_sink_cfg[i].video_info.height,
                 s_sink_cfg[i].video_info.fps,
                 (unsigned long)s_sink_cfg[i].audio_info.format_id);
    }

    /* Build once; each scenario does its own start/stop. */
    capture_sys_t sys = {0};
    if (build_capture(&sys) != 0) {
        ESP_LOGE(TAG, "build_capture failed");
        destroy_capture(&sys);
        return;
    }

    scenario_streaming_only(&sys);
    scenario_stream_and_display(&sys);
    scenario_oneshot_fast(&sys);
    scenario_oneshot_low_mem(&sys);

    destroy_capture(&sys);
    ESP_LOGI(TAG, "All scenarios finished");
}

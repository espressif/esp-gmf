/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "unity.h"
#include <string.h>
#include <string.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_alc.h"

#include "esp_audio_simple_player.h"
#include "esp_audio_simple_player_advance.h"
#include "esp_gmf_setup_peripheral.h"
#include "esp_codec_dev.h"
#include "esp_gmf_app_sys.h"

static const char *TAG = "PLAYER_TEST";

#define PIPELINE_BLOCK_BIT BIT(0)

static const char *dec_file_path[] = {
    "file://sdcard/test.opus",
    "file://sdcard/test.m4a",
    "file://sdcard/test.aac",
    "file://sdcard/test.mp3",
    "file://sdcard/test.amr",
    "https://dl.espressif.com/dl/audio/gs-16b-2c-44100hz.mp3",
    "file://sdcard/test.flac",
    "file://sdcard/test.wav",
    "https://dl.espressif.com/dl/audio/gs-16b-2c-44100hz.ts",
    "file://sdcard/test.ts",
};

static int out_data_callback(uint8_t *data, int data_size, void *ctx)
{
    esp_codec_dev_handle_t dev = (esp_codec_dev_handle_t)ctx;
    esp_codec_dev_write(dev, data, data_size);
    return 0;
}

static int in_data_callback(uint8_t *data, int data_size, void *ctx)
{
    int ret = fread(data, 1, data_size, ctx);
    ESP_LOGD(TAG, "%s-%d,rd size:%d", __func__, __LINE__, ret);
    return ret;
}

static int mock_event_callback(esp_asp_event_pkt_t *event, void *ctx)
{
    if (event->type == ESP_ASP_EVENT_TYPE_MUSIC_INFO) {
        esp_asp_music_info_t info = {0};
        memcpy(&info, event->payload, event->payload_size);
        ESP_LOGW(TAG, "Get info, rate:%d, channels:%d, bits:%d", info.sample_rate, info.channels, info.bits);
    } else if (event->type == ESP_ASP_EVENT_TYPE_STATE) {
        esp_asp_state_t st = 0;
        memcpy(&st, event->payload, event->payload_size);
        ESP_LOGW(TAG, "Get State, %d,%s", st, esp_audio_simple_player_state_to_str(st));
        if (ctx && ((st == ESP_ASP_STATE_STOPPED) || (st == ESP_ASP_STATE_FINISHED) || (st == ESP_ASP_STATE_ERROR))) {
            xSemaphoreGive((SemaphoreHandle_t)ctx);
        }
    }
    return 0;
}

void task_audio_run_to_end(void *param)
{
    const char *uri = "file://sdcard/test.mp3";
    esp_asp_handle_t player = (esp_asp_handle_t)param;
    esp_gmf_err_t err = esp_audio_simple_player_run_to_end(player, uri, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    vTaskDelete(NULL);
}

void task_audio_stop(void *param)
{
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_asp_handle_t player = (esp_asp_handle_t)param;
    esp_gmf_err_t err = esp_audio_simple_player_stop(player);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    vTaskDelete(NULL);
}

TEST_CASE("Play, new and delete", "Simple_Player")
{
    esp_asp_cfg_t cfg = {
        .in.cb = out_data_callback,
        .in.user_ctx = NULL,
        .out.cb = out_data_callback,
        .out.user_ctx = NULL,
        .task_prio = 5,
    };
    esp_asp_handle_t handle = NULL;
    esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NOT_NULL(handle);
    err = esp_audio_simple_player_set_event(handle, mock_event_callback, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    err = esp_audio_simple_player_destroy(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    cfg.out.cb = NULL;
    handle = NULL;
    err = esp_audio_simple_player_new(&cfg, &handle);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NULL(handle);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Play, async run and stop", "Simple_Player")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
    void *card = NULL;
    esp_gmf_setup_periph_sdmmc(&card);
    esp_gmf_setup_periph_i2c(0);
    esp_gmf_setup_periph_aud_info play_info = {
        .sample_rate = 48000,
        .channel = 2,
        .bits_per_sample = 16,
        .port_num = 0,
    };
    esp_codec_dev_handle_t play_dev = NULL;
    esp_gmf_setup_periph_codec(&play_info, NULL, &play_dev, NULL);
    TEST_ASSERT_NOT_NULL(play_dev);

    esp_asp_cfg_t cfg = {
        .in.cb = NULL,
        .in.user_ctx = NULL,
        .out.cb = out_data_callback,
        .out.user_ctx = play_dev,
        .task_prio = 5,
    };
    esp_asp_handle_t handle = NULL;
    ESP_GMF_MEM_SHOW(TAG);
    for (size_t i = 0; i < 3; i++) {
        esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &handle);
        TEST_ASSERT_EQUAL(ESP_OK, err);
        err = esp_audio_simple_player_set_event(handle, mock_event_callback, NULL);

        const char *uri = "file://sdcard/test.mp3";
        err = esp_audio_simple_player_run(handle, uri, NULL);
        TEST_ASSERT_EQUAL(ESP_OK, err);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_asp_state_t state;
        err = esp_audio_simple_player_get_state(handle, &state);
        TEST_ASSERT_EQUAL(ESP_OK, err);
        TEST_ASSERT_EQUAL(ESP_ASP_STATE_RUNNING, state);
        vTaskDelay(6000 / portTICK_PERIOD_MS);

        err = esp_audio_simple_player_stop(handle);
        TEST_ASSERT_EQUAL(ESP_OK, err);

        err = esp_audio_simple_player_destroy(handle);
        TEST_ASSERT_EQUAL(ESP_OK, err);
    }
    ESP_GMF_MEM_SHOW(TAG);

    esp_gmf_teardown_periph_codec(play_dev, NULL);
    esp_gmf_teardown_periph_sdmmc(card);
    esp_gmf_teardown_periph_i2c(0);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Play, Sync run and stop", "Simple_Player")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
    void *card = NULL;
    esp_gmf_setup_periph_sdmmc(&card);
    esp_gmf_setup_periph_i2c(0);
    esp_gmf_setup_periph_aud_info play_info = {
        .sample_rate = 48000,
        .channel = 2,
        .bits_per_sample = 16,
        .port_num = 0,
    };
    esp_codec_dev_handle_t play_dev = NULL;
    esp_gmf_setup_periph_codec(&play_info, NULL, &play_dev, NULL);
    TEST_ASSERT_NOT_NULL(play_dev);

    esp_asp_cfg_t cfg = {
        .in.cb = NULL,
        .in.user_ctx = NULL,
        .out.cb = out_data_callback,
        .out.user_ctx = play_dev,
        .task_prio = 5,
    };
    esp_asp_handle_t handle = NULL;
    esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    err = esp_audio_simple_player_set_event(handle, mock_event_callback, NULL);

    const char *uri = "file://sdcard/test.mp3";
    err = esp_audio_simple_player_run_to_end(handle, uri, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    esp_asp_state_t state;
    err = esp_audio_simple_player_get_state(handle, &state);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(ESP_ASP_STATE_FINISHED, state);

    err = esp_audio_simple_player_stop(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = esp_audio_simple_player_destroy(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    esp_gmf_teardown_periph_codec(play_dev, NULL);
    esp_gmf_teardown_periph_sdmmc(card);
    esp_gmf_teardown_periph_i2c(0);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Play, Advance API run and stop", "Simple_Player")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    // esp_log_level_set("ESP_GMF_ASMP_DEC", ESP_LOG_DEBUG);
    // esp_log_level_set("ESP_GMF_PORT", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);
    void *card = NULL;
    esp_gmf_setup_periph_sdmmc(&card);
    esp_gmf_setup_periph_i2c(0);
    esp_gmf_setup_periph_aud_info play_info = {
        .sample_rate = 48000,
        .channel = 2,
        .bits_per_sample = 16,
        .port_num = 0,
    };
    esp_codec_dev_handle_t play_dev = NULL;
    esp_gmf_setup_periph_codec(&play_info, NULL, &play_dev, NULL);
    TEST_ASSERT_NOT_NULL(play_dev);

    esp_asp_cfg_t cfg = {
        .in.cb = NULL,
        .in.user_ctx = NULL,
        .out.cb = out_data_callback,
        .out.user_ctx = play_dev,
        .task_prio = 5,
    };
    esp_asp_handle_t handle = NULL;
    esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    err = esp_audio_simple_player_set_event(handle, mock_event_callback, NULL);

    esp_ae_alc_cfg_t alc_cfg = DEFAULT_ESP_GMF_ALC_CONFIG();
    alc_cfg.channel = 2;
    esp_gmf_element_handle_t alc_hd = NULL;
    esp_gmf_alc_init(&alc_cfg, &alc_hd);
    esp_gmf_alc_cast(&alc_cfg, alc_hd);
    esp_audio_simple_player_register_el(handle, alc_hd);


    const char *name[] = {"aud_simp_dec", "rate_cvt", "ch_cvt", "bit_cvt", "alc"};
    esp_audio_simple_player_set_pipeline(handle, NULL, name, 5);

    const char *uri = "file://sdcard/test.mp3";
    err = esp_audio_simple_player_run(handle, uri, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_asp_state_t state;
    err = esp_audio_simple_player_get_state(handle, &state);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(ESP_ASP_STATE_RUNNING, state);
    vTaskDelay(6000 / portTICK_PERIOD_MS);

    err = esp_audio_simple_player_stop(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);


    const char *uri2 = "file://sdcard/test.aac";
    err = esp_audio_simple_player_run(handle, uri2, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    err = esp_audio_simple_player_get_state(handle, &state);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(ESP_ASP_STATE_RUNNING, state);
    vTaskDelay(6000 / portTICK_PERIOD_MS);

    err = esp_audio_simple_player_stop(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);


    err = esp_audio_simple_player_destroy(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);


    esp_gmf_teardown_periph_codec(play_dev, NULL);
    esp_gmf_teardown_periph_sdmmc(card);
    esp_gmf_teardown_periph_i2c(0);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Play, pause,resume", "Simple_Player")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
    void *card = NULL;
    esp_gmf_setup_periph_sdmmc(&card);
    esp_gmf_setup_periph_i2c(0);
    esp_gmf_setup_periph_aud_info play_info = {
        .sample_rate = 48000,
        .channel = 2,
        .bits_per_sample = 16,
        .port_num = 0,
    };
    esp_codec_dev_handle_t play_dev = NULL;
    esp_gmf_setup_periph_codec(&play_info, NULL, &play_dev, NULL);
    TEST_ASSERT_NOT_NULL(play_dev);

    esp_asp_cfg_t cfg = {
        .in.cb = NULL,
        .in.user_ctx = NULL,
        .out.cb = out_data_callback,
        .out.user_ctx = play_dev,
        .task_prio = 5,
    };
    esp_asp_handle_t handle = NULL;
    esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    err = esp_audio_simple_player_set_event(handle, mock_event_callback, NULL);

    const char *uri = "file://sdcard/test.mp3";
    err = esp_audio_simple_player_run(handle, uri, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    esp_asp_state_t state;
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    err = esp_audio_simple_player_get_state(handle, &state);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(ESP_ASP_STATE_RUNNING, state);

    vTaskDelay(1000 / portTICK_PERIOD_MS);
    err = esp_audio_simple_player_pause(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    err = esp_audio_simple_player_get_state(handle, &state);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(ESP_ASP_STATE_PAUSED, state);

    vTaskDelay(1000 / portTICK_PERIOD_MS);
    err = esp_audio_simple_player_resume(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    err = esp_audio_simple_player_get_state(handle, &state);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(ESP_ASP_STATE_RUNNING, state);

    vTaskDelay(3000 / portTICK_PERIOD_MS);
    err = esp_audio_simple_player_stop(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = esp_audio_simple_player_destroy(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    esp_gmf_teardown_periph_codec(play_dev, NULL);
    esp_gmf_teardown_periph_sdmmc(card);
    esp_gmf_teardown_periph_i2c(0);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Play, play-multitask", "Simple_Player")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
    void *card = NULL;
    esp_gmf_setup_periph_sdmmc(&card);
    esp_gmf_setup_periph_i2c(0);
    esp_gmf_setup_periph_aud_info play_info = {
        .sample_rate = 48000,
        .channel = 2,
        .bits_per_sample = 16,
        .port_num = 0,
    };
    esp_codec_dev_handle_t play_dev = NULL;
    esp_gmf_setup_periph_codec(&play_info, NULL, &play_dev, NULL);
    TEST_ASSERT_NOT_NULL(play_dev);

    esp_asp_cfg_t cfg = {
        .in.cb = NULL,
        .in.user_ctx = NULL,
        .out.cb = out_data_callback,
        .out.user_ctx = play_dev,
        .task_prio = 5,
    };
    esp_asp_handle_t handle = NULL;
    esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    err = esp_audio_simple_player_set_event(handle, mock_event_callback, NULL);

    xTaskCreate(task_audio_run_to_end, "task_run_to_end", 1024 * 4, handle, 5, NULL);
    xTaskCreate(task_audio_stop, "task_stop", 2048, handle, 5, NULL);

    vTaskDelay(pdMS_TO_TICKS(10000));

    esp_asp_state_t state;
    err = esp_audio_simple_player_get_state(handle, &state);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT(state == ESP_ASP_STATE_STOPPED || state == ESP_ASP_STATE_FINISHED);

    err = esp_audio_simple_player_destroy(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    esp_gmf_teardown_periph_codec(play_dev, NULL);
    esp_gmf_teardown_periph_sdmmc(card);
    esp_gmf_teardown_periph_i2c(0);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Play, Raw IN MP3", "Simple_Player")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
    void *card = NULL;
    esp_gmf_setup_periph_sdmmc(&card);
    esp_gmf_setup_periph_i2c(0);
    esp_gmf_setup_periph_aud_info play_info = {
        .sample_rate = 48000,
        .channel = 2,
        .bits_per_sample = 16,
        .port_num = 0,
    };
    esp_codec_dev_handle_t play_dev = NULL;
    esp_gmf_setup_periph_codec(&play_info, NULL, &play_dev, NULL);
    TEST_ASSERT_NOT_NULL(play_dev);

    FILE *in_file = fopen("/sdcard/test.mp3", "rb");
    if (in_file == NULL) {
        ESP_LOGE(TAG, "Open the source file failed, in:%p", in_file);
        return;
    }
    esp_asp_cfg_t cfg = {
        .in.cb = in_data_callback,
        .in.user_ctx = in_file,
        .out.cb = out_data_callback,
        .out.user_ctx = play_dev,
        .task_prio = 5,
    };
    esp_asp_handle_t handle = NULL;
    esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    err = esp_audio_simple_player_set_event(handle, mock_event_callback, NULL);

    const char *uri = "raw://sdcard/test.mp3";
    err = esp_audio_simple_player_run(handle, uri, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    esp_asp_state_t state;
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    err = esp_audio_simple_player_get_state(handle, &state);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(ESP_ASP_STATE_RUNNING, state);
    vTaskDelay(10000 / portTICK_PERIOD_MS);

    err = esp_audio_simple_player_stop(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = esp_audio_simple_player_destroy(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    esp_gmf_teardown_periph_codec(play_dev, NULL);
    esp_gmf_teardown_periph_sdmmc(card);
    esp_gmf_teardown_periph_i2c(0);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Play, Multiple-file Sync Playing", "Simple_Player")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);

    void *card = NULL;
    esp_gmf_setup_periph_sdmmc(&card);
    esp_gmf_setup_periph_i2c(0);
    esp_gmf_setup_periph_wifi();
    esp_gmf_app_sys_monitor_start();

    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_setup_periph_aud_info play_info = {
        .sample_rate = 48000,
        .channel = 2,
        .bits_per_sample = 16,
        .port_num = 0,
    };
    esp_codec_dev_handle_t play_dev = NULL;
    esp_gmf_setup_periph_codec(&play_info, NULL, &play_dev, NULL);
    TEST_ASSERT_NOT_NULL(play_dev);

    esp_asp_cfg_t cfg = {
        .in.cb = NULL,
        .in.user_ctx = NULL,
        .out.cb = out_data_callback,
        .out.user_ctx = play_dev,
        .task_prio = 5,
    };
    esp_asp_handle_t handle = NULL;
    esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    err = esp_audio_simple_player_set_event(handle, mock_event_callback, NULL);
    ESP_GMF_MEM_SHOW(TAG);
    int repeat = 1;
    for (int i = 0; i < repeat; ++i) {
        for (int i = 0; i < sizeof(dec_file_path) / sizeof(char *); ++i) {
            err = esp_audio_simple_player_run_to_end(handle, dec_file_path[i], NULL);
            // TEST_ASSERT_EQUAL(ESP_OK, err);
            ESP_GMF_MEM_SHOW(TAG);
        }
    }

    err = esp_audio_simple_player_stop(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = esp_audio_simple_player_destroy(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    esp_gmf_teardown_periph_codec(play_dev, NULL);
    ESP_GMF_MEM_SHOW(TAG);

    esp_gmf_teardown_periph_sdmmc(card);
    esp_gmf_teardown_periph_i2c(0);
    esp_gmf_teardown_periph_wifi();
    esp_gmf_app_sys_monitor_stop();

    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Play, Multiple-file Async Playing", "Simple_Player")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
    void *card = NULL;
    esp_gmf_setup_periph_sdmmc(&card);
    esp_gmf_setup_periph_i2c(0);
    esp_gmf_setup_periph_wifi();

    ESP_GMF_MEM_SHOW(TAG);

    esp_gmf_setup_periph_aud_info play_info = {
        .sample_rate = 48000,
        .channel = 2,
        .bits_per_sample = 16,
        .port_num = 0,
    };
    esp_codec_dev_handle_t play_dev = NULL;
    esp_gmf_setup_periph_codec(&play_info, NULL, &play_dev, NULL);
    TEST_ASSERT_NOT_NULL(play_dev);

    SemaphoreHandle_t semph_event = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(semph_event);
    esp_gmf_app_sys_monitor_start();

    esp_asp_cfg_t cfg = {
        .in.cb = NULL,
        .in.user_ctx = NULL,
        .out.cb = out_data_callback,
        .out.user_ctx = play_dev,
        .task_prio = 5,
    };
    esp_asp_handle_t handle = NULL;
    esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    err = esp_audio_simple_player_set_event(handle, mock_event_callback, semph_event);

    ESP_GMF_MEM_SHOW(TAG);
    for (int i = 0; i < sizeof(dec_file_path) / sizeof(char *); ++i) {
        err = esp_audio_simple_player_run(handle, dec_file_path[i], NULL);
        TEST_ASSERT_EQUAL(ESP_OK, err);
        ESP_GMF_MEM_SHOW(TAG);
        xSemaphoreTake(semph_event, portMAX_DELAY);
    }

    err = esp_audio_simple_player_stop(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    esp_gmf_app_sys_monitor_stop();
    vSemaphoreDelete(semph_event);

    err = esp_audio_simple_player_destroy(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    esp_gmf_teardown_periph_codec(play_dev, NULL);

    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_teardown_periph_sdmmc(card);
    esp_gmf_teardown_periph_i2c(0);
    esp_gmf_teardown_periph_wifi();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

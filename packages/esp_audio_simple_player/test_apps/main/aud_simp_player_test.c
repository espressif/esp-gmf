/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
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
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_app_unit_test.h"
#include "esp_codec_dev.h"
#include "esp_gmf_app_sys.h"
#include "esp_embed_tone.h"
#include "esp_gmf_io.h"
#include "esp_gmf_io_embed_flash.h"

#define PIPELINE_BLOCK_BIT BIT(0)

#define STATE_RUNNING_BIT              (1 << 0)
#define STATE_STOPPED_BIT              (1 << 1)
#define STATE_PAUSED_BIT               (1 << 2)
#define STATE_FINISHED_BIT             (1 << 3)
#define CUSTOM_HIGH_PRIO_TASK_STOP_BIT (1 << 4)
#define CUSTOM_LOW_PRIO_TASK_STOP_BIT  (1 << 5)

typedef struct {
    esp_asp_handle_t  *player_handle;
    volatile bool     *test_flag;
    EventGroupHandle_t state_event_group;
} test_task_params_t;

typedef struct {
    void *sdcard_handle;
    bool  wifi_connected;
    bool  sys_monitor_started;
} test_env_t;

static const char *TAG = "PLAYER_TEST";

static const char *dec_file_path[] = {
    "file://sdcard/test.mp3",
    "file://sdcard/test.opus",
    "file://sdcard/test.m4a",
    "file://sdcard/test.aac",
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

static void setup_test_environment(test_env_t *env, bool need_wifi, bool need_sys_monitor)
{
    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_setup_codec_dev(NULL);
    esp_gmf_app_setup_sdcard(&env->sdcard_handle);

    if (need_wifi) {
        esp_gmf_app_wifi_connect();
        env->wifi_connected = true;
    }

    if (need_sys_monitor) {
        esp_gmf_app_sys_monitor_start();
        env->sys_monitor_started = true;
    }
    ESP_GMF_MEM_SHOW(TAG);
}

static void teardown_test_environment(test_env_t *env)
{
    esp_gmf_app_teardown_sdcard(env->sdcard_handle);
    esp_gmf_app_teardown_codec_dev();

    ESP_GMF_MEM_SHOW(TAG);

    if (env->wifi_connected) {
        esp_gmf_app_wifi_disconnect();
    }

    if (env->sys_monitor_started) {
        esp_gmf_app_sys_monitor_stop();
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_GMF_MEM_SHOW(TAG);
}

static esp_asp_handle_t create_simple_player(const esp_asp_data_func in_cb, void *in_ctx,
                                      const esp_asp_event_func event_cb, void *event_ctx)
{
    esp_asp_cfg_t cfg = {
        .in.cb = in_cb,
        .in.user_ctx = in_ctx,
        .out.cb = out_data_callback,
        .out.user_ctx = esp_gmf_app_get_playback_handle(),
        .task_prio = 5,
    };

    esp_asp_handle_t handle = NULL;
    esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NOT_NULL(handle);
    err = esp_audio_simple_player_set_event(handle, event_cb, event_ctx);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    return handle;
}

static void destroy_simple_player(esp_asp_handle_t handle)
{
    esp_gmf_err_t err = esp_audio_simple_player_stop(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    err = esp_audio_simple_player_destroy(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
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

TEST_CASE("Play, new and delete", "[Simple_Player]")
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

TEST_CASE("Create and delete multiple instances for playback, stop", "[Simple_Player]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    test_env_t env = {0};
    setup_test_environment(&env, false, false);

    esp_asp_handle_t handle = NULL;
    ESP_LOGW(TAG, "--- Async playback ---\r\n");
    for (int i = 0; i < 3; ++i) {
        handle = create_simple_player(NULL, NULL, mock_event_callback, NULL);

        esp_gmf_err_t err = esp_audio_simple_player_run(handle, dec_file_path[0], NULL);
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

    ESP_LOGW(TAG, "--- Sync playback ---\r\n");
    for (int i = 0; i < 3; ++i) {
        handle = create_simple_player(NULL, NULL, mock_event_callback, NULL);

        esp_gmf_err_t err = esp_audio_simple_player_run_to_end(handle, dec_file_path[0], NULL);
        TEST_ASSERT_EQUAL(ESP_OK, err);
        err = esp_audio_simple_player_stop(handle);
        TEST_ASSERT_EQUAL(ESP_OK, err);
        err = esp_audio_simple_player_destroy(handle);
        TEST_ASSERT_EQUAL(ESP_OK, err);
    }
    teardown_test_environment(&env);
}

TEST_CASE("Repeat playback same URI", "[Simple_Player]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    test_env_t env = {0};
    setup_test_environment(&env, false, false);
    esp_asp_handle_t handle = create_simple_player(NULL, NULL, mock_event_callback, NULL);

    ESP_LOGW(TAG, "--- Async repeat playback music ---\r\n");
    for (int i = 0; i < 3; ++i) {
        esp_gmf_err_t err = esp_audio_simple_player_run(handle, dec_file_path[0], NULL);
        TEST_ASSERT_EQUAL(ESP_OK, err);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_asp_state_t state;
        err = esp_audio_simple_player_get_state(handle, &state);
        TEST_ASSERT_EQUAL(ESP_OK, err);
        TEST_ASSERT_EQUAL(ESP_ASP_STATE_RUNNING, state);
        vTaskDelay(6000 / portTICK_PERIOD_MS);

        err = esp_audio_simple_player_stop(handle);
        TEST_ASSERT_EQUAL(ESP_OK, err);
    }
    ESP_LOGW(TAG, "--- Sync repeat playback music ---\r\n");
    for (int i = 0; i < 3; ++i) {
        esp_gmf_err_t err = esp_audio_simple_player_run_to_end(handle, dec_file_path[0], NULL);
        TEST_ASSERT_EQUAL(ESP_OK, err);
    }

    destroy_simple_player(handle);
    teardown_test_environment(&env);
}

TEST_CASE("Playback with raw MP3 data", "[Simple_Player]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    test_env_t env = {0};
    setup_test_environment(&env, false, false);

    FILE *in_file = fopen("/sdcard/test.mp3", "rb");
    if (in_file == NULL) {
        ESP_LOGE(TAG, "Open the source file failed, in:%p", in_file);
        return;
    }
    esp_asp_handle_t handle = create_simple_player(in_data_callback, in_file, mock_event_callback, NULL);

    const char *uri = "raw://sdcard/test.mp3";
    esp_gmf_err_t err = esp_audio_simple_player_run(handle, uri, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    esp_asp_state_t state;
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    err = esp_audio_simple_player_get_state(handle, &state);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(ESP_ASP_STATE_RUNNING, state);
    vTaskDelay(10000 / portTICK_PERIOD_MS);

    err = esp_audio_simple_player_stop(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    ESP_LOGW(TAG, "--- Playback with sync mode ---\r\n");
    // Reset the file pointer to the beginning of the file
    if (in_file) {
        fseek(in_file, 0, SEEK_SET);
    }
    err = esp_audio_simple_player_run_to_end(handle, uri, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    destroy_simple_player(handle);
    teardown_test_environment(&env);
}

static int embed_flash_io_set(esp_asp_handle_t *handle, void *ctx)
{
    esp_gmf_pipeline_handle_t pipe = NULL;
    int ret =  esp_audio_simple_player_get_pipeline(handle, &pipe);
    if (pipe) {
        esp_gmf_io_handle_t flash = NULL;
        ret = esp_gmf_pipeline_get_in(pipe, &flash);
        if ((ret == ESP_GMF_ERR_OK) && (strcasecmp(OBJ_GET_TAG(flash), "io_embed_flash") == 0)) {
            ret = esp_gmf_io_embed_flash_set_context(flash, (embed_item_info_t *)&g_esp_embed_tone[0], ESP_EMBED_TONE_URL_MAX);
        }
    }
    return ret;
}

TEST_CASE("Playback embed flash tone", "[Simple_Player]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    test_env_t env = {0};
    setup_test_environment(&env, false, false);

    esp_asp_cfg_t cfg = {
        .in.cb = NULL,
        .in.user_ctx = NULL,
        .out.cb = out_data_callback,
        .out.user_ctx = esp_gmf_app_get_playback_handle(),
        .task_prio = 5,
        .prev = embed_flash_io_set,
        .prev_ctx = NULL,
    };
    esp_asp_handle_t handle = NULL;
    esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    err = esp_audio_simple_player_set_event(handle, mock_event_callback, NULL);

    err = esp_audio_simple_player_run(handle, esp_embed_tone_url[0], NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    esp_asp_state_t state;
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    err = esp_audio_simple_player_get_state(handle, &state);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(ESP_ASP_STATE_RUNNING, state);
    vTaskDelay(4000 / portTICK_PERIOD_MS);

    err = esp_audio_simple_player_stop(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    ESP_LOGW(TAG, "--- Playback with sync mode ---\r\n");

    err = esp_audio_simple_player_run_to_end(handle, esp_embed_tone_url[1], NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = esp_audio_simple_player_run_to_end(handle, dec_file_path[0], NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    destroy_simple_player(handle);
    teardown_test_environment(&env);
}

TEST_CASE("Play, Advance API run and stop", "[Simple_Player]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    // esp_log_level_set("ESP_GMF_ASMP_DEC", ESP_LOG_DEBUG);
    // esp_log_level_set("ESP_GMF_PORT", ESP_LOG_DEBUG);
    test_env_t env = {0};
    setup_test_environment(&env, false, false);
    esp_asp_handle_t handle = create_simple_player(NULL, NULL, mock_event_callback, NULL);

    esp_ae_alc_cfg_t alc_cfg = DEFAULT_ESP_GMF_ALC_CONFIG();
    alc_cfg.channel = 2;
    esp_gmf_element_handle_t alc_hd = NULL;
    esp_gmf_alc_init(&alc_cfg, &alc_hd);
    esp_audio_simple_player_register_el(handle, alc_hd);

    const char *name[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt", "aud_alc"};
    esp_audio_simple_player_set_pipeline(handle, NULL, name, 5);

    const char *uri = "file://sdcard/test.mp3";
    esp_gmf_err_t err = esp_audio_simple_player_run(handle, uri, NULL);
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

    destroy_simple_player(handle);
    teardown_test_environment(&env);
}

TEST_CASE("Play, pause,resume", "[Simple_Player]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    test_env_t env = {0};
    setup_test_environment(&env, false, false);
    esp_asp_handle_t handle = create_simple_player(NULL, NULL, mock_event_callback, NULL);

    const char *uri = "file://sdcard/test.mp3";
    esp_gmf_err_t err = esp_audio_simple_player_run(handle, uri, NULL);
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

    destroy_simple_player(handle);
    teardown_test_environment(&env);
}

TEST_CASE("Play, play-multitask", "[Simple_Player]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    test_env_t env = {0};
    setup_test_environment(&env, false, false);
    esp_asp_handle_t handle = create_simple_player(NULL, NULL, mock_event_callback, NULL);

    xTaskCreate(task_audio_run_to_end, "task_run_to_end", 1024 * 4, handle, 5, NULL);
    xTaskCreate(task_audio_stop, "task_stop", 2048, handle, 5, NULL);

    vTaskDelay(pdMS_TO_TICKS(10000));

    esp_asp_state_t state;
    esp_gmf_err_t err = esp_audio_simple_player_get_state(handle, &state);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT(state == ESP_ASP_STATE_STOPPED || state == ESP_ASP_STATE_FINISHED);

    destroy_simple_player(handle);
    teardown_test_environment(&env);
}

TEST_CASE("Play, Multiple-file Sync Playing", "[Simple_Player][leaks=14000]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_gmf_app_test_case_uses_tcpip();
    test_env_t env = {0};
    setup_test_environment(&env, true, true);
    esp_asp_handle_t handle = create_simple_player(NULL, NULL, mock_event_callback, NULL);
    ESP_GMF_MEM_SHOW(TAG);
    int repeat = 1;
    for (int i = 0; i < repeat; ++i) {
        for (int i = 0; i < sizeof(dec_file_path) / sizeof(char *); ++i) {
            esp_audio_simple_player_run_to_end(handle, dec_file_path[i], NULL);
            // TEST_ASSERT_EQUAL(ESP_OK, err);
            ESP_GMF_MEM_SHOW(TAG);
        }
    }
    destroy_simple_player(handle);
    teardown_test_environment(&env);
}

TEST_CASE("Play, Multiple-file Async Playing", "[Simple_Player][leaks=14000]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_gmf_app_test_case_uses_tcpip();
    test_env_t env = {0};
    setup_test_environment(&env, true, true);

    SemaphoreHandle_t semph_event = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(semph_event);
    esp_asp_handle_t handle = create_simple_player(NULL, NULL, mock_event_callback, semph_event);

    ESP_GMF_MEM_SHOW(TAG);
    for (int i = 0; i < sizeof(dec_file_path) / sizeof(char *); ++i) {
        esp_gmf_err_t err = esp_audio_simple_player_run(handle, dec_file_path[i], NULL);
        TEST_ASSERT_EQUAL(ESP_OK, err);
        ESP_GMF_MEM_SHOW(TAG);
        xSemaphoreTake(semph_event, portMAX_DELAY);
    }
    destroy_simple_player(handle);
    teardown_test_environment(&env);
}

static int test_event_callback(esp_asp_event_pkt_t *event, void *ctx)
{
    if (event->type == ESP_ASP_EVENT_TYPE_MUSIC_INFO) {
        esp_asp_music_info_t info = {0};
        memcpy(&info, event->payload, event->payload_size);
        ESP_LOGW(TAG, "Get info, rate:%d, channels:%d, bits:%d", info.sample_rate, info.channels, info.bits);
    } else if (event->type == ESP_ASP_EVENT_TYPE_STATE) {
        esp_asp_state_t st = 0;
        memcpy(&st, event->payload, event->payload_size);
        ESP_LOGW(TAG, "Get State, %d,%s", st, esp_audio_simple_player_state_to_str(st));

        uint8_t bits = 0;
        switch (st) {
            case ESP_ASP_STATE_RUNNING:
                bits = STATE_RUNNING_BIT;
                break;
            case ESP_ASP_STATE_STOPPED:
                bits = STATE_STOPPED_BIT;
                break;
            case ESP_ASP_STATE_PAUSED:
                bits = STATE_PAUSED_BIT;
                break;
            case ESP_ASP_STATE_FINISHED:
                bits = STATE_FINISHED_BIT;
                break;
            default:
                break;
        }
        if (bits != 0) {
            // Set event group bits
            xEventGroupSetBits((EventGroupHandle_t)ctx, bits);
        }
    }
    return 0;
}

// Low priority task：run simple player and randomly pause or stop it
void low_priority_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Low priority task running ...");
    srand(xTaskGetTickCount() ^ (uint32_t)pvParameters);

    test_task_params_t *task_params = (test_task_params_t *)pvParameters;
    esp_asp_handle_t *handle = task_params->player_handle;
    EventGroupHandle_t event_group = task_params->state_event_group;

    esp_asp_state_t state;
    esp_gmf_err_t err;

    err = esp_audio_simple_player_run(*handle, "file://sdcard/alarm_44100hz_16bit_2ch_100ms.mp3", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    while (*(task_params->test_flag)) {
        vTaskDelay(pdMS_TO_TICKS(90 + rand() % 40));

        err = esp_audio_simple_player_get_state(*handle, &state);
        TEST_ASSERT_EQUAL(ESP_OK, err);

        if (state == ESP_ASP_STATE_RUNNING) {
            uint8_t op = rand() % 2;
            switch (op) {
                case 0:  // Pause
                    ESP_LOGW(TAG, "Player is running, trying to PAUSE it");
                    err = esp_audio_simple_player_pause(*handle);
                    TEST_ASSERT_EQUAL(ESP_OK, err);
                    break;

                case 1:  // Stop
                    ESP_LOGW(TAG, "Player is running, trying to STOP it");
                    err = esp_audio_simple_player_stop(*handle);
                    TEST_ASSERT_EQUAL(ESP_OK, err);
                    break;
            }
        }
    }

    ESP_LOGI(TAG, "Low priority task is done");
    xEventGroupSetBits(event_group, CUSTOM_LOW_PRIO_TASK_STOP_BIT);
    vTaskDelete(NULL);
}

// High priority task：monitor player state and recover it if needed
void high_priority_task(void *pvParameters)
{
    ESP_LOGI(TAG, "High priority task running ...");

    test_task_params_t *task_params = (test_task_params_t *)pvParameters;
    esp_asp_handle_t *handle = task_params->player_handle;
    EventGroupHandle_t event_group = task_params->state_event_group;

    EventBits_t bits;
    TickType_t wait_timeout = 100;

    while (*(task_params->test_flag)) {
        bits = xEventGroupWaitBits(event_group,
                                   STATE_STOPPED_BIT | STATE_FINISHED_BIT | STATE_PAUSED_BIT,
                                   pdTRUE, pdFALSE, portMAX_DELAY);

        if ((bits & STATE_FINISHED_BIT) || (bits & STATE_STOPPED_BIT)) {
            wait_timeout = 200;
            xEventGroupClearBits(event_group, STATE_FINISHED_BIT | STATE_STOPPED_BIT | STATE_RUNNING_BIT);
            ESP_LOGW(TAG, "Player FINISHED or STOPPED, high priority begin to recover player");
            esp_gmf_err_t err = esp_audio_simple_player_run(*handle, "file://sdcard/alarm_44100hz_16bit_2ch_100ms.mp3", NULL);
            TEST_ASSERT_EQUAL(ESP_OK, err);
        } else if (bits & STATE_PAUSED_BIT) {
            wait_timeout = 100;
            xEventGroupClearBits(event_group, STATE_PAUSED_BIT | STATE_RUNNING_BIT);
            ESP_LOGW(TAG, "Player PAUSED, high priority begin to recover player");
            esp_gmf_err_t err = esp_audio_simple_player_resume(*handle);
            TEST_ASSERT_EQUAL(ESP_OK, err);
        }

        EventBits_t run_bits = xEventGroupWaitBits(event_group,
                                                   STATE_RUNNING_BIT,
                                                   pdTRUE, pdFALSE,
                                                   pdMS_TO_TICKS(wait_timeout));
        TEST_ASSERT_TRUE(run_bits & STATE_RUNNING_BIT);
    }

    ESP_LOGI(TAG, "High priority task is done");
    xEventGroupSetBits(event_group, CUSTOM_HIGH_PRIO_TASK_STOP_BIT);
    vTaskDelete(NULL);
}

TEST_CASE("Pause, Stop, and Run APIs for Multi-task Execution", "[Simple_Player]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    test_env_t env = {0};
    setup_test_environment(&env, false, false);

    EventGroupHandle_t event_group = xEventGroupCreate();
    xEventGroupClearBits(event_group, 0xFFFFFF);
    esp_asp_handle_t handle = create_simple_player(NULL, NULL, test_event_callback, event_group);

    test_task_params_t *params = malloc(sizeof(test_task_params_t));
    volatile bool test_flag = true;

    params->player_handle = &handle;
    params->test_flag = &test_flag;
    params->state_event_group = event_group;

    xTaskCreate(high_priority_task, "High Priority Task", 4096, (void *)params, 7, NULL);
    xTaskCreate(low_priority_task, "Low Priority Task", 4096, (void *)params, 6, NULL);

    vTaskDelay(pdMS_TO_TICKS(20000));
    test_flag = false;

    xEventGroupWaitBits(event_group,
                        CUSTOM_HIGH_PRIO_TASK_STOP_BIT | CUSTOM_LOW_PRIO_TASK_STOP_BIT,
                        pdTRUE, pdTRUE,
                        portMAX_DELAY);
    ESP_LOGI(TAG, "All tasks are deleted, test finished");

    destroy_simple_player(handle);
    teardown_test_environment(&env);
}

TEST_CASE("Play wrong uri", "[Simple_Player]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    test_env_t env = {0};
    setup_test_environment(&env, false, false);
    esp_asp_handle_t handle = create_simple_player(NULL, NULL, mock_event_callback, NULL);

    const char *uri = "file://sdcard/wrong_uri.mp3";
    esp_gmf_err_t err = esp_audio_simple_player_run(handle, uri, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    esp_asp_state_t state;
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    err = esp_audio_simple_player_get_state(handle, &state);
    ESP_LOGW(TAG, "Get State, %d,%s", state, esp_audio_simple_player_state_to_str(state));
    TEST_ASSERT_EQUAL(ESP_ASP_STATE_ERROR, state);

    destroy_simple_player(handle);
    teardown_test_environment(&env);
}

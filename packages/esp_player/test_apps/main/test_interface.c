/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "unity.h"
#include "esp_log.h"

#include "esp_player.h"
#include "esp_player_advance.h"
#include "esp_player_types.h"
#include "render_common.h"
#include "test_data.h"

#define CREATE_TEST_PLAYER(player_var)  do {                        \
    esp_player_config_t config = ESP_PLAYER_CONFIG_DEFAULT();       \
    config.audio_render_hd = (void *)1;                             \
    config.video_render_hd = (void *)1;                             \
    esp_player_err_t _ret = esp_player_init(&config, &player_var);  \
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, _ret);                     \
    TEST_ASSERT_NOT_NULL(player_var);                               \
} while (0)

#define DESTROY_TEST_PLAYER(player_var)  do {               \
    esp_player_err_t _ret = esp_player_deinit(player_var);  \
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, _ret);             \
} while (0)

static const char *TAG = "TEST_INTERFACE";

static esp_player_err_t test_event_callback(esp_player_event_msg_t *event_msg, void *ctx)
{
    ESP_LOGI(TAG, "Event received: %d", event_msg->event_type);
    return ESP_PLAYER_ERR_OK;
}

TEST_CASE("[interface]:test_player_init_with_default_config", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    esp_player_config_t config = ESP_PLAYER_CONFIG_DEFAULT();
    esp_player_err_t ret = esp_player_init(&config, &player);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);
    config.audio_render_hd = (void *)1;
    config.video_render_hd = (void *)1;
    ret = esp_player_init(&config, &player);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);
    TEST_ASSERT_NOT_NULL(player);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_init_with_null_config", "[player][interface]")
{
    esp_player_handle_t player = NULL;

    esp_player_err_t ret = esp_player_init(NULL, &player);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);
    TEST_ASSERT_NULL(player);
}

TEST_CASE("[interface]:test_player_init_with_null_handle", "[player][interface]")
{
    esp_player_config_t config = ESP_PLAYER_CONFIG_DEFAULT();

    esp_player_err_t ret = esp_player_init(&config, NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);
}

TEST_CASE("[interface]:test_player_deinit_with_null_handle", "[player][interface]")
{
    esp_player_err_t ret = esp_player_deinit(NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);
}

TEST_CASE("[interface]:test_player_init_deinit_multiple_times", "[player][interface]")
{
    for (int i = 0; i < 5; i++) {
        esp_player_handle_t player = NULL;
        CREATE_TEST_PLAYER(player);
        DESTROY_TEST_PLAYER(player);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

TEST_CASE("[interface]:test_player_set_av_mask", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    esp_player_err_t ret = esp_player_set_av_mask(player, ESP_PLAYER_MASK_AUDIO);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_av_mask(player, ESP_PLAYER_MASK_VIDEO);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_av_mask(player, ESP_PLAYER_MASK_AV);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_av_mask(NULL, ESP_PLAYER_MASK_AUDIO);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_set_url", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    esp_player_err_t ret = esp_player_set_url(player, TEST_FILE_PATH);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_url(player, NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_url(NULL, TEST_FILE_PATH);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_set_data_src", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    esp_player_data_src_t src = ESP_PLAYER_DATA_SRC(TEST_FILE_PATH, ESP_PLAYER_MASK_AUDIO);
    esp_player_err_t ret = esp_player_set_data_src(player, &src);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_data_src(NULL, &src);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    ret = esp_player_set_data_src(player, NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_set_sync_mode", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    esp_player_err_t ret = esp_player_set_sync_mode(NULL, ESP_PLAYER_SYNC_MODE_AUDIO);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    ret = esp_player_set_sync_mode(player, ESP_PLAYER_SYNC_MODE_MAX);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    static const esp_player_sync_mode_t valid_modes[] = {
        ESP_PLAYER_SYNC_MODE_SYSTEM,
        ESP_PLAYER_SYNC_MODE_AUDIO,
        ESP_PLAYER_SYNC_MODE_VIDEO,
        ESP_PLAYER_SYNC_MODE_NONE,
    };
    for (size_t i = 0; i < sizeof(valid_modes) / sizeof(valid_modes[0]); i++) {
        ret = esp_player_set_sync_mode(player, valid_modes[i]);
        TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);
    }

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_set_dec_cfg", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    esp_player_format_t test_format = ESP_FOURCC_AAC;
    uint8_t test_cfg[64] = {0};

    esp_player_err_t ret = esp_player_set_dec_cfg(player, test_format, test_cfg, sizeof(test_cfg));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_FAIL, ret);

    ret = esp_player_set_dec_cfg(NULL, test_format, test_cfg, sizeof(test_cfg));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_set_task_config", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    esp_player_task_config_t task_config = {0};

    esp_player_err_t ret = esp_player_set_task_config(player, &task_config);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_task_config(player, &task_config);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_task_config(player, NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_task_config(NULL, &task_config);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_set_buffer_config", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    esp_player_buffer_config_t buffer_config = {
        .extractor_pool_size = 4096,
        .http_read_buf_size = 8192,
        .prebuffer_resume_ms = 1000,
        .rebuffer_enter_ms = 200,
        .rebuffer_resume_ms = 800,
        .rebuffer_grace_ms = 500,
    };

    esp_player_err_t ret = esp_player_set_buffer_config(player, &buffer_config);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_buffer_config(player, &buffer_config);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_buffer_config(player, NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_buffer_config(NULL, &buffer_config);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_set_event_cb", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    esp_player_err_t ret = esp_player_set_event_cb(player, test_event_callback, NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_event_cb(player, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_event_cb(NULL, test_event_callback, NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_set_event_queue", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    QueueHandle_t queue = xQueueCreate(10, sizeof(esp_player_event_msg_t));
    TEST_ASSERT_NOT_NULL(queue);

    esp_player_err_t ret = esp_player_set_event_queue(player, queue);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_event_queue(player, NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_event_queue(NULL, queue);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    vQueueDelete(queue);
    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_run", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    esp_player_err_t ret = esp_player_run(player);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_run(NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_pause_resume", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    esp_player_err_t ret = esp_player_pause(player);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_STATE, ret);

    ret = esp_player_resume(player);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_STATE, ret);

    ret = esp_player_pause(NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    ret = esp_player_resume(NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_stop", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    esp_player_err_t ret = esp_player_stop(player);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_stop(NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_seek", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    esp_player_err_t ret = esp_player_seek(player, 5000);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_STATE, ret);

    ret = esp_player_seek(NULL, 5000);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_set_speed", "[player][interface]")
{
    esp_player_handle_t player = NULL;

    esp_player_config_t config = ESP_PLAYER_CONFIG_DEFAULT();
    audio_render_create_handle(&config.audio_render_hd, 44100, 16, 2, 0);
    esp_player_err_t _ret = esp_player_init(&config, &player);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, _ret);
    TEST_ASSERT_NOT_NULL(player);

    esp_player_err_t ret = esp_player_set_speed(player, 1.0f);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_FAIL, ret);

    ret = esp_player_set_speed(player, 0.5f);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_FAIL, ret);

    ret = esp_player_set_speed(player, 2.0f);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_FAIL, ret);

    ret = esp_player_set_speed(NULL, 1.0f);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    ret = esp_player_set_speed(player, 0.0f);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    ret = esp_player_set_speed(player, -1.0f);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    esp_audio_render_proc_type_t proc_type[1] = {ESP_AUDIO_RENDER_PROC_SONIC};
    esp_audio_render_stream_add_proc(config.audio_render_hd, proc_type, 1);

    ret = esp_player_set_speed(player, 1.0f);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_speed(player, 0.5f);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_speed(player, 2.0f);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);
    _ret = esp_player_deinit(player);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, _ret);
    audio_render_destroy_handle();
}

TEST_CASE("[interface]:test_player_get_state", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    esp_player_state_t state = ESP_PLAYER_STATE_ERROR;
    esp_player_err_t ret = esp_player_get_state(player, &state);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);
    TEST_ASSERT_EQUAL(ESP_PLAYER_STATE_IDLE, state);

    ret = esp_player_get_state(NULL, &state);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    ret = esp_player_get_state(player, NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_get_duration", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    uint64_t duration = 0;

    esp_player_err_t ret = esp_player_get_duration(player, &duration);

    ret = esp_player_get_duration(NULL, &duration);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    ret = esp_player_get_duration(player, NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_get_play_time", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    uint64_t current_time = 0;

    esp_player_err_t ret = esp_player_get_play_time(player, &current_time);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);
    TEST_ASSERT_EQUAL(current_time, 0);

    ret = esp_player_get_play_time(NULL, &current_time);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    ret = esp_player_get_play_time(player, NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_get_track_num", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    uint16_t track_num = 0;

    esp_player_err_t ret = esp_player_get_track_num(player, ESP_PLAYER_TRACK_TYPE_AUDIO, &track_num);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_FAIL, ret);

    ret = esp_player_get_track_num(player, ESP_PLAYER_TRACK_TYPE_VIDEO, &track_num);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_FAIL, ret);

    ret = esp_player_get_track_num(NULL, ESP_PLAYER_TRACK_TYPE_AUDIO, &track_num);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    ret = esp_player_get_track_num(player, ESP_PLAYER_TRACK_TYPE_AUDIO, NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_get_track_info", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    esp_player_track_info_t track_info = {0};

    esp_player_err_t ret = esp_player_get_track_info(player, ESP_PLAYER_TRACK_TYPE_AUDIO, 0, &track_info);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_NOT_SUPPORT, ret);

    ret = esp_player_get_track_info(NULL, ESP_PLAYER_TRACK_TYPE_AUDIO, 0, &track_info);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    ret = esp_player_get_track_info(player, ESP_PLAYER_TRACK_TYPE_AUDIO, 0, NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_enable_track", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    esp_player_err_t ret = esp_player_enable_track(player, ESP_PLAYER_TRACK_TYPE_AUDIO, 0, true);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_NOT_SUPPORT, ret);

    ret = esp_player_enable_track(player, ESP_PLAYER_TRACK_TYPE_AUDIO, 0, false);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_NOT_SUPPORT, ret);

    ret = esp_player_enable_track(NULL, ESP_PLAYER_TRACK_TYPE_AUDIO, 0, true);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_get_id3_info", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    const esp_extractor_id3_info_t *id3_info = NULL;

    esp_player_err_t ret = esp_player_get_id3_info(player, &id3_info);

    ret = esp_player_get_id3_info(NULL, &id3_info);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_submit_frame", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    esp_player_frame_t frame = {
        .data = NULL,
        .data_len = 0,
        .pts = 0,
        .frame_type = ESP_PLAYER_FRAME_TYPE_DEFAULT,
        .is_bad = false,
        .eos = false,
    };
    esp_player_err_t ret = esp_player_submit_frame(NULL, &frame, 0);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    ret = esp_player_submit_frame(player, NULL, 0);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    ret = esp_player_submit_frame(player, &frame, 0);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_ARG, ret);

    frame.data = (void *)1;
    ret = esp_player_submit_frame(player, &frame, 0);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_INVALID_STATE, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_api_sequence", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    esp_player_err_t ret = esp_player_set_av_mask(player, ESP_PLAYER_MASK_AUDIO);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_event_cb(player, test_event_callback, NULL);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    ret = esp_player_set_url(player, TEST_FILE_PATH);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    uint64_t duration = 0;
    esp_player_get_duration(player, &duration);

    uint64_t current_time = 0;
    esp_player_get_play_time(player, &current_time);

    ret = esp_player_stop(player);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    DESTROY_TEST_PLAYER(player);
}

TEST_CASE("[interface]:test_player_multiple_operations", "[player][interface]")
{
    esp_player_handle_t player = NULL;
    CREATE_TEST_PLAYER(player);

    for (int i = 0; i < 10; i++) {
        esp_player_err_t ret = esp_player_set_av_mask(player, ESP_PLAYER_MASK_AUDIO);
        TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

        ret = esp_player_set_url(player, TEST_FILE_PATH);
        TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

        ret = esp_player_stop(player);
        TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);
    }

    DESTROY_TEST_PLAYER(player);
}

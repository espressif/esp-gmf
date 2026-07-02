/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "unity.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_random.h"

#include "esp_audio_render.h"
#include "esp_audio_dec_default.h"
#include "esp_video_codec_types.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_app_unit_test.h"

#include "esp_player.h"
#include "esp_player_advance.h"
#include "render_common.h"
#include "test_data.h"

#define SC_PLAYED_BIT      (1 << 0)
#define SC_PAUSED_BIT      (1 << 1)
#define SC_STOPPED_BIT     (1 << 2)
#define SC_SEEK_DONE_BIT   (1 << 3)
#define SC_FINISHED_BIT    (1 << 4)
#define SC_ERROR_BIT       (1 << 5)
#define SC_BUFFERING_BIT   (1 << 6)
#define SC_BUFFERED_BIT    (1 << 7)
#define SC_TRACK_INFO_BIT  (1 << 8)

#define SC_TIMEOUT_PLAY_MS    10000
#define SC_TIMEOUT_STOP_MS    8000
#define SC_TIMEOUT_SEEK_MS    5000
#define SC_TIMEOUT_FINISH_MS  120000

typedef struct {
    esp_player_handle_t  player;
    EventGroupHandle_t   event_group;
    void                *audio_stream_handle;
    void                *video_stream_handle;
    uint8_t              stream_idx;
    volatile int64_t     run_time_us;
    volatile int64_t     played_time_us;
    volatile int64_t     seek_time_us;
    volatile int64_t     seek_done_time_us;
} sc_ctx_t;

typedef struct {
    sc_ctx_t           *ctx;
    const char         *url;
    const char         *play_seq;  /* 'r'=run,'p'=pause,'e'=resume,'s'=stop,'f'=finish */
    EventGroupHandle_t  done_group;
    EventBits_t         done_bit;
} sc_mixer_task_arg_t;

typedef struct {
    esp_player_handle_t  player;
    int                  rounds;
    int                 *ok_count;
    SemaphoreHandle_t    counter_mutex;
    EventGroupHandle_t   done_group;
    EventBits_t          done_bit;
    int                  task_priority;
} sc_mt_seek_arg_t;

typedef struct {
    esp_player_handle_t  player;
    volatile bool       *running_flag;
    EventGroupHandle_t   done_group;
    EventBits_t          done_bit;
} sc_priority_arg_t;

typedef struct {
    esp_player_handle_t  player;
    int                  role;
    int                  rounds;
    EventGroupHandle_t   done_group;
    EventBits_t          done_bit;
    int                  task_priority;
} sc_mt_stop_run_arg_t;

static const char *TAG = "TEST_SCENARIO";

static esp_player_err_t sc_event_cb(esp_player_event_msg_t *event, void *user_ctx)
{
    sc_ctx_t *c = (sc_ctx_t *)user_ctx;
    if (!c || !c->event_group) {
        return ESP_PLAYER_ERR_OK;
    }
    switch (event->event_type) {
        case ESP_PLAYER_EVENT_PLAYED:
            c->played_time_us = esp_timer_get_time();
            xEventGroupSetBits(c->event_group, SC_PLAYED_BIT);
            ESP_LOGD(TAG, "[stream%u] PLAYED", c->stream_idx);
            break;
        case ESP_PLAYER_EVENT_PAUSED:
            xEventGroupSetBits(c->event_group, SC_PAUSED_BIT);
            ESP_LOGD(TAG, "[stream%u] PAUSED", c->stream_idx);
            break;
        case ESP_PLAYER_EVENT_STOPPED:
            xEventGroupSetBits(c->event_group, SC_STOPPED_BIT);
            ESP_LOGD(TAG, "[stream%u] STOPPED", c->stream_idx);
            break;
        case ESP_PLAYER_EVENT_SEEK_DONE:
            c->seek_done_time_us = esp_timer_get_time();
            xEventGroupSetBits(c->event_group, SC_SEEK_DONE_BIT);
            ESP_LOGD(TAG, "[stream%u] SEEK_DONE", c->stream_idx);
            break;
        case ESP_PLAYER_EVENT_FINISHED:
            xEventGroupSetBits(c->event_group, SC_FINISHED_BIT);
            ESP_LOGD(TAG, "[stream%u] FINISHED", c->stream_idx);
            break;
        case ESP_PLAYER_EVENT_ERROR:
            xEventGroupSetBits(c->event_group, SC_ERROR_BIT);
            ESP_LOGW(TAG, "[stream%u] ERROR", c->stream_idx);
            break;
        case ESP_PLAYER_EVENT_BUFFERING:
            xEventGroupSetBits(c->event_group, SC_BUFFERING_BIT);
            ESP_LOGW(TAG, "[stream%u] BUFFERING", c->stream_idx);
            break;
        case ESP_PLAYER_EVENT_BUFFERED:
            xEventGroupSetBits(c->event_group, SC_BUFFERED_BIT);
            ESP_LOGW(TAG, "[stream%u] BUFFERED", c->stream_idx);
            break;
        case ESP_PLAYER_EVENT_TRACK_INFO_PARSED:
            xEventGroupSetBits(c->event_group, SC_TRACK_INFO_BIT);
            ESP_LOGD(TAG, "[stream%u] TRACK_INFO_PARSED", c->stream_idx);
            break;
        default:
            break;
    }
    return ESP_PLAYER_ERR_OK;
}

static bool sc_wait_bits(sc_ctx_t *c, EventBits_t bits, uint32_t timeout_ms)
{
    EventBits_t got = xEventGroupWaitBits(c->event_group, bits,
                                          pdTRUE, pdFALSE,
                                          pdMS_TO_TICKS(timeout_ms));
    return (got & bits) != 0;
}

static bool sc_wait_queue_event(QueueHandle_t queue, esp_player_event_type_t want, uint32_t timeout_ms)
{
    esp_player_event_msg_t msg;
    int64_t deadline_us = esp_timer_get_time() + (int64_t)timeout_ms * 1000;

    while (esp_timer_get_time() < deadline_us) {
        if (xQueueReceive(queue, &msg, pdMS_TO_TICKS(50)) == pdTRUE) {
            ESP_LOGI(TAG, "event_queue recv: type=%d", (int)msg.event_type);
            if (msg.event_type == want) {
                return true;
            }
            if (msg.event_type == ESP_PLAYER_EVENT_ERROR) {
                return false;
            }
        }
    }
    return false;
}

static void sc_clear_bits(sc_ctx_t *c, EventBits_t bits)
{
    xEventGroupClearBits(c->event_group, bits);
}

static esp_player_err_t sc_create_audio_player(sc_ctx_t *c, uint8_t stream_idx,
                                               uint32_t sample_rate,
                                               uint8_t bits_per_sample,
                                               uint8_t channels)
{
    memset(c, 0, sizeof(*c));
    c->stream_idx = stream_idx;

    c->event_group = xEventGroupCreate();
    if (!c->event_group) {
        ESP_LOGE(TAG, "xEventGroupCreate failed");
        return ESP_PLAYER_ERR_NO_MEM;
    }

    esp_player_err_t ret = audio_render_create_handle(
        (esp_audio_render_stream_handle_t *)&c->audio_stream_handle,
        sample_rate, bits_per_sample, channels,
        ESP_AUDIO_RENDER_STREAM_ID(stream_idx));
    if (ret != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(TAG, "audio_render_create_handle stream%u failed: %d", stream_idx, ret);
        vEventGroupDelete(c->event_group);
        c->event_group = NULL;
        return ret;
    }

    esp_player_config_t cfg = ESP_PLAYER_CONFIG_DEFAULT();
    cfg.audio_render_hd = c->audio_stream_handle;
    ret = esp_player_init(&cfg, &c->player);
    if (ret != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(TAG, "esp_player_init stream%u failed: %d", stream_idx, ret);
        vEventGroupDelete(c->event_group);
        c->event_group = NULL;
        return ret;
    }
    esp_player_set_event_cb(c->player, sc_event_cb, c);

    esp_player_set_av_mask(c->player, ESP_PLAYER_MASK_AUDIO);
    return ESP_PLAYER_ERR_OK;
}

static void sc_destroy_player_only(sc_ctx_t *c)
{
    if (c->player) {
        esp_player_set_event_cb(c->player, NULL, NULL);
        esp_player_deinit(c->player);
        c->player = NULL;
    }
    if (c->event_group) {
        vEventGroupDelete(c->event_group);
        c->event_group = NULL;
    }
}

static void sc_destroy_player_and_render(sc_ctx_t *c)
{
    sc_destroy_player_only(c);
    audio_render_destroy_handle();
}

static void sc_destroy_av_player_and_render(sc_ctx_t *c)
{
    sc_destroy_player_only(c);
    if (c->video_stream_handle) {
        video_render_destroy_handle();
        c->video_stream_handle = NULL;
    }
    audio_render_destroy_handle();
}

static esp_player_err_t sc_create_av_player(sc_ctx_t *c, uint8_t stream_idx)
{
    memset(c, 0, sizeof(*c));
    c->stream_idx = stream_idx;

    c->event_group = xEventGroupCreate();
    if (!c->event_group) {
        ESP_LOGE(TAG, "xEventGroupCreate failed");
        return ESP_PLAYER_ERR_NO_MEM;
    }

    esp_player_err_t ret = audio_render_create_handle(
        (esp_audio_render_stream_handle_t *)&c->audio_stream_handle,
        44100, 16, 2, ESP_AUDIO_RENDER_STREAM_ID(stream_idx));
    if (ret != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(TAG, "audio_render_create_handle stream%u failed: %d", stream_idx, ret);
        vEventGroupDelete(c->event_group);
        c->event_group = NULL;
        return ret;
    }

    void *vid_handle = NULL;
    video_render_create_handle(&vid_handle, ESP_VIDEO_CODEC_PIXEL_FMT_RGB888, 30);
    c->video_stream_handle = vid_handle;

    esp_player_config_t cfg = ESP_PLAYER_CONFIG_DEFAULT();
    cfg.audio_render_hd = c->audio_stream_handle;
    cfg.video_render_hd = c->video_stream_handle;
    ret = esp_player_init(&cfg, &c->player);
    if (ret != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(TAG, "esp_player_init stream%u failed: %d", stream_idx, ret);
        sc_destroy_av_player_and_render(c);
        return ret;
    }

    esp_player_set_event_cb(c->player, sc_event_cb, c);
    esp_player_set_av_mask(c->player, ESP_PLAYER_MASK_AV);
    return ESP_PLAYER_ERR_OK;
}

static bool sc_get_first_file(const char *dir_path, const char *ext,
                              char *out, size_t out_sz)
{
    DIR *d = opendir(dir_path);
    if (!d) {
        ESP_LOGW(TAG, "sc_get_first_file: opendir(%s) failed: %s",
                 dir_path, strerror(errno));
        return false;
    }
    char tmp[TEST_PATH_MAX_LEN];
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        if (e->d_type == DT_DIR) {
            continue;
        }
        if (ext) {
            const char *dot = strrchr(e->d_name, '.');
            if (!dot || strcasecmp(dot, ext) != 0) {
                continue;
            }
        }
        int ret = snprintf(tmp, sizeof(tmp), "%s/%s", dir_path, e->d_name);
        if (ret > 0 && ret < (int)sizeof(tmp)) {
            closedir(d);
            strlcpy(out, tmp, out_sz);
            return true;
        }
    }
    closedir(d);
    ESP_LOGW(TAG, "sc_get_first_file: no matching file in %s (ext=%s)",
             dir_path, ext ? ext : "any");
    return false;
}

static bool sc_run_and_wait_played(sc_ctx_t *c)
{
    c->run_time_us = esp_timer_get_time();
    esp_player_run(c->player);

    EventBits_t wait_bits = SC_PLAYED_BIT | SC_ERROR_BIT | SC_FINISHED_BIT;
    EventBits_t got = xEventGroupWaitBits(c->event_group, wait_bits,
                                          pdTRUE, pdFALSE,
                                          pdMS_TO_TICKS(SC_TIMEOUT_PLAY_MS));
    return (got & SC_PLAYED_BIT) != 0;
}

static bool sc_run_and_wait_prepared(sc_ctx_t *c)
{
    c->run_time_us = esp_timer_get_time();
    esp_player_run(c->player);

    EventBits_t wait_bits = SC_TRACK_INFO_BIT | SC_ERROR_BIT;
    EventBits_t got = xEventGroupWaitBits(c->event_group, wait_bits,
                                          pdTRUE, pdFALSE,
                                          pdMS_TO_TICKS(SC_TIMEOUT_PLAY_MS));
    return (got & SC_TRACK_INFO_BIT) != 0;
}

static bool sc_stop_and_wait(sc_ctx_t *c)
{
    esp_player_stop(c->player);
    return sc_wait_bits(c, SC_STOPPED_BIT | SC_FINISHED_BIT, SC_TIMEOUT_STOP_MS);
}

static void sc_trigger_full_error(sc_ctx_t *ctx, const char *error_url)
{
    sc_clear_bits(ctx, SC_ERROR_BIT | SC_FINISHED_BIT | SC_PLAYED_BIT | SC_STOPPED_BIT | SC_PAUSED_BIT | SC_SEEK_DONE_BIT);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_set_url(ctx->player, error_url));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_run(ctx->player));

    EventBits_t bits = xEventGroupWaitBits(ctx->event_group,
                                           SC_ERROR_BIT | SC_FINISHED_BIT | SC_PLAYED_BIT,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(SC_TIMEOUT_PLAY_MS));
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bits & SC_ERROR_BIT,
                                  "Expected ERROR from invalid media source");
    TEST_ASSERT_EQUAL_MESSAGE(0, bits & SC_PLAYED_BIT,
                              "Full error source unexpectedly reached PLAYING");
}

static void sc_recover_with_valid_audio_and_wait_playing(sc_ctx_t *ctx, const char *valid_audio_url)
{
    sc_clear_bits(ctx, SC_ERROR_BIT | SC_PLAYED_BIT | SC_FINISHED_BIT | SC_STOPPED_BIT);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_set_url(ctx->player, valid_audio_url));
    TEST_ASSERT_TRUE_MESSAGE(sc_run_and_wait_played(ctx),
                             "Expected valid audio source to recover and reach PLAYING");
}

static void sc_concurrent_seek_task(void *arg)
{
    sc_mt_seek_arg_t *a = (sc_mt_seek_arg_t *)arg;
    int local_ok = 0;

    for (int i = 0; i < a->rounds; i++) {
        uint64_t target_ms = (esp_random() % 10) * 1000;
        esp_player_err_t ret = esp_player_seek(a->player, target_ms);
        if (ret == ESP_PLAYER_ERR_OK) {
            local_ok++;
        }
        vTaskDelay(pdMS_TO_TICKS(50 + esp_random() % 100));
    }

    xSemaphoreTake(a->counter_mutex, portMAX_DELAY);
    *(a->ok_count) += local_ok;
    xSemaphoreGive(a->counter_mutex);

    xEventGroupSetBits(a->done_group, a->done_bit);
    vTaskDelete(NULL);
}

static void sc_stop_run_race_task(void *arg)
{
    sc_mt_stop_run_arg_t *a = (sc_mt_stop_run_arg_t *)arg;

    for (int i = 0; i < a->rounds; i++) {
        if (a->role == 0) {
            esp_player_run(a->player);
            vTaskDelay(pdMS_TO_TICKS(80 + esp_random() % 120));
            esp_player_stop(a->player);
        } else {
            esp_player_stop(a->player);
            vTaskDelay(pdMS_TO_TICKS(20 + esp_random() % 60));
            esp_player_run(a->player);
        }
        vTaskDelay(pdMS_TO_TICKS(40 + esp_random() % 80));
    }

    xEventGroupSetBits(a->done_group, a->done_bit);
    vTaskDelete(NULL);
}

static void sc_mixer_player_task(void *arg)
{
    sc_mixer_task_arg_t *a = (sc_mixer_task_arg_t *)arg;
    sc_ctx_t *c = a->ctx;

    if (a->url) {
        esp_player_set_url(c->player, a->url);
    }

    for (const char *p = a->play_seq; *p; p++) {
        switch (*p) {
            case 'r':
                sc_run_and_wait_played(c);
                vTaskDelay(pdMS_TO_TICKS(2000));
                break;
            case 'p':
                if (esp_player_pause(c->player) == ESP_PLAYER_ERR_OK) {
                    sc_wait_bits(c, SC_PAUSED_BIT, SC_TIMEOUT_PLAY_MS);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
                break;
            case 'e':
                if (esp_player_resume(c->player) == ESP_PLAYER_ERR_OK) {
                    sc_wait_bits(c, SC_PLAYED_BIT, SC_TIMEOUT_PLAY_MS);
                    vTaskDelay(pdMS_TO_TICKS(2000));
                }
                break;
            case 's':
                sc_stop_and_wait(c);
                vTaskDelay(pdMS_TO_TICKS(500));
                break;
            case 'f':
                sc_wait_bits(c, SC_FINISHED_BIT, SC_TIMEOUT_FINISH_MS);
                break;
            default:
                break;
        }
    }

    if (a->done_group) {
        xEventGroupSetBits(a->done_group, a->done_bit);
    }
    vTaskDelete(NULL);
}

static void sc_high_priority_disruptor(void *arg)
{
    sc_priority_arg_t *a = (sc_priority_arg_t *)arg;
    int round = 0;
    while (*(a->running_flag)) {
        uint32_t r = esp_random() % 3;
        switch (r) {
            case 0:
                esp_player_pause(a->player);
                break;
            case 1:
                esp_player_resume(a->player);
                break;
            case 2:
                esp_player_seek(a->player, (esp_random() % 8) * 1000);
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(300 + esp_random() % 700));
        round++;
    }
    ESP_LOGI(TAG, "[PriorityTest] Disruptor finished after %d rounds", round);
    xEventGroupSetBits(a->done_group, a->done_bit);
    vTaskDelete(NULL);
}

static void sc_log_id3_info(const char *url, const esp_extractor_id3_info_t *info)
{
    ESP_LOGI(TAG, "======== ID3 metadata: %s ========", url ? url : "(null)");
    if (info == NULL) {
        ESP_LOGI(TAG, "  (no info)");
        return;
    }
    ESP_LOGI(TAG, "  title   : %s", info->title ? info->title : "(null)");
    ESP_LOGI(TAG, "  author  : %s", info->author ? info->author : "(null)");
    ESP_LOGI(TAG, "  album   : %s", info->album ? info->album : "(null)");
    ESP_LOGI(TAG, "  date    : %s", info->date ? info->date : "(null)");
    ESP_LOGI(TAG, "  genre   : %s", info->genre ? info->genre : "(null)");
    ESP_LOGI(TAG, "  encoding: %u", (unsigned)info->encoding);
    ESP_LOGI(TAG, "  cover   : mime=%s size=%" PRIu32,
             info->cover_mime ? info->cover_mime : "(null)", info->cover_size);
    for (uint16_t i = 0; i < info->extra_num; i++) {
        ESP_LOGI(TAG, "  extra[%u]: %s = %s (encoding=%u)", (unsigned)i,
                 info->extra[i].key ? info->extra[i].key : "(null)",
                 info->extra[i].value ? info->extra[i].value : "(null)",
                 (unsigned)info->extra[i].encoding);
    }
    ESP_LOGI(TAG, "========================================");
}

TEST_CASE("[metadata]:test_player_get_id3_info_mp3", "[player][scenario]")
{
    const char *mp3_file = "/sdcard/test.mp3";
    struct stat st;
    if (stat(mp3_file, &st) != 0) {
        TEST_IGNORE_MESSAGE("Missing /sdcard/test.mp3 on SD card");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));

    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_enable_id3_parse(ctx.player, true));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_set_url(ctx.player, mp3_file));
    TEST_ASSERT_TRUE_MESSAGE(sc_run_and_wait_prepared(&ctx), "Expected TRACK_INFO_PARSED before reading ID3");

    const esp_extractor_id3_info_t *id3 = NULL;
    esp_player_err_t ret = esp_player_get_id3_info(ctx.player, &id3);
    if (ret == ESP_PLAYER_ERR_FAIL) {
        ESP_LOGW(TAG, "MP3 file has no ID3 tag: %s", mp3_file);
        TEST_IGNORE_MESSAGE("MP3 test file has no ID3 tags; use an MP3 with ID3v2 metadata");
    }
    TEST_ASSERT_EQUAL_MESSAGE(ESP_PLAYER_ERR_OK, ret, "esp_player_get_id3_info should succeed for tagged MP3");
    TEST_ASSERT_NOT_NULL(id3);

    sc_log_id3_info(mp3_file, id3);

    bool has_text = (id3->title && id3->title[0] != '\0')
                    || (id3->author && id3->author[0] != '\0')
                    || (id3->album && id3->album[0] != '\0');
    TEST_ASSERT_TRUE_MESSAGE(has_text,
                             "ID3 parsed but title/author/album are all empty");

    TEST_ASSERT_TRUE(sc_stop_and_wait(&ctx));
    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[switch]:test_player_sequential_url_switch", "[player][scenario]")
{
    char m4a_file[TEST_PATH_MAX_LEN] = {0};
    char mp3_file[TEST_PATH_MAX_LEN] = {0};

    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", m4a_file, sizeof(m4a_file))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".mp3", mp3_file, sizeof(mp3_file))) {
        TEST_IGNORE_MESSAGE("No .mp3 file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));

    const char *urls[] = {m4a_file, mp3_file, m4a_file, mp3_file};
    uint64_t durations[4] = {0};

    for (int i = 0; i < 4; i++) {
        ESP_LOGI(TAG, "==> Sequential switch [%d]: %s", i, urls[i]);

        TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                          esp_player_set_url(ctx.player, urls[i]));
        TEST_ASSERT_TRUE_MESSAGE(sc_run_and_wait_played(&ctx),
                                 "Expected PLAYED event");

        esp_player_get_duration(ctx.player, &durations[i]);
        ESP_LOGI(TAG, "    duration = %" PRIu64 " ms", durations[i]);

        uint64_t play_slice = (durations[i] > 0 && durations[i] < 5000)
                                  ? durations[i] / 4
                                  : 2000;
        vTaskDelay(pdMS_TO_TICKS(play_slice));

        TEST_ASSERT_TRUE_MESSAGE(sc_stop_and_wait(&ctx), "Expected STOPPED event");
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[switch]:test_player_rapid_url_switch_stress", "[player][scenario]")
{
    char file_a[TEST_PATH_MAX_LEN] = {0};
    char file_b[TEST_PATH_MAX_LEN] = {0};

    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", file_a, sizeof(file_a))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".mp3", file_b, sizeof(file_b))) {
        strlcpy(file_b, file_a, sizeof(file_b));
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));

    const char *toggle[] = {file_a, file_b};
    const int rounds = 15;
    int stop_ok = 0, play_ok = 0;

    for (int i = 0; i < rounds; i++) {
        const char *url = toggle[i % 2];
        esp_player_set_url(ctx.player, url);

        ctx.run_time_us = esp_timer_get_time();
        esp_player_run(ctx.player);

        vTaskDelay(pdMS_TO_TICKS(200));
        esp_player_stop(ctx.player);
        (void)ctx.run_time_us;

        bool got = sc_wait_bits(&ctx, SC_STOPPED_BIT | SC_FINISHED_BIT, SC_TIMEOUT_STOP_MS);
        if (got) {
            stop_ok++;
        }
        play_ok++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "Rapid switch: %d/%d stop events received", stop_ok, rounds);
    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[switch]:test_player_format_switch_m4a_mp3", "[player][scenario]")
{
    char m4a[TEST_PATH_MAX_LEN] = {0};
    char mp3[TEST_PATH_MAX_LEN] = {0};

    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", m4a, sizeof(m4a))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".mp3", mp3, sizeof(mp3))) {
        TEST_IGNORE_MESSAGE("No .mp3 file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));

    const char *seq[] = {m4a, mp3, m4a};
    for (int i = 0; i < 3; i++) {
        ESP_LOGI(TAG, "Format switch [%d]: %s", i, seq[i]);
        TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                          esp_player_set_url(ctx.player, seq[i]));
        TEST_ASSERT_TRUE(sc_run_and_wait_played(&ctx));

        uint64_t dur = 0;
        esp_player_get_duration(ctx.player, &dur);
        uint64_t slice = (dur > 0 && dur < 4000) ? dur / 3 : 1500;
        vTaskDelay(pdMS_TO_TICKS(slice));

        TEST_ASSERT_TRUE(sc_stop_and_wait(&ctx));
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[switch]:test_player_av_mask_dynamic_switch", "[player][scenario]")
{
    char m4a[TEST_PATH_MAX_LEN] = {0};
    char mp4[TEST_PATH_MAX_LEN] = {0};

    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", m4a, sizeof(m4a))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }
    if (!sc_get_first_file(TEST_FILE_AV_PATH, ".mp4", mp4, sizeof(mp4))) {
        TEST_IGNORE_MESSAGE("No .mp4 file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, sc_create_av_player(&ctx, 0));

    const struct {
        uint8_t  mask;
        const char *url;
        const char *name;
    } rounds[] = {
        {ESP_PLAYER_MASK_AUDIO, m4a, "AUDIO_ONLY + M4A"},
        {ESP_PLAYER_MASK_AV, mp4, "AV + MP4"},
        {ESP_PLAYER_MASK_AUDIO, m4a, "AUDIO_ONLY + M4A (re-switch)"},
    };
    const int round_count = (int)(sizeof(rounds) / sizeof(rounds[0]));

    for (int i = 0; i < round_count; i++) {
        ESP_LOGI(TAG, "AV mask switch [%d]: %s", i, rounds[i].name);
        esp_player_set_av_mask(ctx.player, rounds[i].mask);
        TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                          esp_player_set_url(ctx.player, rounds[i].url));
        TEST_ASSERT_TRUE_MESSAGE(sc_run_and_wait_played(&ctx),
                                 rounds[i].name);

        vTaskDelay(pdMS_TO_TICKS(2000));

        TEST_ASSERT_TRUE(sc_stop_and_wait(&ctx));
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    sc_destroy_av_player_and_render(&ctx);
}

TEST_CASE("[switch]:test_player_runtime_track_switch", "[player][scenario]")
{
    char mp4[TEST_PATH_MAX_LEN] = {0};
    if (!sc_get_first_file(TEST_FILE_AV_PATH, ".mp4", mp4, sizeof(mp4))) {
        TEST_IGNORE_MESSAGE("No .mp4 file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, sc_create_av_player(&ctx, 0));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_set_url(ctx.player, mp4));
    TEST_ASSERT_TRUE_MESSAGE(sc_run_and_wait_played(&ctx), "AV stream failed to start");

    sc_clear_bits(&ctx, SC_ERROR_BIT);
    vTaskDelay(pdMS_TO_TICKS(1500));

    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      esp_player_enable_track(ctx.player, ESP_PLAYER_TRACK_TYPE_AUDIO, 0, false));
    vTaskDelay(pdMS_TO_TICKS(1000));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      esp_player_enable_track(ctx.player, ESP_PLAYER_TRACK_TYPE_AUDIO, 0, true));
    vTaskDelay(pdMS_TO_TICKS(1000));

    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      esp_player_enable_track(ctx.player, ESP_PLAYER_TRACK_TYPE_VIDEO, 0, false));
    vTaskDelay(pdMS_TO_TICKS(1000));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      esp_player_enable_track(ctx.player, ESP_PLAYER_TRACK_TYPE_VIDEO, 0, true));
    vTaskDelay(pdMS_TO_TICKS(1000));

    EventBits_t bits = xEventGroupGetBits(ctx.event_group);
    TEST_ASSERT_EQUAL_MESSAGE(0, bits & SC_ERROR_BIT, "Unexpected ERROR during track switch");

    TEST_ASSERT_TRUE(sc_stop_and_wait(&ctx));
    sc_destroy_av_player_and_render(&ctx);
}

TEST_CASE("[scenario]:test_player_av_sync_mode_play", "[player][scenario]")
{
    char mp4[TEST_PATH_MAX_LEN] = {0};
    if (!sc_get_first_file(TEST_FILE_AV_PATH, ".mp4", mp4, sizeof(mp4))) {
        TEST_IGNORE_MESSAGE("No .mp4 file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, sc_create_av_player(&ctx, 0));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_set_url(ctx.player, mp4));

    static const struct {
        esp_player_sync_mode_t  mode;
        const char *name;
    } modes[] = {
        {ESP_PLAYER_SYNC_MODE_SYSTEM, "SYSTEM"},
        {ESP_PLAYER_SYNC_MODE_AUDIO, "AUDIO"},
        {ESP_PLAYER_SYNC_MODE_VIDEO, "VIDEO"},
        {ESP_PLAYER_SYNC_MODE_NONE, "NONE"},
    };

    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        ESP_LOGI(TAG, "AV sync mode round [%u]: %s", (unsigned)i, modes[i].name);

        TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_set_sync_mode(ctx.player, modes[i].mode));

        sc_clear_bits(&ctx, SC_ERROR_BIT | SC_PLAYED_BIT | SC_STOPPED_BIT);
        TEST_ASSERT_TRUE_MESSAGE(sc_run_and_wait_played(&ctx), "Expected PLAYED with sync mode");

        vTaskDelay(pdMS_TO_TICKS(1500));
        TEST_ASSERT_EQUAL_MESSAGE(0, xEventGroupGetBits(ctx.event_group) & SC_ERROR_BIT, "Unexpected ERROR during sync mode playback");

        TEST_ASSERT_TRUE(sc_stop_and_wait(&ctx));
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    sc_destroy_av_player_and_render(&ctx);
}

TEST_CASE("[mixer]:test_player_3stream_concurrent", "[player][scenario]")
{
    char m4a[TEST_PATH_MAX_LEN] = {0};
    char mp3[TEST_PATH_MAX_LEN] = {0};

    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", m4a, sizeof(m4a))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".mp3", mp3, sizeof(mp3))) {
        strlcpy(mp3, m4a, sizeof(mp3));
    }

    audio_render_set_max_stream_num(3);

    sc_ctx_t ctx[3] = {0};
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                          sc_create_audio_player(&ctx[i], i, 44100, 16, 2));
    }

    EventGroupHandle_t done_group = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL(done_group);

    const char *urls[] = {m4a, mp3, m4a};
    const char *seqs[] = {"rs", "rs", "rs"};
    EventBits_t done_bits[] = {1 << 0, 1 << 1, 1 << 2};

    sc_mixer_task_arg_t args[3];
    for (int i = 0; i < 3; i++) {
        args[i].ctx = &ctx[i];
        args[i].url = urls[i];
        args[i].play_seq = seqs[i];
        args[i].done_group = done_group;
        args[i].done_bit = done_bits[i];
        xTaskCreate(sc_mixer_player_task, "sc_mix_task",
                    20480, &args[i], 5, NULL);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    EventBits_t all = done_bits[0] | done_bits[1] | done_bits[2];
    EventBits_t got = xEventGroupWaitBits(done_group, all,
                                          pdTRUE, pdTRUE,
                                          pdMS_TO_TICKS(SC_TIMEOUT_FINISH_MS));
    TEST_ASSERT_EQUAL_MESSAGE(all, got & all, "Not all 3 mixer tasks completed");

    vEventGroupDelete(done_group);
    for (int i = 0; i < 3; i++) {
        sc_destroy_player_only(&ctx[i]);
    }
    audio_render_destroy_handle();
}

TEST_CASE("[mixer]:test_player_mixer_independent_control", "[player][scenario]")
{
    char file_a[TEST_PATH_MAX_LEN] = {0};
    char file_b[TEST_PATH_MAX_LEN] = {0};

    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", file_a, sizeof(file_a))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".aac", file_b, sizeof(file_b))) {
        strlcpy(file_b, file_a, sizeof(file_b));
    }

    audio_render_set_max_stream_num(2);

    sc_ctx_t ctx0 = {0}, ctx1 = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx0, 0, 44100, 16, 2));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx1, 1, 44100, 16, 2));

    esp_player_set_url(ctx0.player, file_a);
    TEST_ASSERT_TRUE(sc_run_and_wait_played(&ctx0));

    esp_player_set_url(ctx1.player, file_b);
    TEST_ASSERT_TRUE(sc_run_and_wait_played(&ctx1));

    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "Pausing stream1 while stream0 keeps playing...");
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_pause(ctx1.player));
    TEST_ASSERT_TRUE(sc_wait_bits(&ctx1, SC_PAUSED_BIT, SC_TIMEOUT_PLAY_MS));

    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "Resuming stream1...");
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_resume(ctx1.player));
    TEST_ASSERT_TRUE(sc_wait_bits(&ctx1, SC_PLAYED_BIT, SC_TIMEOUT_PLAY_MS));

    vTaskDelay(pdMS_TO_TICKS(2000));

    sc_stop_and_wait(&ctx0);
    sc_stop_and_wait(&ctx1);

    sc_destroy_player_only(&ctx0);
    sc_destroy_player_only(&ctx1);
    audio_render_destroy_handle();
}

TEST_CASE("[mixer]:test_player_mixer_stream_end_continue", "[player][scenario]")
{
    char long_file[TEST_PATH_MAX_LEN] = {0};
    char short_file[TEST_PATH_MAX_LEN] = {0};

    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", long_file, sizeof(long_file))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".mp3", short_file, sizeof(short_file))) {
        strlcpy(short_file, long_file, sizeof(short_file));
    }

    audio_render_set_max_stream_num(2);

    sc_ctx_t ctx_long = {0}, ctx_short = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx_long, 0, 44100, 16, 2));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx_short, 1, 44100, 16, 2));

    esp_player_set_url(ctx_long.player, long_file);
    TEST_ASSERT_TRUE(sc_run_and_wait_played(&ctx_long));

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_player_set_url(ctx_short.player, short_file);
    TEST_ASSERT_TRUE(sc_run_and_wait_played(&ctx_short));

    bool short_done = sc_wait_bits(&ctx_short, SC_FINISHED_BIT | SC_STOPPED_BIT,
                                   SC_TIMEOUT_FINISH_MS);
    ESP_LOGI(TAG, "Short stream finished: %s", short_done ? "YES" : "NO (timeout)");

    uint64_t cur_time = 0;
    esp_player_get_play_time(ctx_long.player, &cur_time);
    ESP_LOGI(TAG, "Long stream current time after short ends: %" PRIu64 " ms", cur_time);
    TEST_ASSERT_GREATER_THAN(0, cur_time);

    sc_stop_and_wait(&ctx_long);

    sc_destroy_player_only(&ctx_long);
    sc_destroy_player_only(&ctx_short);
    audio_render_destroy_handle();
}

TEST_CASE("[usecase]:test_player_bgm_pause_for_prompt", "[player][scenario]")
{
    char bgm[TEST_PATH_MAX_LEN] = {0};
    char prompt[TEST_PATH_MAX_LEN] = {0};

    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", bgm, sizeof(bgm))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".mp3", prompt, sizeof(prompt))) {
        strlcpy(prompt, bgm, sizeof(prompt));
    }

    audio_render_set_max_stream_num(2);

    sc_ctx_t bgm_ctx = {0};
    sc_ctx_t prompt_ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&bgm_ctx, 0, 44100, 16, 2));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&prompt_ctx, 1, 44100, 16, 2));

    ESP_LOGI(TAG, "[BgmPrompt] Start BGM");
    esp_player_set_url(bgm_ctx.player, bgm);
    TEST_ASSERT_TRUE(sc_run_and_wait_played(&bgm_ctx));
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "[BgmPrompt] Pause BGM, play prompt, resume BGM");
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_pause(bgm_ctx.player));
    TEST_ASSERT_TRUE(sc_wait_bits(&bgm_ctx, SC_PAUSED_BIT, SC_TIMEOUT_PLAY_MS));

    esp_player_set_url(prompt_ctx.player, prompt);
    TEST_ASSERT_TRUE(sc_run_and_wait_played(&prompt_ctx));
    bool prompt_done = sc_wait_bits(&prompt_ctx, SC_FINISHED_BIT | SC_STOPPED_BIT,
                                    SC_TIMEOUT_FINISH_MS);
    ESP_LOGI(TAG, "[BgmPrompt] Prompt ended: %s", prompt_done ? "YES" : "TIMEOUT");

    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_resume(bgm_ctx.player));
    TEST_ASSERT_TRUE(sc_wait_bits(&bgm_ctx, SC_PLAYED_BIT, SC_TIMEOUT_PLAY_MS));

    uint64_t resumed_time = 0;
    esp_player_get_play_time(bgm_ctx.player, &resumed_time);
    ESP_LOGI(TAG, "[BgmPrompt] BGM resumed at %" PRIu64 " ms", resumed_time);
    TEST_ASSERT_GREATER_THAN(500, resumed_time);

    vTaskDelay(pdMS_TO_TICKS(1000));
    sc_stop_and_wait(&bgm_ctx);

    sc_destroy_player_only(&bgm_ctx);
    sc_destroy_player_only(&prompt_ctx);
    audio_render_destroy_handle();
}

TEST_CASE("[usecase]:test_player_voice_mix_over_bgm", "[player][scenario]")
{
    char bgm[TEST_PATH_MAX_LEN] = {0};
    char voice[TEST_PATH_MAX_LEN] = {0};

    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", bgm, sizeof(bgm))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".mp3", voice, sizeof(voice))) {
        strlcpy(voice, bgm, sizeof(voice));
    }

    audio_render_set_max_stream_num(2);

    sc_ctx_t bgm_ctx = {0};
    sc_ctx_t voice_ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&bgm_ctx, 0, 44100, 16, 2));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&voice_ctx, 1, 44100, 16, 2));

    esp_player_set_url(bgm_ctx.player, bgm);
    TEST_ASSERT_TRUE(sc_run_and_wait_played(&bgm_ctx));

    const int OVERLAY_ROUNDS = 3;
    for (int i = 0; i < OVERLAY_ROUNDS; i++) {
        ESP_LOGI(TAG, "[VoiceMix] Overlay prompt #%d/%d",
                 i + 1, OVERLAY_ROUNDS);
        vTaskDelay(pdMS_TO_TICKS(3000));

        esp_player_set_url(voice_ctx.player, voice);
        TEST_ASSERT_TRUE(sc_run_and_wait_played(&voice_ctx));

        bool done = sc_wait_bits(&voice_ctx, SC_FINISHED_BIT | SC_ERROR_BIT, 10000);
        ESP_LOGI(TAG, "[VoiceMix] Prompt #%d done: %s",
                 i + 1, done ? "YES" : "TIMEOUT");
        if (!done) {
            sc_stop_and_wait(&voice_ctx);
        }
    }

    sc_stop_and_wait(&bgm_ctx);

    sc_destroy_player_only(&bgm_ctx);
    sc_destroy_player_only(&voice_ctx);
    audio_render_destroy_handle();
}

TEST_CASE("[perf]:test_player_startup_latency", "[player][scenario]")
{
    char m4a[TEST_PATH_MAX_LEN] = {0};
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", m4a, sizeof(m4a))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));
    esp_player_set_url(ctx.player, m4a);

    const int N = 5;
    int64_t sum = 0;
    int64_t max_us = 0;
    const int64_t limit_us = 3000000LL;

    for (int i = 0; i < N; i++) {
        sc_clear_bits(&ctx, SC_PLAYED_BIT | SC_ERROR_BIT | SC_STOPPED_BIT | SC_FINISHED_BIT);

        ctx.run_time_us = 0;
        ctx.played_time_us = 0;

        ctx.run_time_us = esp_timer_get_time();
        esp_player_run(ctx.player);
        bool ok = sc_wait_bits(&ctx, SC_PLAYED_BIT | SC_ERROR_BIT, SC_TIMEOUT_PLAY_MS);
        TEST_ASSERT_TRUE_MESSAGE(ok, "Startup: no PLAYED event");

        int64_t latency_us = ctx.played_time_us - ctx.run_time_us;
        if (latency_us < 0) {
            latency_us = 0;
        }
        sum += latency_us;
        if (latency_us > max_us) {
            max_us = latency_us;
        }

        ESP_LOGI(TAG, "[StartupLatency] Round %d: %" PRId64 " µs (%.2f ms)",
                 i + 1, latency_us, latency_us / 1000.0);
        TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(limit_us, latency_us,
                                          "Startup latency exceeds 3s limit");

        vTaskDelay(pdMS_TO_TICKS(500));
        sc_stop_and_wait(&ctx);
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    ESP_LOGI(TAG, "[StartupLatency] AVG: %.2f ms  MAX: %.2f ms",
             sum / N / 1000.0, max_us / 1000.0);

    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[perf]:test_player_seek_latency", "[player][scenario]")
{
    char m4a[TEST_PATH_MAX_LEN] = {0};
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", m4a, sizeof(m4a))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));
    esp_player_set_url(ctx.player, m4a);
    TEST_ASSERT_TRUE(sc_run_and_wait_played(&ctx));

    uint64_t dur = 0;
    esp_player_get_duration(ctx.player, &dur);
    if (dur == 0) {
        dur = 30000;
    }

    const int N = 5;
    int64_t sum = 0;
    int64_t max_us = 0;
    const int64_t limit_us = 2000000LL;

    for (int i = 0; i < N; i++) {
        sc_clear_bits(&ctx, SC_SEEK_DONE_BIT);
        uint64_t target_ms = (uint32_t)((int64_t)dur * (i + 1) / (N + 1));

        ctx.seek_time_us = esp_timer_get_time();
        ctx.seek_done_time_us = 0;
        esp_player_seek(ctx.player, target_ms);

        bool ok = sc_wait_bits(&ctx, SC_SEEK_DONE_BIT, SC_TIMEOUT_SEEK_MS);
        TEST_ASSERT_TRUE_MESSAGE(ok, "Seek: no SEEK_DONE event");

        int64_t latency_us = ctx.seek_done_time_us - ctx.seek_time_us;
        if (latency_us < 0) {
            latency_us = 0;
        }
        sum += latency_us;
        if (latency_us > max_us) {
            max_us = latency_us;
        }

        ESP_LOGI(TAG, "[SeekLatency] Seek to %" PRIu64 " ms: %" PRId64 " µs (%.2f ms)",
                 target_ms, latency_us, latency_us / 1000.0);
        TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(limit_us, latency_us,
                                          "Seek latency exceeds 2s limit");
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "[SeekLatency] AVG: %.2f ms  MAX: %.2f ms",
             sum / N / 1000.0, max_us / 1000.0);

    sc_stop_and_wait(&ctx);
    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[perf]:test_player_memory_stability", "[player][scenario]")
{
    char m4a[TEST_PATH_MAX_LEN] = {0};
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", m4a, sizeof(m4a))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }

    {
        sc_ctx_t ctx = {0};
        sc_create_audio_player(&ctx, 0, 44100, 16, 2);
        esp_player_set_url(ctx.player, m4a);
        sc_run_and_wait_played(&ctx);
        sc_stop_and_wait(&ctx);
        sc_destroy_player_and_render(&ctx);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    size_t heap_baseline = esp_get_free_heap_size();
    ESP_LOGI(TAG, "[MemStability] Baseline heap: %zu bytes", heap_baseline);

    const int ROUNDS = 20;
    for (int i = 0; i < ROUNDS; i++) {
        sc_ctx_t ctx = {0};
        TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                          sc_create_audio_player(&ctx, 0, 44100, 16, 2));
        esp_player_set_url(ctx.player, m4a);
        sc_run_and_wait_played(&ctx);
        vTaskDelay(pdMS_TO_TICKS(500));
        sc_stop_and_wait(&ctx);
        sc_destroy_player_and_render(&ctx);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    size_t heap_after = esp_get_free_heap_size();
    int32_t leaked = (int32_t)heap_baseline - (int32_t)heap_after;
    ESP_LOGI(TAG, "[MemStability] After %d rounds: heap = %zu, delta = %" PRId32 " bytes",
             ROUNDS, heap_after, leaked);

    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(2048, leaked,
                                      "Memory leak detected after 20 play/stop cycles");
}

TEST_CASE("[perf]:test_player_high_freq_run_stop_stress", "[player][scenario]")
{
    char m4a[TEST_PATH_MAX_LEN] = {0};
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", m4a, sizeof(m4a))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));
    esp_player_set_url(ctx.player, m4a);

    const int ROUNDS = 50;
    int ok_cnt = 0;

    for (int i = 0; i < ROUNDS; i++) {
        sc_clear_bits(&ctx, SC_PLAYED_BIT | SC_STOPPED_BIT |
                                SC_FINISHED_BIT | SC_ERROR_BIT);
        esp_player_run(ctx.player);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_player_stop(ctx.player);
        bool got_stop = sc_wait_bits(&ctx, SC_STOPPED_BIT | SC_FINISHED_BIT,
                                     SC_TIMEOUT_STOP_MS);
        if (got_stop) {
            ok_cnt++;
        }
    }

    ESP_LOGI(TAG, "[HighFreq] %d/%d runs produced stop event", ok_cnt, ROUNDS);
    TEST_ASSERT_GREATER_OR_EQUAL((int)(ROUNDS * 0.8), ok_cnt);

    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[multithread]:test_player_concurrent_seek", "[player][scenario]")
{
    char m4a[TEST_PATH_MAX_LEN] = {0};
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", m4a, sizeof(m4a))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));
    esp_player_set_url(ctx.player, m4a);
    TEST_ASSERT_TRUE(sc_run_and_wait_played(&ctx));

    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    EventGroupHandle_t done_group = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL(mutex);
    TEST_ASSERT_NOT_NULL(done_group);

    int ok_count = 0;
    const int TASKS = 4;
    const int ROUNDS_PER_TASK = 10;

    sc_mt_seek_arg_t args[4];
    EventBits_t all_done = 0;
    int priorities[] = {4, 5, 6, 7};

    for (int i = 0; i < TASKS; i++) {
        args[i].player = ctx.player;
        args[i].rounds = ROUNDS_PER_TASK;
        args[i].ok_count = &ok_count;
        args[i].counter_mutex = mutex;
        args[i].done_group = done_group;
        args[i].done_bit = (1 << i);
        args[i].task_priority = priorities[i];
        all_done |= (1 << i);

        xTaskCreate(sc_concurrent_seek_task, "sc_seek_task",
                    4096, &args[i], priorities[i], NULL);
    }

    xEventGroupWaitBits(done_group, all_done, pdTRUE, pdTRUE, pdMS_TO_TICKS(60000));

    ESP_LOGI(TAG, "[ConcurrentSeek] Total successful seeks: %d / %d",
             ok_count, TASKS * ROUNDS_PER_TASK);
    TEST_ASSERT_GREATER_THAN(0, ok_count);

    sc_stop_and_wait(&ctx);

    vSemaphoreDelete(mutex);
    vEventGroupDelete(done_group);
    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[multithread]:test_player_stop_run_race", "[player][scenario]")
{
    char m4a[TEST_PATH_MAX_LEN] = {0};
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", m4a, sizeof(m4a))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_set_url(ctx.player, m4a));

    EventGroupHandle_t done_group = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL(done_group);

    const int TASKS = 4;
    const int ROUNDS_PER_TASK = 25;
    sc_mt_stop_run_arg_t args[4];
    EventBits_t all_done = 0;
    int priorities[] = {5, 6, 7, 8};

    for (int i = 0; i < TASKS; i++) {
        args[i].player = ctx.player;
        args[i].role = i % 2;
        args[i].rounds = ROUNDS_PER_TASK;
        args[i].done_group = done_group;
        args[i].done_bit = (1 << i);
        args[i].task_priority = priorities[i];
        all_done |= (1 << i);

        xTaskCreate(sc_stop_run_race_task, "sc_stop_run",
                    4096, &args[i], priorities[i], NULL);
    }

    EventBits_t got = xEventGroupWaitBits(done_group, all_done, pdTRUE, pdTRUE,
                                          pdMS_TO_TICKS(60000));
    TEST_ASSERT_EQUAL_MESSAGE(all_done, got & all_done,
                              "stop/run race tasks did not all finish");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_player_stop(ctx.player);
    (void)sc_wait_bits(&ctx, SC_STOPPED_BIT | SC_FINISHED_BIT, SC_TIMEOUT_STOP_MS);
    sc_clear_bits(&ctx, SC_ERROR_BIT | SC_PLAYED_BIT | SC_STOPPED_BIT);
    TEST_ASSERT_TRUE_MESSAGE(sc_run_and_wait_played(&ctx),
                             "Player should recover after stop/run race");
    TEST_ASSERT_EQUAL_MESSAGE(0, xEventGroupGetBits(ctx.event_group) & SC_ERROR_BIT,
                              "Unexpected ERROR after stop/run race recovery");

    TEST_ASSERT_TRUE(sc_stop_and_wait(&ctx));

    vEventGroupDelete(done_group);
    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[multithread]:test_player_priority_high_low_contention", "[player][scenario]")
{
    char m4a[TEST_PATH_MAX_LEN] = {0};
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", m4a, sizeof(m4a))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));
    esp_player_set_url(ctx.player, m4a);
    TEST_ASSERT_TRUE(sc_run_and_wait_played(&ctx));

    volatile bool running = true;
    EventGroupHandle_t done = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL(done);

    sc_priority_arg_t disruptor_arg = {
        .player = ctx.player,
        .running_flag = &running,
        .done_group = done,
        .done_bit = (1 << 0),
    };

    xTaskCreate(sc_high_priority_disruptor, "sc_disruptor",
                4096, &disruptor_arg, 10, NULL);

    vTaskDelay(pdMS_TO_TICKS(15000));
    running = false;

    xEventGroupWaitBits(done, (1 << 0), pdTRUE, pdTRUE, pdMS_TO_TICKS(5000));
    vEventGroupDelete(done);

    esp_player_stop(ctx.player);
    sc_wait_bits(&ctx, SC_STOPPED_BIT | SC_FINISHED_BIT, SC_TIMEOUT_STOP_MS);

    sc_destroy_player_and_render(&ctx);
    ESP_LOGI(TAG, "[PriorityTest] Completed without crash");
}

TEST_CASE("[recovery]:test_player_errors_then_valid_url", "[player][scenario]")
{
    char error_file[TEST_PATH_MAX_LEN] = {0};
    char valid_file[TEST_PATH_MAX_LEN] = {0};

    if (!sc_get_first_file(TEST_FILE_ERROR_PATH, NULL, error_file, sizeof(error_file))) {
        TEST_IGNORE_MESSAGE("No error file found, skip");
    }
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", valid_file, sizeof(valid_file))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));

    const int ERROR_ROUNDS = 3;
    for (int i = 0; i < ERROR_ROUNDS; i++) {
        ESP_LOGI(TAG, "[Recovery] Error round %d/%d: %s",
                 i + 1, ERROR_ROUNDS, error_file);
        sc_clear_bits(&ctx, SC_ERROR_BIT | SC_FINISHED_BIT);
        TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                          esp_player_set_url(ctx.player, error_file));
        esp_player_run(ctx.player);
        bool got_err = sc_wait_bits(&ctx, SC_ERROR_BIT | SC_FINISHED_BIT,
                                    SC_TIMEOUT_PLAY_MS);
        ESP_LOGI(TAG, "[Recovery] Round %d got error/finish: %s",
                 i + 1, got_err ? "YES" : "NO");
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    ESP_LOGI(TAG, "[Recovery] Switch to valid file: %s", valid_file);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      esp_player_set_url(ctx.player, valid_file));
    TEST_ASSERT_TRUE_MESSAGE(sc_run_and_wait_played(&ctx),
                             "Recovery: valid file should play after errors");

    uint64_t dur = 0;
    esp_player_get_duration(ctx.player, &dur);
    ESP_LOGI(TAG, "[Recovery] Valid file duration: %" PRIu64 " ms", dur);
    TEST_ASSERT_GREATER_THAN(0, dur);

    vTaskDelay(pdMS_TO_TICKS(2000));
    sc_stop_and_wait(&ctx);

    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[recovery]:test_player_after_full_error_stop_then_run", "[player][scenario]")
{
    char error_file[TEST_PATH_MAX_LEN] = {0};
    char valid_file[TEST_PATH_MAX_LEN] = {0};

    if (!sc_get_first_file(TEST_FILE_ERROR_PATH, NULL, error_file, sizeof(error_file))) {
        TEST_IGNORE_MESSAGE("No error file found, skip");
    }
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", valid_file, sizeof(valid_file))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));

    sc_trigger_full_error(&ctx, error_file);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_PLAYER_ERR_OK, esp_player_stop(ctx.player),
                              "stop() should be safe after full ERROR");

    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_set_url(ctx.player, valid_file));
    TEST_ASSERT_TRUE_MESSAGE(sc_run_and_wait_played(&ctx),
                             "run() should recover with a valid URL after full ERROR");

    sc_stop_and_wait(&ctx);
    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[recovery]:test_player_after_full_error_api_matrix", "[player][scenario]")
{
    char error_file[TEST_PATH_MAX_LEN] = {0};
    char valid_file[TEST_PATH_MAX_LEN] = {0};

    if (!sc_get_first_file(TEST_FILE_ERROR_PATH, NULL, error_file, sizeof(error_file))) {
        TEST_IGNORE_MESSAGE("No error file found, skip");
    }
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", valid_file, sizeof(valid_file))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));

    sc_trigger_full_error(&ctx, error_file);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_PLAYER_ERR_OK, esp_player_stop(ctx.player),
                              "stop() should be idempotent after full ERROR");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_PLAYER_ERR_INVALID_STATE, esp_player_pause(ctx.player),
                              "pause() should be rejected after full ERROR auto-recovers to IDLE");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_PLAYER_ERR_INVALID_STATE, esp_player_resume(ctx.player),
                              "resume() should be rejected after full ERROR auto-recovers to IDLE");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_PLAYER_ERR_INVALID_STATE, esp_player_seek(ctx.player, 0),
                              "seek() should be rejected after full ERROR auto-recovers to IDLE");

    sc_clear_bits(&ctx, SC_ERROR_BIT | SC_PLAYED_BIT | SC_FINISHED_BIT);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_PLAYER_ERR_OK, esp_player_run(ctx.player),
                              "run() should be accepted after full ERROR");
    EventBits_t bits = xEventGroupWaitBits(ctx.event_group,
                                           SC_ERROR_BIT | SC_PLAYED_BIT,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(SC_TIMEOUT_PLAY_MS));
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bits & SC_ERROR_BIT,
                                  "Re-running the same bad URL should report ERROR again");

    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_set_url(ctx.player, valid_file));
    TEST_ASSERT_TRUE_MESSAGE(sc_run_and_wait_played(&ctx),
                             "Player should still recover after repeated full ERROR");

    sc_stop_and_wait(&ctx);
    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[recovery]:test_player_after_error_stop_recover_audio", "[player][scenario]")
{
    char error_file[TEST_PATH_MAX_LEN] = {0};
    char valid_file[TEST_PATH_MAX_LEN] = {0};

    if (!sc_get_first_file(TEST_FILE_ERROR_PATH, NULL, error_file, sizeof(error_file))) {
        TEST_IGNORE_MESSAGE("No error file found, skip");
    }
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", valid_file, sizeof(valid_file))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));

    sc_trigger_full_error(&ctx, error_file);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_PLAYER_ERR_OK, esp_player_stop(ctx.player),
                              "stop() should be safe after ERROR");

    sc_recover_with_valid_audio_and_wait_playing(&ctx, valid_file);

    sc_stop_and_wait(&ctx);
    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[recovery]:test_player_after_error_recover_pause_seek_resume", "[player][scenario]")
{
    char error_file[TEST_PATH_MAX_LEN] = {0};
    char valid_file[TEST_PATH_MAX_LEN] = {0};

    if (!sc_get_first_file(TEST_FILE_ERROR_PATH, NULL, error_file, sizeof(error_file))) {
        TEST_IGNORE_MESSAGE("No error file found, skip");
    }
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", valid_file, sizeof(valid_file))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));

    sc_trigger_full_error(&ctx, error_file);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_PLAYER_ERR_INVALID_STATE, esp_player_pause(ctx.player),
                              "pause() should be rejected before recovery from ERROR");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_PLAYER_ERR_INVALID_STATE, esp_player_seek(ctx.player, 0),
                              "seek() should be rejected before recovery from ERROR");

    sc_recover_with_valid_audio_and_wait_playing(&ctx, valid_file);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_PLAYER_ERR_OK, esp_player_pause(ctx.player),
                              "pause() should work after recovery playback starts");
    TEST_ASSERT_TRUE_MESSAGE(sc_wait_bits(&ctx, SC_PAUSED_BIT, SC_TIMEOUT_PLAY_MS),
                             "Expected PAUSED after recovery pause");

    TEST_ASSERT_EQUAL_MESSAGE(ESP_PLAYER_ERR_OK, esp_player_seek(ctx.player, 0),
                              "seek() should work while paused after recovery");
    TEST_ASSERT_TRUE_MESSAGE(sc_wait_bits(&ctx, SC_SEEK_DONE_BIT, SC_TIMEOUT_SEEK_MS),
                             "Expected SEEK_DONE after recovery paused seek");

    TEST_ASSERT_EQUAL_MESSAGE(ESP_PLAYER_ERR_OK, esp_player_resume(ctx.player),
                              "resume() should work after recovery paused seek");
    TEST_ASSERT_TRUE_MESSAGE(sc_wait_bits(&ctx, SC_PLAYED_BIT, SC_TIMEOUT_PLAY_MS),
                             "Expected PLAYED after recovery resume");

    sc_stop_and_wait(&ctx);
    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[edge]:test_player_seek_while_paused", "[player][scenario]")
{
    char m4a[TEST_PATH_MAX_LEN] = {0};
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", m4a, sizeof(m4a))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));
    esp_player_set_url(ctx.player, m4a);
    TEST_ASSERT_TRUE(sc_run_and_wait_played(&ctx));

    uint64_t dur = 0;
    esp_player_get_duration(ctx.player, &dur);

    vTaskDelay(pdMS_TO_TICKS(500));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_pause(ctx.player));
    TEST_ASSERT_TRUE(sc_wait_bits(&ctx, SC_PAUSED_BIT, SC_TIMEOUT_PLAY_MS));

    uint64_t paused_pos = 0;
    esp_player_get_play_time(ctx.player, &paused_pos);
    ESP_LOGI(TAG, "[SeekWhilePaused] Paused at %" PRIu64 " ms", paused_pos);

    uint64_t seek_target = dur / 2;
    ESP_LOGI(TAG, "[SeekWhilePaused] Seek to %" PRIu64 " ms (50%%)", seek_target);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      esp_player_seek(ctx.player, seek_target));
    TEST_ASSERT_TRUE(sc_wait_bits(&ctx, SC_SEEK_DONE_BIT, SC_TIMEOUT_SEEK_MS));

    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_resume(ctx.player));
    TEST_ASSERT_TRUE(sc_wait_bits(&ctx, SC_PLAYED_BIT, SC_TIMEOUT_PLAY_MS));

    vTaskDelay(pdMS_TO_TICKS(500));
    uint64_t resumed_pos = 0;
    esp_player_get_play_time(ctx.player, &resumed_pos);
    ESP_LOGI(TAG, "[SeekWhilePaused] Resumed at %" PRIu64 " ms", resumed_pos);

    TEST_ASSERT_GREATER_OR_EQUAL(paused_pos, resumed_pos);

    vTaskDelay(pdMS_TO_TICKS(2000));
    sc_stop_and_wait(&ctx);
    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[edge]:test_player_seek_boundaries", "[player][scenario]")
{
    char m4a[TEST_PATH_MAX_LEN] = {0};
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", m4a, sizeof(m4a))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_set_url(ctx.player, m4a));

    TEST_ASSERT_EQUAL_MESSAGE(ESP_PLAYER_ERR_INVALID_STATE, esp_player_seek(ctx.player, 0),
                              "seek in IDLE should return INVALID_STATE");

    TEST_ASSERT_TRUE(sc_run_and_wait_played(&ctx));

    uint64_t dur = 0;
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_get_duration(ctx.player, &dur));
    TEST_ASSERT_GREATER_THAN(0, dur);

    uint64_t mid = dur / 2;
    ESP_LOGI(TAG, "[SeekBoundary] duration=%" PRIu64 " ms", dur);

    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_seek(ctx.player, 0));
    TEST_ASSERT_TRUE(sc_wait_bits(&ctx, SC_SEEK_DONE_BIT, SC_TIMEOUT_SEEK_MS));

    sc_clear_bits(&ctx, SC_SEEK_DONE_BIT);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_seek(ctx.player, mid));
    TEST_ASSERT_TRUE(sc_wait_bits(&ctx, SC_SEEK_DONE_BIT, SC_TIMEOUT_SEEK_MS));

    sc_clear_bits(&ctx, SC_SEEK_DONE_BIT);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_seek(ctx.player, dur));
    TEST_ASSERT_TRUE(sc_wait_bits(&ctx, SC_SEEK_DONE_BIT, SC_TIMEOUT_SEEK_MS));

    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_pause(ctx.player));
    TEST_ASSERT_TRUE(sc_wait_bits(&ctx, SC_PAUSED_BIT, SC_TIMEOUT_PLAY_MS));

    sc_clear_bits(&ctx, SC_SEEK_DONE_BIT);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_seek(ctx.player, mid));
    TEST_ASSERT_TRUE(sc_wait_bits(&ctx, SC_SEEK_DONE_BIT, SC_TIMEOUT_SEEK_MS));

    TEST_ASSERT_TRUE(sc_stop_and_wait(&ctx));

    sc_clear_bits(&ctx, SC_SEEK_DONE_BIT | SC_PLAYED_BIT);
    uint64_t bookmark = dur / 3;
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_seek(ctx.player, bookmark));
    TEST_ASSERT_TRUE(sc_wait_bits(&ctx, SC_SEEK_DONE_BIT, SC_TIMEOUT_SEEK_MS));

    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_run(ctx.player));
    TEST_ASSERT_TRUE(sc_wait_bits(&ctx, SC_PLAYED_BIT, SC_TIMEOUT_PLAY_MS));
    vTaskDelay(pdMS_TO_TICKS(500));

    uint64_t play_time = 0;
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_get_play_time(ctx.player, &play_time));
    uint64_t expected = bookmark + 500;
    ESP_LOGI(TAG, "[SeekBoundary] STOPPED bookmark=%" PRIu64 " expected~%" PRIu64 " play_time=%" PRIu64,
             bookmark, expected, play_time);
    TEST_ASSERT_GREATER_OR_EQUAL(expected - 1000, play_time);
    TEST_ASSERT_LESS_OR_EQUAL(expected + 1000, play_time);

    TEST_ASSERT_TRUE(sc_stop_and_wait(&ctx));
    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[edge]:test_player_rapid_consecutive_seeks", "[player][scenario]")
{
    char m4a[TEST_PATH_MAX_LEN] = {0};
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", m4a, sizeof(m4a))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));
    esp_player_set_url(ctx.player, m4a);
    TEST_ASSERT_TRUE(sc_run_and_wait_played(&ctx));

    uint64_t dur = 0;
    esp_player_get_duration(ctx.player, &dur);

    const int SEEK_COUNT = 5;
    ESP_LOGI(TAG, "[RapidSeek] Firing %d seeks in quick succession", SEEK_COUNT);
    for (int i = 0; i < SEEK_COUNT; i++) {
        uint64_t pos = (uint32_t)((int64_t)dur * (esp_random() % 80 + 5) / 100);
        esp_player_err_t ret = esp_player_seek(ctx.player, pos);
        ESP_LOGI(TAG, "[RapidSeek] seek[%d] to %" PRIu64 " ms -> ret=%d", i, pos, ret);
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    sc_wait_bits(&ctx, SC_SEEK_DONE_BIT, SC_TIMEOUT_SEEK_MS);
    vTaskDelay(pdMS_TO_TICKS(500));

    uint64_t final_pos = 0;
    esp_player_get_play_time(ctx.player, &final_pos);
    ESP_LOGI(TAG, "[RapidSeek] Final position: %" PRIu64 " ms", final_pos);

    sc_stop_and_wait(&ctx);
    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[edge]:test_player_seek_near_end_auto_finish", "[player][scenario]")
{
    char m4a[TEST_PATH_MAX_LEN] = {0};
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", m4a, sizeof(m4a))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));
    esp_player_set_url(ctx.player, m4a);
    TEST_ASSERT_TRUE(sc_run_and_wait_played(&ctx));

    uint64_t dur = 0;
    esp_player_get_duration(ctx.player, &dur);

    uint64_t near_end = dur * 95 / 100;
    ESP_LOGI(TAG, "[SeekNearEnd] Seek to %" PRIu64 " ms (95%% of %" PRIu64 " ms)",
             near_end, dur);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      esp_player_seek(ctx.player, near_end));
    TEST_ASSERT_TRUE(sc_wait_bits(&ctx, SC_SEEK_DONE_BIT, SC_TIMEOUT_SEEK_MS));

    bool finished = sc_wait_bits(&ctx, SC_FINISHED_BIT, 15000);
    ESP_LOGI(TAG, "[SeekNearEnd] Auto-finish: %s", finished ? "YES" : "TIMEOUT");
    TEST_ASSERT_TRUE_MESSAGE(finished,
                             "Expected FINISHED event after seek to near end");

    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[frame_mode]:test_player_continuous_frame_push_stress", "[player][scenario]")
{
    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 22050, 16, 1));

    esp_player_set_url(ctx.player, TEST_FILL_URL_AAC);
    esp_player_run(ctx.player);

    const int FRAME_COUNT = 50;
    const uint64_t FRAME_DURATION_MS = 23;  /* ~1024 samples @ 44.1 kHz AAC */
    int push_ok = 0;
    ESP_LOGI(TAG, "[FrameStress] Pushing %d AAC frames continuously...", FRAME_COUNT);

    for (int i = 0; i < FRAME_COUNT; i++) {
        const uint8_t *frame_data = (i % 2 == 0)
                                        ? test_data_aac_frame1
                                        : test_data_aac_frame2;
        size_t frame_len = (i % 2 == 0)
                               ? TEST_DATA_AAC_FRAME1_COUNT
                               : TEST_DATA_AAC_FRAME2_COUNT;
        esp_player_frame_t frame = {
            .data = (uint8_t *)frame_data,
            .data_len = frame_len,
            .pts = (uint64_t)i * FRAME_DURATION_MS,
            .frame_type = ESP_PLAYER_FRAME_TYPE_DEFAULT,
            .is_bad = false,
            .eos = false,
        };
        esp_player_err_t ret = esp_player_submit_frame(ctx.player, &frame, 2000);
        if (ret == ESP_PLAYER_ERR_OK) {
            push_ok++;
        }
    }

    esp_player_frame_t eos_frame = {
        .data = (uint8_t *)test_data_aac_frame1,
        .data_len = TEST_DATA_AAC_FRAME1_COUNT,
        .pts = (uint64_t)FRAME_COUNT * FRAME_DURATION_MS,
        .frame_type = ESP_PLAYER_FRAME_TYPE_DEFAULT,
        .is_bad = false,
        .eos = true,
    };
    esp_player_submit_frame(ctx.player, &eos_frame, 2000);

    ESP_LOGI(TAG, "[FrameStress] Pushed %d/%d frames successfully", push_ok, FRAME_COUNT);
    TEST_ASSERT_GREATER_OR_EQUAL((int)(FRAME_COUNT * 0.9), push_ok);

    bool done = sc_wait_bits(&ctx, SC_FINISHED_BIT | SC_ERROR_BIT, 5000);
    ESP_LOGI(TAG, "[FrameStress] Playback ended: %s", done ? "YES" : "TIMEOUT");

    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[frame_mode]:test_player_fill_vs_block_mode", "[player][scenario]")
{
    const char *modes[] = {"FILL", "BLOCK"};
    const char *mode_urls[] = {TEST_FILL_URL_AAC, TEST_BLOCK_URL_AAC};

    for (int m = 0; m < 2; m++) {
        ESP_LOGI(TAG, "[FillVsBlock] Testing mode: %s", modes[m]);

        sc_ctx_t ctx = {0};
        TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                          sc_create_audio_player(&ctx, 0, 22050, 16, 1));

        esp_player_set_url(ctx.player, mode_urls[m]);
        esp_player_run(ctx.player);

        if (m == 1) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        esp_player_frame_t frame = {
            .data = (uint8_t *)test_data_aac_frame1,
            .data_len = TEST_DATA_AAC_FRAME1_COUNT,
            .pts = 0,
            .frame_type = ESP_PLAYER_FRAME_TYPE_DEFAULT,
            .is_bad = false,
            .eos = false,
        };
        uint32_t timeout = (m == 0) ? 1000 : 0;
        esp_player_submit_frame(ctx.player, &frame, timeout);

        frame.eos = true;
        esp_player_submit_frame(ctx.player, &frame, timeout);

        bool done = sc_wait_bits(&ctx, SC_FINISHED_BIT | SC_ERROR_BIT, 5000);
        ESP_LOGI(TAG, "[FillVsBlock] Mode %s ended: %s", modes[m], done ? "YES" : "TIMEOUT");

        vTaskDelay(pdMS_TO_TICKS(500));
        sc_destroy_player_and_render(&ctx);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

TEST_CASE("[frame_mode]:test_player_frame_pts_tracking", "[player][scenario]")
{
    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 22050, 16, 1));

    esp_player_set_url(ctx.player, TEST_FILL_URL_AAC);
    esp_player_run(ctx.player);

    const uint64_t FRAME_DURATION_MS = 46;  /* ~2048 samples @ 44.1 kHz AAC */
    const int TOTAL_FRAMES = 20;

    for (int i = 0; i < TOTAL_FRAMES; i++) {
        const uint8_t *frame_data = (i % 2 == 0)
                                        ? test_data_aac_frame1
                                        : test_data_aac_frame2;
        size_t frame_len = (i % 2 == 0)
                               ? TEST_DATA_AAC_FRAME1_COUNT
                               : TEST_DATA_AAC_FRAME2_COUNT;
        esp_player_frame_t frame = {
            .data = (uint8_t *)frame_data,
            .data_len = frame_len,
            .pts = (uint64_t)i * FRAME_DURATION_MS,
            .frame_type = ESP_PLAYER_FRAME_TYPE_DEFAULT,
            .is_bad = false,
            .eos = (i == TOTAL_FRAMES - 1),
        };
        esp_player_err_t ret = esp_player_submit_frame(ctx.player, &frame, 3000);
        ESP_LOGD(TAG, "[PtsTracking] Frame %d PTS=%" PRIu64 " ms ret=%d",
                 i, frame.pts, ret);
    }

    bool done = sc_wait_bits(&ctx, SC_FINISHED_BIT | SC_ERROR_BIT, 5000);
    uint64_t final_time = 0;
    esp_player_get_play_time(ctx.player, &final_time);

    uint64_t expected_ms = (uint64_t)(TOTAL_FRAMES - 1) * FRAME_DURATION_MS;
    ESP_LOGI(TAG, "[PtsTracking] Expected: ~%" PRIu64 "ms  Actual: %" PRIu64 "ms  Done: %s",
             expected_ms, final_time, done ? "YES" : "TIMEOUT");

    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[event]:test_player_event_queue_delivery", "[player][scenario]")
{
    char m4a[TEST_PATH_MAX_LEN] = {0};
    if (!sc_get_first_file(TEST_FILE_AUDIO_PATH, ".m4a", m4a, sizeof(m4a))) {
        TEST_IGNORE_MESSAGE("No .m4a file found, skip");
    }

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_set_event_cb(ctx.player, NULL, NULL));

    QueueHandle_t queue = xQueueCreate(10, sizeof(esp_player_event_msg_t));
    TEST_ASSERT_NOT_NULL(queue);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_set_event_queue(ctx.player, queue));

    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_set_url(ctx.player, m4a));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_run(ctx.player));
    TEST_ASSERT_TRUE_MESSAGE(sc_wait_queue_event(queue, ESP_PLAYER_EVENT_PLAYED, SC_TIMEOUT_PLAY_MS),
                             "Expected PLAYED via event queue");

    vTaskDelay(pdMS_TO_TICKS(500));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_stop(ctx.player));
    TEST_ASSERT_TRUE_MESSAGE(sc_wait_queue_event(queue, ESP_PLAYER_EVENT_STOPPED, SC_TIMEOUT_STOP_MS),
                             "Expected STOPPED via event queue");

    esp_player_set_event_queue(ctx.player, NULL);
    vQueueDelete(queue);
    sc_destroy_player_and_render(&ctx);
}

TEST_CASE("[buffering]:test_player_rebuffer_event_pair_http", "[player][buffering][leaks=20000]")
{
    esp_gmf_app_test_case_uses_tcpip();
    esp_gmf_app_wifi_connect();

    sc_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      sc_create_audio_player(&ctx, 0, 44100, 16, 2));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      esp_player_set_url(ctx.player, TEST_HTTP_URL));
    TEST_ASSERT_TRUE_MESSAGE(sc_run_and_wait_played(&ctx),
                             "HTTP stream failed to reach PLAYED");

    EventBits_t bits = xEventGroupGetBits(ctx.event_group);
    TEST_ASSERT_EQUAL_MESSAGE(0, bits & SC_ERROR_BIT,
                              "Unexpected ERROR during HTTP startup");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bits & SC_BUFFERING_BIT,
                                  "Expected PRE_BUFFERING (ESP_PLAYER_EVENT_BUFFERING) on HTTP startup");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bits & SC_BUFFERED_BIT,
                                  "Expected ESP_PLAYER_EVENT_BUFFERED after PRE_BUFFERING completes");

    /* Play for a short while to confirm the stream stays healthy. */
    vTaskDelay(pdMS_TO_TICKS(3000));
    TEST_ASSERT_EQUAL_MESSAGE(0, xEventGroupGetBits(ctx.event_group) & SC_ERROR_BIT,
                              "ERROR reported during steady HTTP playback");

    sc_stop_and_wait(&ctx);
    sc_destroy_player_and_render(&ctx);

    esp_gmf_app_wifi_disconnect();
}

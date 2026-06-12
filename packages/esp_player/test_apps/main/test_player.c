/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <dirent.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"

#include "unity.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_audio_render.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_enc_reg.h"
#include "esp_video_codec_types.h"
#include "esp_gmf_app_sys.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_app_unit_test.h"
#include "esp_gmf_audio_param.h"
#include "esp_gmf_element.h"
#include "esp_gmf_sonic.h"

#include "esp_player.h"
#include "esp_player_advance.h"
#include "render_common.h"
#include "test_data.h"

#define PLAYED_BIT     (1 << 0)
#define PAUSED_BIT     (1 << 1)
#define STOPPED_BIT    (1 << 2)
#define SEEK_DONE_BIT  (1 << 3)
#define FINISHED_BIT   (1 << 4)
#define ERROR_BIT      (1 << 5)

#define EVENT_WAIT_TIMEOUT_MS     30000U
#define EVENT_WAIT_FINISHED_MS    120000U
#define MIN_SEEK_TEST_FILE_BYTES  4096U
#define SEEK_STABILIZE_DELAY_MS   5U
#define SEEK_TIME_DIFF_MS         1500
#define TASK_DELAY_MS             1000
#define MAX_STREAM_NUM            4

#define STALL_SERVER_PORT  8123
#define STALL_SERVER_URL   "http://127.0.0.1:8123/stall.m4a"

#define SET_EVENT_BIT(g_player_event_group, bit, bit_name)  do {  \
    (void)bit_name;                                               \
    if (g_player_event_group) {                                   \
        xEventGroupSetBits(g_player_event_group, bit);            \
    }                                                             \
} while (0)

#define HANDLE_PLAYER_EVENT_WITH_BIT(test_ctx, log_msg, bit, bit_name)  do {         \
    ESP_LOGW(TAG, log_msg);                                                          \
    SET_EVENT_BIT(g_player_event_group[test_ctx->stream_num_index], bit, bit_name);  \
} while (0)

typedef struct {
    int         sample_rate;
    int         bits_per_sample;
    int         channels;
    const char *url;
    uint32_t    av_mask;
    int         video_width;
    int         video_height;
    int         pixel_format;
    int         video_fps;
    uint8_t     stream_num_index;
} test_config_t;

typedef struct {
    esp_player_handle_t  player;
    esp_player_config_t  config;
    void                *audio_stream_handle;
    void                *video_stream_handle;
    uint8_t              stream_num_index;
    const char          *current_url;
} test_context_t;

typedef struct {
    int                listen_fd;
    volatile bool      running;
    SemaphoreHandle_t  done;
} stall_server_t;

typedef enum {
    PLAY_SEQUENCE_INIT,
    PLAY_SEQUENCE_RUN,
    PLAY_SEQUENCE_PAUSE,
    PLAY_SEQUENCE_STOP,
    PLAY_SEQUENCE_RESUME,
    PLAY_SEQUENCE_SEEK,
} play_sequence_t;

static const char *TAG = "TEST_PLAYER";

static esp_audio_render_proc_type_t g_proc_type[MAX_STREAM_NUM][5] = {0};
static uint8_t g_proc_type_index[MAX_STREAM_NUM]                   = {0};

static EventGroupHandle_t g_player_event_group[MAX_STREAM_NUM] = {NULL, NULL, NULL, NULL};
static int output_sample_rate = 44100;
static int output_bits_per_sample = 16;
static int output_channels = 2;

static int error_num = 0;

static void stall_server_task(void *arg)
{
    stall_server_t *srv = (stall_server_t *)arg;
    int client_fd = -1;
    while (srv->running) {
        int fd = accept(srv->listen_fd, NULL, NULL);
        if (fd < 0) {
            break;  /* listen_fd was shut down on teardown */
        }
        client_fd = fd;
        while (srv->running) {  /* hold the connection open, never reply */
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        close(client_fd);
        client_fd = -1;
    }
    if (client_fd >= 0) {
        close(client_fd);
    }
    close(srv->listen_fd);
    srv->listen_fd = -1;
    xSemaphoreGive(srv->done);
    vTaskDelete(NULL);
}

static esp_err_t stall_server_start(stall_server_t *srv)
{
    memset(srv, 0, sizeof(*srv));
    srv->listen_fd = -1;
    srv->done = xSemaphoreCreateBinary();
    if (srv->done == NULL) {
        return ESP_FAIL;
    }
    srv->listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv->listen_fd < 0) {
        vSemaphoreDelete(srv->done);
        return ESP_FAIL;
    }
    int opt = 1;
    setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(STALL_SERVER_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0
        || listen(srv->listen_fd, 1) != 0) {
        close(srv->listen_fd);
        srv->listen_fd = -1;
        vSemaphoreDelete(srv->done);
        return ESP_FAIL;
    }
    srv->running = true;
    if (xTaskCreate(stall_server_task, "stall_srv", 4096, srv, 5, NULL) != pdPASS) {
        srv->running = false;
        close(srv->listen_fd);
        srv->listen_fd = -1;
        vSemaphoreDelete(srv->done);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void stall_server_stop(stall_server_t *srv)
{
    srv->running = false;
    if (srv->listen_fd >= 0) {
        shutdown(srv->listen_fd, SHUT_RDWR);  /* wake a blocked accept() */
    }
    xSemaphoreTake(srv->done, portMAX_DELAY);
    vSemaphoreDelete(srv->done);
    vTaskDelay(pdMS_TO_TICKS(50));  /* let the idle task reclaim the deleted task's stack */
}

static EventBits_t wait_player_bits(uint8_t stream_idx, EventBits_t bits, uint32_t timeout_ms)
{
    if (g_player_event_group[stream_idx] == NULL) {
        TEST_FAIL_MESSAGE("g_player_event_group is NULL");
        return 0;
    }
    EventBits_t got = xEventGroupWaitBits(
        g_player_event_group[stream_idx],
        bits,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms));
    if ((got & bits) == 0) {
        ESP_LOGE(TAG, "wait_player_bits timeout: expect_any_of=0x%lx got=0x%lx",
                 (unsigned long)bits, (unsigned long)got);
    }
    return got;
}

static esp_player_err_t player_event_callback(esp_player_event_msg_t *event_msg, void *ctx)
{
    test_context_t *test_ctx = (test_context_t *)ctx;
    if (g_player_event_group[test_ctx->stream_num_index] == NULL) {
        ESP_LOGW(TAG, "g_player_event_group is NULL in callback, ignoring event %d", event_msg->event_type);
        return ESP_PLAYER_ERR_OK;
    }

    switch (event_msg->event_type) {
        case ESP_PLAYER_EVENT_PLAYED:
            HANDLE_PLAYER_EVENT_WITH_BIT(test_ctx, "Player started playing", PLAYED_BIT, "PLAYED_BIT");
            break;
        case ESP_PLAYER_EVENT_PAUSED:
            HANDLE_PLAYER_EVENT_WITH_BIT(test_ctx, "Player paused", PAUSED_BIT, "PAUSED_BIT");
            break;
        case ESP_PLAYER_EVENT_STOPPED:
            HANDLE_PLAYER_EVENT_WITH_BIT(test_ctx, "Player stopped", STOPPED_BIT, "STOPPED_BIT");
            break;
        case ESP_PLAYER_EVENT_SEEK_DONE:
            HANDLE_PLAYER_EVENT_WITH_BIT(test_ctx, "Seek operation completed", SEEK_DONE_BIT, "SEEK_DONE_BIT");
            break;
        case ESP_PLAYER_EVENT_FINISHED:
            ESP_LOGI(TAG, "PLAYER_EVENT_FINISHED received, setting FINISHED_BIT");
            HANDLE_PLAYER_EVENT_WITH_BIT(test_ctx, "Player finished", FINISHED_BIT, "FINISHED_BIT");
            ESP_LOGI(TAG, "FINISHED_BIT set successfully");
            break;
        case ESP_PLAYER_EVENT_BUFFERING:
            ESP_LOGW(TAG, "Player buffering - data insufficient");
            break;
        case ESP_PLAYER_EVENT_BUFFERED:
            ESP_LOGW(TAG, "Player buffered - data sufficient");
            break;
        case ESP_PLAYER_EVENT_ERROR:
            HANDLE_PLAYER_EVENT_WITH_BIT(test_ctx, "Player error occurred", ERROR_BIT, "ERROR_BIT");
            error_num++;
            break;
        case ESP_PLAYER_EVENT_TRACK_INFO_PARSED:
            ESP_LOGW(TAG, "Track info reported");
            break;
        case ESP_PLAYER_EVENT_AUDIO_INFO_PARSED:
            ESP_LOGW(TAG, "Audio info reported");
            break;
        case ESP_PLAYER_EVENT_VIDEO_INFO_PARSED:
            ESP_LOGW(TAG, "Video info reported");
            break;
        default:
            ESP_LOGW(TAG, "Unknown event: %d", event_msg->event_type);
            break;
    }

    return ESP_PLAYER_ERR_OK;
}

static esp_player_err_t create_audio_renderer(test_context_t *ctx, const test_config_t *cfg)
{
    if (cfg->av_mask == ESP_PLAYER_MASK_VIDEO) {
        return ESP_PLAYER_ERR_OK;
    }
    esp_player_err_t ret = audio_render_create_handle(
        &ctx->audio_stream_handle,
        cfg->sample_rate,
        cfg->bits_per_sample,
        cfg->channels,
        ESP_AUDIO_RENDER_STREAM_ID(cfg->stream_num_index));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);
    ctx->config.audio_render_hd = ctx->audio_stream_handle;
    if (g_proc_type_index[cfg->stream_num_index] != 0) {
        esp_audio_render_stream_add_proc(ctx->audio_stream_handle, &g_proc_type[cfg->stream_num_index][0], g_proc_type_index[cfg->stream_num_index]);
    }
    printf("create_audio_renderer: %d-%p\n", cfg->stream_num_index, ctx->audio_stream_handle);
    return ESP_PLAYER_ERR_OK;
}

static esp_player_err_t create_video_renderer(test_context_t *ctx, const test_config_t *cfg)
{
    if (cfg->av_mask == ESP_PLAYER_MASK_AUDIO) {
        return ESP_PLAYER_ERR_OK;
    }

    esp_player_err_t ret = video_render_create_handle(&ctx->video_stream_handle, cfg->pixel_format, cfg->video_fps);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);
    ctx->config.video_render_hd = ctx->video_stream_handle;
    return ret;
}

static esp_player_err_t init_player(test_context_t *ctx, test_config_t *cfg)
{
    if (g_player_event_group[cfg->stream_num_index] == NULL) {
        ESP_LOGI(TAG, "Creating event group...");
        g_player_event_group[cfg->stream_num_index] = xEventGroupCreate();
        if (!g_player_event_group[cfg->stream_num_index]) {
            ESP_LOGE(TAG, "Failed to create event group");
            return ESP_PLAYER_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "Event group created successfully: %p", g_player_event_group[cfg->stream_num_index]);
    } else {
        ESP_LOGI(TAG, "Event group already exists: %p", g_player_event_group[cfg->stream_num_index]);
    }

    esp_player_err_t ret = create_audio_renderer(ctx, cfg);
    if (ret != ESP_PLAYER_ERR_OK) {
        return ret;
    }
    ret = create_video_renderer(ctx, cfg);
    if (ret != ESP_PLAYER_ERR_OK) {
        return ret;
    }

    ret = esp_player_init(&ctx->config, &ctx->player);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    esp_player_set_event_cb(ctx->player, player_event_callback, ctx);

    if (cfg->url) {
        esp_player_set_url(ctx->player, cfg->url);
    }

    esp_player_set_av_mask(ctx->player, cfg->av_mask);

    return ESP_PLAYER_ERR_OK;
}

static void cleanup_player(test_context_t *ctx)
{

    if (ctx->player) {
        esp_player_set_event_cb(ctx->player, NULL, NULL);
        esp_player_deinit(ctx->player);
        ctx->player = NULL;
    }
    if (ctx->audio_stream_handle) {
        audio_render_destroy_handle();
        ctx->audio_stream_handle = NULL;
    }
    if (ctx->video_stream_handle) {
        video_render_destroy_handle();
        ctx->video_stream_handle = NULL;
    }
    if (g_player_event_group[ctx->stream_num_index]) {
        vEventGroupDelete(g_player_event_group[ctx->stream_num_index]);
        g_player_event_group[ctx->stream_num_index] = NULL;
    }
}

static void execute_play_sequence(test_context_t *ctx, const char *sequence)
{
    uint64_t duration = 0;
    uint64_t current_time = 0;
    uint64_t cur_start_time = 0;
    uint64_t cur_end_time = 0;
    uint32_t delay_time = 0;
    for (const char *p = sequence; *p; p++) {
        switch (*p) {
            case 'r':
                TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_run(ctx->player));
                EventBits_t bits_r = wait_player_bits(ctx->stream_num_index, PLAYED_BIT | ERROR_BIT, EVENT_WAIT_TIMEOUT_MS);
                TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bits_r & PLAYED_BIT, "run: timeout waiting PLAYED");
                TEST_ASSERT_EQUAL_MESSAGE(0, bits_r & ERROR_BIT, "run: unexpected ERROR event");
                esp_player_get_duration(ctx->player, &duration);
                duration = duration == 0 ? 10000 : duration;
                vTaskDelay(pdMS_TO_TICKS(duration / 10 > 1500 ? 1500 : duration / 10));
                break;
            case 'p':
                if (esp_player_pause(ctx->player) == ESP_PLAYER_ERR_OK) {
                    EventBits_t bits_p = wait_player_bits(ctx->stream_num_index, PAUSED_BIT | ERROR_BIT, EVENT_WAIT_TIMEOUT_MS);
                    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bits_p & PAUSED_BIT, "pause: timeout waiting PAUSED");
                    TEST_ASSERT_EQUAL_MESSAGE(0, bits_p & ERROR_BIT, "pause: unexpected ERROR event");
                    vTaskDelay(pdMS_TO_TICKS(1000 * 1));
                } else {
                    printf("pause skipped\n");
                }
                break;
            case 's':
                TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_stop(ctx->player));
                EventBits_t bits_s = wait_player_bits(ctx->stream_num_index, STOPPED_BIT | FINISHED_BIT | ERROR_BIT, EVENT_WAIT_TIMEOUT_MS);
                TEST_ASSERT_EQUAL_MESSAGE(0, bits_s & ERROR_BIT, "stop: unexpected ERROR event");
                TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bits_s & (STOPPED_BIT | FINISHED_BIT), "stop: timeout waiting STOPPED/FINISHED");
                vTaskDelay(pdMS_TO_TICKS(1000 * 1));
                break;
            case 'e':
                if (esp_player_resume(ctx->player) == ESP_PLAYER_ERR_OK) {
                    EventBits_t bits_e = wait_player_bits(ctx->stream_num_index, PLAYED_BIT | ERROR_BIT, EVENT_WAIT_TIMEOUT_MS);
                    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bits_e & PLAYED_BIT, "resume: timeout waiting PLAYED");
                    TEST_ASSERT_EQUAL_MESSAGE(0, bits_e & ERROR_BIT, "resume: unexpected ERROR event");
                    vTaskDelay(pdMS_TO_TICKS(duration / 10 > 20 * 1000 ? 20 * 1000 : duration / 10));
                } else {
                    printf("resume skipped\n");
                }
                break;
            case 'k':
                TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_seek(ctx->player, 0));
                EventBits_t bits_k = wait_player_bits(ctx->stream_num_index, SEEK_DONE_BIT | ERROR_BIT, EVENT_WAIT_TIMEOUT_MS);
                TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bits_k & SEEK_DONE_BIT, "seek: timeout waiting SEEK_DONE");
                TEST_ASSERT_EQUAL_MESSAGE(0, bits_k & ERROR_BIT, "seek: unexpected ERROR event");
                vTaskDelay(pdMS_TO_TICKS(SEEK_STABILIZE_DELAY_MS));
                TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_get_play_time(ctx->player, &current_time));
                const char *dot = ctx->current_url ? strrchr(ctx->current_url, '.') : NULL;
                bool is_flac = (dot != NULL) && (strcasecmp(dot, ".flac") == 0);
                if (is_flac && current_time > (uint64_t)SEEK_TIME_DIFF_MS) {
                    ESP_LOGW(TAG, "seek: .flac current_time=%" PRIu64 " ms > %u ms (tolerated)",
                             current_time, (unsigned)SEEK_TIME_DIFF_MS);
                } else {
                    TEST_ASSERT_TRUE_MESSAGE(current_time <= (uint64_t)SEEK_TIME_DIFF_MS,
                                             "seek: current_time not near 0");
                }
                break;
            case 'n':
                TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_run_to_end(ctx->player));
                EventBits_t bits_n = wait_player_bits(ctx->stream_num_index, FINISHED_BIT | ERROR_BIT, EVENT_WAIT_FINISHED_MS);
                TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bits_n & FINISHED_BIT, "run_to_end: timeout waiting FINISHED");
                TEST_ASSERT_EQUAL_MESSAGE(0, bits_n & ERROR_BIT, "run_to_end: unexpected ERROR event");
                break;
            case 'o':
                EventBits_t bits_o = 0;
                esp_player_run(ctx->player);
                bits_o = wait_player_bits(ctx->stream_num_index, PLAYED_BIT | ERROR_BIT, EVENT_WAIT_TIMEOUT_MS);
                TEST_ASSERT_MESSAGE(bits_o & ERROR_BIT, "Expected ESP_PLAYER_EVENT_ERROR (corrupt / invalid source)");
                break;
            case 'b': {
                TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_run(ctx->player));
                vTaskDelay(pdMS_TO_TICKS(1000));
                EventBits_t pre = xEventGroupGetBits(g_player_event_group[ctx->stream_num_index]);
                TEST_ASSERT_EQUAL_MESSAGE(0, pre & (PLAYED_BIT | ERROR_BIT | STOPPED_BIT),
                                          "stop during open: open already settled before stop (server not stalling?)");
                TickType_t t0 = xTaskGetTickCount();
                TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_stop(ctx->player));
                uint32_t stop_ms = (uint32_t)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS);
                EventBits_t bits_b = wait_player_bits(ctx->stream_num_index, STOPPED_BIT | ERROR_BIT, EVENT_WAIT_TIMEOUT_MS);
                TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bits_b & STOPPED_BIT, "stop during open: expected STOPPED");
                TEST_ASSERT_EQUAL_MESSAGE(0, bits_b & ERROR_BIT, "stop during open: unexpected ERROR");
                TEST_ASSERT_TRUE_MESSAGE(stop_ms < 5000, "stop during open: stop() did not abort the blocked open promptly");
                break;
            }
            case 'd':
                esp_player_get_duration(ctx->player, &duration);
                ESP_LOGI(TAG, "Player duration: %" PRIu64 " ms", duration);
                ESP_LOGI(TAG, "Set speed to 0.5x");
                delay_time = duration / 10 > 20 * 1000 ? 20 * 1000 : duration / 10;
                esp_player_set_speed(ctx->player, 0.5f);
                esp_player_get_play_time(ctx->player, &cur_start_time);
                vTaskDelay(pdMS_TO_TICKS(delay_time));
                esp_player_get_play_time(ctx->player, &cur_end_time);
                // TEST_ASSERT_EQUAL(true, abs((cur_end_time - cur_start_time)*0.5f - delay_time) < 100);
                printf("cur_end_time - cur_start_time: %" PRIu64 "\n", cur_end_time - cur_start_time);
                ESP_LOGI(TAG, "Set speed to 1.0x");

                esp_player_set_speed(ctx->player, 1.0f);
                esp_player_get_play_time(ctx->player, &cur_start_time);
                delay_time = duration / 10 > 20 * 1000 ? 20 * 1000 : duration / 10;
                vTaskDelay(pdMS_TO_TICKS(delay_time));
                esp_player_get_play_time(ctx->player, &cur_end_time);
                // TEST_ASSERT_EQUAL(true, abs((cur_end_time - cur_start_time)*1.0f - delay_time) < 100);
                printf("cur_end_time - cur_start_time: %" PRIu64 "\n", cur_end_time - cur_start_time);
                ESP_LOGI(TAG, "Set speed to 2.0x");
                esp_player_set_speed(ctx->player, 2.0f);
                esp_player_get_play_time(ctx->player, &cur_start_time);
                delay_time = duration / 10 > 20 * 1000 ? 20 * 1000 : duration / 10;
                vTaskDelay(pdMS_TO_TICKS(delay_time));
                esp_player_get_play_time(ctx->player, &cur_end_time);
                // TEST_ASSERT_EQUAL(true, abs((cur_end_time - cur_start_time)*2.0f - delay_time) < 100);
                printf("cur_end_time - cur_start_time: %" PRIu64 "\n", cur_end_time - cur_start_time);
                ESP_LOGI(TAG, "Set speed to 1.0x");
                esp_player_set_speed(ctx->player, 1.0f);
                esp_player_get_play_time(ctx->player, &cur_start_time);
                delay_time = duration / 10 > 20 * 1000 ? 20 * 1000 : duration / 10;
                vTaskDelay(pdMS_TO_TICKS(delay_time));
                esp_player_get_play_time(ctx->player, &cur_end_time);
                // TEST_ASSERT_EQUAL(true, abs((cur_end_time - cur_start_time)*1.0f - delay_time) < 100);
                printf("cur_end_time - cur_start_time: %" PRIu64 "\n", cur_end_time - cur_start_time);
                break;
            case 'f':
                EventBits_t bits_f = wait_player_bits(ctx->stream_num_index, FINISHED_BIT | ERROR_BIT, EVENT_WAIT_FINISHED_MS);
                TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bits_f & FINISHED_BIT, "finish: timeout waiting FINISHED");
                TEST_ASSERT_EQUAL_MESSAGE(0, bits_f & ERROR_BIT, "finish: unexpected ERROR event");
                break;
            case 't':
                esp_player_run(ctx->player);
                EventBits_t bits_t = wait_player_bits(ctx->stream_num_index, PLAYED_BIT | ERROR_BIT, EVENT_WAIT_TIMEOUT_MS);
                if ((bits_t & ERROR_BIT) && !(bits_t & PLAYED_BIT)) {
                    bits_t = wait_player_bits(ctx->stream_num_index, PLAYED_BIT, EVENT_WAIT_TIMEOUT_MS);
                }
                TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bits_t & PLAYED_BIT, "terminal: surviving stream did not reach PLAYING");
                bits_t = wait_player_bits(ctx->stream_num_index, FINISHED_BIT, EVENT_WAIT_FINISHED_MS);
                TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bits_t & FINISHED_BIT, "terminal: surviving stream did not finish");
                break;
            case 'g':
                esp_player_get_duration(ctx->player, &duration);
                ESP_LOGI(TAG, "Player duration: %" PRIu64 " ms", duration);
                esp_gmf_element_handle_t element = NULL;
                esp_audio_render_stream_get_element(ctx->audio_stream_handle, ESP_AUDIO_RENDER_PROC_ALC, &element);
                if (element) {
                    ESP_LOGI(TAG, "Set gain to -10dB");
                    esp_gmf_audio_param_set_alc_gain(element, -10);
                    vTaskDelay(pdMS_TO_TICKS(duration / 10 > 20 * 1000 ? 20 * 1000 : duration / 10));
                    ESP_LOGI(TAG, "Set gain to 0dB");
                    esp_gmf_audio_param_set_alc_gain(element, 0);
                    vTaskDelay(pdMS_TO_TICKS(duration / 10 > 20 * 1000 ? 20 * 1000 : duration / 10));
                    ESP_LOGI(TAG, "Set gain to 10dB");
                    esp_gmf_audio_param_set_alc_gain(element, 10);
                    vTaskDelay(pdMS_TO_TICKS(duration / 10 > 20 * 1000 ? 20 * 1000 : duration / 10));
                    ESP_LOGI(TAG, "Set gain to 0dB");
                    esp_gmf_audio_param_set_alc_gain(element, 0);
                    vTaskDelay(pdMS_TO_TICKS(duration / 10 > 20 * 1000 ? 20 * 1000 : duration / 10));
                }
                break;
        }
    }
}

static test_config_t create_test_config(const char *url, uint8_t av_mask, uint8_t stream_num_index)
{
    return (test_config_t) {
        .sample_rate = output_sample_rate,
        .bits_per_sample = output_bits_per_sample,
        .channels = output_channels,
        .url = url,
        .av_mask = av_mask,
        .video_width = 640,
        .video_height = 360,
        .pixel_format = ESP_VIDEO_CODEC_PIXEL_FMT_RGB888,
        .video_fps = 30,
        .stream_num_index = stream_num_index};
}

static void execute_player_test(const char *test_name, const char *sequence, uint8_t av_mask, uint8_t stream_num_index, const char *test_path, const char *url, const char *file_type)
{
    test_config_t cfg = create_test_config(url, av_mask, stream_num_index);
    test_context_t ctx = {0};
    ctx.config = (esp_player_config_t)ESP_PLAYER_CONFIG_DEFAULT();
    ctx.stream_num_index = stream_num_index;

    esp_player_err_t ret = init_player(&ctx, &cfg);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);
    if (url) {
        ret = esp_player_set_url(ctx.player, url);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_PLAYER_ERR_OK, ret, "esp_player_set_url (http/path url)");
        ctx.current_url = url;
        if (sequence) {
            execute_play_sequence(&ctx, sequence);
        }
        goto _exit;
    }

    struct dirent *entry;
    char full_path[300];
    DIR *dir = opendir(test_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", test_path);
        TEST_FAIL_MESSAGE("opendir test_path failed (SD 未挂载或路径错误)");
    }
    uint32_t candidate_num = 0;
    uint32_t executed_num = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        snprintf(full_path, sizeof(full_path), "%s/%s", test_path, entry->d_name);
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode)) {
            if (file_type) {
                const char *dot = strrchr(entry->d_name, '.');
                if (dot == NULL || strcasecmp(dot, file_type) != 0) {
                    continue;
                }
            }
            candidate_num++;
            if (sequence && strchr(sequence, 'k') != NULL && (uint64_t)st.st_size < MIN_SEEK_TEST_FILE_BYTES) {
                continue;
            }
            printf("full_path: %s\n", full_path);
            ret = esp_player_set_url(ctx.player, full_path);
            TEST_ASSERT_EQUAL_MESSAGE(ESP_PLAYER_ERR_OK, ret, "esp_player_set_url (local file)");
            ctx.current_url = full_path;
            if (sequence) {
                execute_play_sequence(&ctx, sequence);
                executed_num++;
            }
            ctx.current_url = NULL;
        }
    }
    closedir(dir);
    ESP_LOGI(TAG, "execute_player_test: path=%s file_type=%s candidate=%" PRIu32 " executed=%" PRIu32,
             test_path, file_type ? file_type : "(all)", candidate_num, executed_num);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, candidate_num, "目录下无匹配媒体文件");
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, executed_num, "无成功执行的播放序列");

_exit:
    cleanup_player(&ctx);
    printf("%s: %s-%d\n", test_name, test_name, __LINE__);
    vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
}

TEST_CASE("[audio]:test_player_run_finish", "[player][audio]")
{
    execute_player_test("test_player_run_finish", "rf", ESP_PLAYER_MASK_AUDIO, 0, TEST_FILE_AUDIO_PATH, NULL, NULL);
}

TEST_CASE("[audio]:test_player_run_stop", "[player][audio]")
{
    execute_player_test("test_player_run_stop", "rs", ESP_PLAYER_MASK_AUDIO, 0, TEST_FILE_AUDIO_PATH, NULL, NULL);
}

TEST_CASE("[audio]:test_player_run_to_end", "[player][audio]")
{
    execute_player_test("test_player_run_to_end", "n", ESP_PLAYER_MASK_AUDIO, 0, TEST_FILE_AUDIO_PATH, NULL, NULL);
}

TEST_CASE("[audio]:test_player_run_pause_resume_stop", "[player][audio]")
{
    execute_player_test("test_player_run_pause_resume_stop", "rpes", ESP_PLAYER_MASK_AUDIO, 0, TEST_FILE_AUDIO_PATH, NULL, NULL);
}

TEST_CASE("[audio]:test_player_run_pause_stop", "[player][audio]")
{
    execute_player_test("test_player_run_pause_stop", "rps", ESP_PLAYER_MASK_AUDIO, 0, TEST_FILE_AUDIO_PATH, NULL, NULL);
}

TEST_CASE("[audio]:test_player_run_seek_pause_seek_resume_seek_stop", "[player][audio]")
{
    execute_player_test("test_player_run_seek_pause_seek_resume_seek_stop", "rkpkeks", ESP_PLAYER_MASK_AUDIO, 0, TEST_FILE_AUDIO_PATH, NULL, NULL);
}

TEST_CASE("[audio]:test_player_set_speed", "[player][audio]")
{
    g_proc_type[0][0] = ESP_AUDIO_RENDER_PROC_SONIC;
    g_proc_type_index[0] = 1;
    execute_player_test("test_player_set_speed", "rdf", ESP_PLAYER_MASK_AUDIO, 0, TEST_FILE_AUDIO_PATH, NULL, NULL);
    g_proc_type_index[0] = 0;
}

TEST_CASE("[audio]:test_player_set_gain", "[player][audio]")
{
    g_proc_type[0][0] = ESP_AUDIO_RENDER_PROC_ALC;
    g_proc_type_index[0] = 1;
    execute_player_test("test_player_set_gain", "rgf", ESP_PLAYER_MASK_AUDIO, 0, TEST_FILE_AUDIO_PATH, NULL, NULL);
    g_proc_type_index[0] = 0;
}

TEST_CASE("[video]:test_player_run_finish_v", "[player][video]")
{
    execute_player_test("test_player_run_finish_v", "rf", ESP_PLAYER_MASK_VIDEO, 0, TEST_FILE_VIDEO_PATH, NULL, NULL);
}

TEST_CASE("[video]:test_player_run_stop_v_loop", "[player][video]")
{
    execute_player_test("test_player_run_stop_v_loop", "rsrsrsrsrs", ESP_PLAYER_MASK_VIDEO, 0, TEST_FILE_VIDEO_PATH, NULL, NULL);
}

TEST_CASE("[video]:test_player_run_pause_stop_v", "[player][video]")
{
    execute_player_test("test_player_run_pause_stop_v", "rps", ESP_PLAYER_MASK_VIDEO, 0, TEST_FILE_VIDEO_PATH, NULL, NULL);
}

TEST_CASE("[video]:test_player_run_pause_resume_stop_v", "[player][video]")
{
    execute_player_test("test_player_run_pause_resume_stop_v", "rpes", ESP_PLAYER_MASK_VIDEO, 0, TEST_FILE_VIDEO_PATH, NULL, NULL);
}

TEST_CASE("[video]:test_player_run_seek_pause_seek_resume_seek_stop_v", "[player][video]")
{
    execute_player_test("test_player_run_seek_pause_seek_resume_seek_stop_v", "rkpkeks", ESP_PLAYER_MASK_VIDEO, 0, TEST_FILE_VIDEO_PATH, NULL, NULL);
}

TEST_CASE("[video]:test_player_set_speed_video", "[player][video]")
{
    execute_player_test("test_player_set_speed_v", "rdf", ESP_PLAYER_MASK_VIDEO, 0, TEST_FILE_VIDEO_PATH, NULL, NULL);
}

TEST_CASE("[av]:test_player_run_finish_av", "[player][av]")
{
    execute_player_test("test_player_run_finish_av", "rf", ESP_PLAYER_MASK_AV, 0, TEST_FILE_AV_PATH, NULL, NULL);
}

TEST_CASE("[av]:test_player_run_stop_av_loop", "[player][av]")
{
    execute_player_test("test_player_run_stop_av_loop", "rsrsrsrsrs", ESP_PLAYER_MASK_AV, 0, TEST_FILE_AV_PATH, NULL, NULL);
}

TEST_CASE("[av]:test_player_run_pause_stop_av", "[player][av]")
{
    execute_player_test("test_player_run_pause_stop_av", "rps", ESP_PLAYER_MASK_AV, 0, TEST_FILE_AV_PATH, NULL, NULL);
}

TEST_CASE("[av]:test_player_run_pause_resume_stop_av", "[player][av]")
{
    execute_player_test("test_player_run_pause_resume_stop_av", "rpes", ESP_PLAYER_MASK_AV, 0, TEST_FILE_AV_PATH, NULL, NULL);
}

TEST_CASE("[av]:test_player_run_seek_pause_seek_resume_seek_stop_av", "[player][av]")
{
    execute_player_test("test_player_run_seek_pause_seek_resume_seek_stop_av", "rkpkeks", ESP_PLAYER_MASK_AV, 0, TEST_FILE_AV_PATH, NULL, NULL);
}

TEST_CASE("[av]:test_player_set_speed_av", "[player][av]")
{
    g_proc_type[0][0] = ESP_AUDIO_RENDER_PROC_SONIC;
    g_proc_type_index[0] = 1;
    execute_player_test("test_player_set_speed_av", "rdf", ESP_PLAYER_MASK_AV, 0, TEST_FILE_AV_PATH, NULL, NULL);
    g_proc_type_index[0] = 0;
}

TEST_CASE("[frame]:test_player_submit_block_frame_stop", "[player][frame]")
{
    test_config_t cfg = {
        .sample_rate = 22050,
        .bits_per_sample = 16,
        .channels = 1,
        .url = NULL,
        .av_mask = ESP_PLAYER_MASK_AUDIO,
    };

    test_context_t ctx = {0};
    ctx.config = (esp_player_config_t)ESP_PLAYER_CONFIG_DEFAULT();

    esp_player_err_t ret = init_player(&ctx, &cfg);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    esp_player_set_url(ctx.player, TEST_BLOCK_URL_AAC);
    esp_player_run(ctx.player);
    esp_player_frame_t frame = {
        .data = (uint8_t *)test_data_aac_frame1,
        .data_len = TEST_DATA_AAC_FRAME1_COUNT,
        .pts = 0,
        .frame_type = ESP_PLAYER_FRAME_TYPE_DEFAULT,
        .is_bad = false,
        .eos = false,
    };
    esp_player_submit_frame(ctx.player, &frame, 0);
    frame.eos = true;
    esp_player_submit_frame(ctx.player, &frame, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));
    cleanup_player(&ctx);
}

TEST_CASE("[frame]:test_player_submit_fill_frame_stop", "[player][frame]")
{
    test_config_t cfg = {
        .sample_rate = 22050,
        .bits_per_sample = 16,
        .channels = 1,
        .url = NULL,
        .av_mask = ESP_PLAYER_MASK_AUDIO,
    };

    test_context_t ctx = {0};
    ctx.config = (esp_player_config_t)ESP_PLAYER_CONFIG_DEFAULT();

    esp_player_err_t ret = init_player(&ctx, &cfg);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    esp_player_set_url(ctx.player, TEST_FILL_URL_AAC);
    esp_player_run(ctx.player);

    esp_player_frame_t frame = {
        .data = (uint8_t *)test_data_aac_frame1,
        .data_len = TEST_DATA_AAC_FRAME1_COUNT,
        .pts = 0,
        .frame_type = ESP_PLAYER_FRAME_TYPE_DEFAULT,
        .is_bad = false,
        .eos = false,
    };
    esp_player_submit_frame(ctx.player, &frame, 1000);
    frame.eos = true;
    esp_player_submit_frame(ctx.player, &frame, 1000);
    vTaskDelay(pdMS_TO_TICKS(1000 * 1));

    cleanup_player(&ctx);
}

TEST_CASE("[error]:test_player_submit_frame_error", "[player][error]")
{
    test_config_t cfg = {
        .sample_rate = 22050,
        .bits_per_sample = 16,
        .channels = 1,
        .url = NULL,
        .av_mask = ESP_PLAYER_MASK_AUDIO,
    };

    test_context_t ctx = {0};
    ctx.config = (esp_player_config_t)ESP_PLAYER_CONFIG_DEFAULT();
    ctx.stream_num_index = 0;
    esp_player_err_t ret = init_player(&ctx, &cfg);
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, ret);

    esp_player_set_url(ctx.player, TEST_FILL_URL_AAC);
    esp_player_run(ctx.player);
    uint8_t test_data[TEST_DATA_AAC_FRAME1_COUNT];
    memset(test_data, 0, TEST_DATA_AAC_FRAME1_COUNT);
    esp_player_frame_t frame = {
        .data = (uint8_t *)test_data_aac_frame1,
        .data_len = TEST_DATA_AAC_FRAME1_COUNT,
        .pts = 0,
        .frame_type = ESP_PLAYER_FRAME_TYPE_DEFAULT,
        .is_bad = false,
        .eos = false,
    };
    esp_player_submit_frame(ctx.player, &frame, 1000);
    memcpy(test_data, test_data_aac_frame1, TEST_DATA_AAC_FRAME1_COUNT - 10);
    frame.data = (uint8_t *)test_data;
    // AUD_SDEC ERROR FRAME COUNT THRESHOLD 20
    for (int i = 0; i < 25; i++) {
        esp_player_submit_frame(ctx.player, &frame, 1000);
    }
    frame.data = (uint8_t *)test_data_aac_frame1;
    {
        EventBits_t got = wait_player_bits(ctx.stream_num_index, ERROR_BIT, EVENT_WAIT_TIMEOUT_MS);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, got & ERROR_BIT, "frame error: timeout waiting ERROR");
    }
    cleanup_player(&ctx);
}

TEST_CASE("[error]:test_player_error_handling", "[player][error]")
{
    execute_player_test("test_player_error_handling", "o", ESP_PLAYER_MASK_AUDIO, 0, TEST_FILE_ERROR_PATH, NULL, NULL);
}

TEST_CASE("[error]:test_player_part_error_handling", "[player][error]")
{
    execute_player_test("test_player_part_error_handling", "t", ESP_PLAYER_MASK_AV, 0, TEST_FILE_PERROR_PATH, NULL, NULL);
}

TEST_CASE("[error]:test_player_http_open_error", "[player][error][http][leaks=20000]")
{
    esp_gmf_app_test_case_uses_tcpip();
    esp_gmf_app_wifi_connect();
    execute_player_test("test_player_http_open_error", "o", ESP_PLAYER_MASK_AUDIO, 0, NULL, TEST_HTTP_URL_NOT_FOUND, NULL);
    esp_gmf_app_wifi_disconnect();
}

TEST_CASE("[error]:test_player_http_run_error", "[player][error][http][leaks=20000]")
{
    esp_gmf_app_test_case_uses_tcpip();
    esp_gmf_app_wifi_connect();
    execute_player_test("test_player_http_run_error", "o", ESP_PLAYER_MASK_AUDIO, 0, NULL, TEST_HTTP_URL_INVALID_MEDIA, NULL);
    esp_gmf_app_wifi_disconnect();
}

TEST_CASE("[audio][url][http]:test_player_run_stop", "[player][http][leaks=20000]")
{
    esp_gmf_app_test_case_uses_tcpip();
    esp_gmf_app_wifi_connect();
    execute_player_test("test_player_run_stop", "rs", ESP_PLAYER_MASK_AUDIO, 0, NULL, TEST_HTTPS_URL, NULL);
    esp_gmf_app_wifi_disconnect();
}

TEST_CASE("[audio][url][http]:test_player_run_pause_resume_stop_run_pause_stop", "[player][http][leaks=20000]")
{
    esp_gmf_app_test_case_uses_tcpip();
    esp_gmf_app_wifi_connect();
    execute_player_test("test_player_run_pause_resume_stop_run_pause_stop", "rpesrps", ESP_PLAYER_MASK_AUDIO, 0, NULL, TEST_HTTP_URL, NULL);
    esp_gmf_app_wifi_disconnect();
}

TEST_CASE("[audio][url][http]:test_player_run_seek_stop", "[player][http][leaks=20000]")
{
    esp_gmf_app_test_case_uses_tcpip();
    esp_gmf_app_wifi_connect();
    execute_player_test("test_player_run_seek_stop", "rks", ESP_PLAYER_MASK_AUDIO, 0, NULL, TEST_HTTP_URL, NULL);
    esp_gmf_app_wifi_disconnect();
}

TEST_CASE("[audio][url][http]:test_player_http_stop_during_open", "[player][http][leaks=20000]")
{
    esp_gmf_app_test_case_uses_tcpip();
    stall_server_t srv;
    TEST_ASSERT_EQUAL(ESP_OK, stall_server_start(&srv));
    execute_player_test("test_player_http_stop_during_open", "b", ESP_PLAYER_MASK_AUDIO, 0, NULL, STALL_SERVER_URL, NULL);
    stall_server_stop(&srv);
}

TEST_CASE("[audio][url][hls]:test_player_hls_aac_live_play_stop", "[player][hls][leaks=20000]")
{
    esp_gmf_app_test_case_uses_tcpip();
    esp_gmf_app_wifi_connect();
    execute_player_test("test_player_hls_aac_live_play_stop", "rs", ESP_PLAYER_MASK_AUDIO, 0, NULL, TEST_HLS_URL, NULL);
    esp_gmf_app_wifi_disconnect();
}

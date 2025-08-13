/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_gmf_oal_thread.h"
#include "esp_gmf_io_http.h"
#include <esp_gmf_pipeline.h>
#include "esp_audio_simple_player.h"
#include "esp_audio_simple_player_advance.h"
#include "esp_gmf_element.h"
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "esp_log.h"

#define TAG "DECODE_PCM"

typedef struct {
    const char *url;
    void       *ctx;
    int       (*on_info)(int sample_rate, uint8_t channel, int bits, void *ctx);
    int       (*on_pcm)(uint8_t *pcm, uint32_t len, void *ctx);
    int       (*on_exited)(void *ctx);
    bool       finished;
    uint8_t    core;
} dec_t;

static int on_pre_run(esp_asp_handle_t *handle, void *ctx)
{
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    esp_gmf_pipeline_handle_t pipeline = NULL;
    esp_audio_simple_player_get_pipeline(handle, &pipeline);
    if (pipeline == NULL) {
        return 0;
    }
    esp_gmf_element_handle_t element = NULL;
    esp_gmf_pipeline_get_in(pipeline, &element);
    if (element == NULL) {
        return 0;
    }
    // Set crt bundle
    http_io_cfg_t *http_cfg = (http_io_cfg_t*)OBJ_GET_CFG(element);
    http_cfg->crt_bundle_attach = esp_crt_bundle_attach;
#endif
    return 0;
}
static int out_data_callback(uint8_t *data, int data_size, void *ctx)
{
    dec_t *dec = (dec_t*)ctx;
    if (dec->finished) {
        return -1;
    }
    int ret = dec->on_pcm(data, data_size, dec->ctx);
    if (ret != 0) {
        dec->finished = true;
    }
    return ret;
}

static int on_event_callback(esp_asp_event_pkt_t *event, void *ctx)
{
    dec_t *dec = (dec_t*)ctx;
    if (event->type == ESP_ASP_EVENT_TYPE_MUSIC_INFO) {
        esp_asp_music_info_t *music_info = (esp_asp_music_info_t*)event->payload;
        ESP_LOGW(TAG, "%p rate:%d, channels:%d, bits:%d", dec, music_info->sample_rate, music_info->channels, music_info->bits);
        dec->on_info(music_info->sample_rate, music_info->channels, music_info->bits, dec->ctx);
    } else if (event->type == ESP_ASP_EVENT_TYPE_STATE) {
        esp_asp_state_t st = 0;
        memcpy(&st, event->payload, event->payload_size);
        ESP_LOGW(TAG, "Get State, %d,%s", st, esp_audio_simple_player_state_to_str(st));
        if (st == ESP_ASP_STATE_STOPPED || st == ESP_ASP_STATE_FINISHED) {
            dec->finished = true;
        }
    }
    return 0;
}

static void decode_task(void *arg)
{
    dec_t *dec = (dec_t*) arg;
    esp_asp_cfg_t cfg = {
        .prev = on_pre_run,
        .out.cb = out_data_callback,
        .out.user_ctx = dec,
        .task_core = dec->core,
        .task_prio = 5,
    };
    printf("Task run on core %d\n", dec->core);
    esp_asp_handle_t handle = NULL;
    esp_gmf_err_t ret = esp_audio_simple_player_new(&cfg, &handle);
    do {
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to new player ret %d", ret);
            break;
        }
        esp_audio_simple_player_set_event(handle, on_event_callback, dec);
        ret = esp_audio_simple_player_run(handle, dec->url, NULL);
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to run player ret %d", ret);
            break;
        }
        while (dec->finished == false) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        esp_audio_simple_player_stop(handle);
    } while (0);
    dec->on_exited(dec->ctx);
    if (handle) {
        esp_audio_simple_player_destroy(handle);
    }
    free(dec);
    esp_gmf_oal_thread_delete(NULL);
}

int decode_pcm(const char *url,
               int (*on_info)(int sample_rate, uint8_t channel, int bits, void *ctx),
               int (*on_pcm)(uint8_t *pcm, uint32_t len, void *ctx),
               int (*on_exited)(void *ctx),
               void *ctx,
               uint8_t core)
{
    dec_t *dec = (dec_t*) calloc(1, sizeof(dec_t));
    if (dec == NULL) {
        return -1;
    }
    dec->url = url;
    dec->ctx = ctx;
    dec->on_info = on_info;
    dec->on_pcm = on_pcm;
    dec->on_exited = on_exited;
    dec->core = core;

    if (ESP_GMF_ERR_OK != esp_gmf_oal_thread_create(NULL, "Player", decode_task, dec, 4 *1024, 5, true, 0)) {
        free(dec);
        return -1;
    }
    return 0;
}

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_gmf_task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_media.h"
#include "esp_gmf_audio_dec.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_element.h"
#include "esp_gmf_err.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_io_bt.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_ch_cvt.h"
#if CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM
#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"
#include "esp_board_periph.h"
#include "esp_codec_dev.h"
#include "dev_audio_codec.h"
#include "esp_bt_audio_le_playback_sync.h"
#endif  /* CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM */

#include "stream_proc.h"
#include "codec_defs.h"

static const char *TAG = "STREAM_PROC";

/* Playlist configuration */
static const char *playlist[] = {
    "file://sdcard/media0.mp3",
    "file://sdcard/media1.mp3",
    "file://sdcard/media2.mp3",
};
static const size_t playlist_len = sizeof(playlist) / sizeof(playlist[0]);
static size_t playlist_cur_index = 0;

/* Pipeline handles */
static esp_gmf_task_handle_t bt2codec_task = NULL;
static esp_gmf_pipeline_handle_t bt2codec_pipe = NULL;

static esp_gmf_task_handle_t codec2bt_task = NULL;
static esp_gmf_pipeline_handle_t codec2bt_pipe = NULL;

static esp_gmf_task_handle_t local2bt_task = NULL;
static esp_gmf_pipeline_handle_t local2bt_pipe = NULL;
static esp_bt_audio_stream_handle_t local2bt_stream = NULL;

#if CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM
static esp_bt_audio_le_playback_sync_handle_t playback_sync = NULL;

static esp_err_t stream_proc_open_dac(dev_audio_codec_handles_t *dac_handle)
{
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = CODEC_DAC_SAMPLE_RATE,
        .bits_per_sample = CODEC_DAC_BITS_PER_SAMPLE,
        .channel = CODEC_DAC_CHANNELS,
    };
    return esp_codec_dev_open(dac_handle->codec_dev, &fs);
}

static i2s_chan_handle_t get_i2s_chan_handle(const char *name)
{
    i2s_chan_handle_t ch = NULL;
    dev_audio_codec_config_t *codec_config = NULL;
    esp_err_t ret = esp_board_manager_get_device_config(name, (void **)&codec_config);
    ESP_RETURN_ON_FALSE(ret == ESP_OK, NULL, TAG, "get device config failed");
    ret = esp_board_periph_get_handle(codec_config->i2s_cfg.name, (void **)&ch);
    ESP_RETURN_ON_FALSE(ret == ESP_OK, NULL, TAG, "get i2s chan handle failed");
    ESP_LOGI(TAG, "get i2s[%s:%s] handle %p", name, codec_config->i2s_cfg.name, ch);
    return ch;
}

static void stream_proc_deinit_playback_sync(void)
{
    if (!playback_sync) {
        return;
    }

    esp_err_t ret = esp_bt_audio_le_playback_sync_disable(playback_sync);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Playback sync disable failed: %s", esp_err_to_name(ret));
    }
    ret = esp_bt_audio_le_playback_sync_deinit(playback_sync);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Playback sync deinit failed: %s", esp_err_to_name(ret));
    }
    playback_sync = NULL;
}

static void stream_proc_prepare_playback_sync(void)
{
    if (playback_sync) {
        return;
    }

    dev_audio_codec_handles_t *dac_handle = NULL;
    esp_err_t ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&dac_handle);
    if (ret != ESP_OK || !dac_handle) {
        ESP_LOGE(TAG, "get audio dac handle failed");
        return;
    }

    ret = esp_codec_dev_close(dac_handle->codec_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "close audio dac failed: %s", esp_err_to_name(ret));
        return;
    }

    i2s_chan_handle_t tx_handle = get_i2s_chan_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
    if (!tx_handle) {
        ESP_LOGE(TAG, "get i2s tx handle failed");
        stream_proc_open_dac(dac_handle);
        return;
    }
    ret = esp_bt_audio_le_playback_sync_init(tx_handle, &playback_sync);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Playback sync init failed: %s", esp_err_to_name(ret));
        stream_proc_open_dac(dac_handle);
        return;
    }

    ret = esp_bt_audio_le_playback_sync_enable(playback_sync);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Playback sync enable failed: %s", esp_err_to_name(ret));
        esp_bt_audio_le_playback_sync_deinit(playback_sync);
        playback_sync = NULL;
        stream_proc_open_dac(dac_handle);
        return;
    }

    ret = stream_proc_open_dac(dac_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "open audio dac failed: %s", esp_err_to_name(ret));
        stream_proc_deinit_playback_sync();
    }
}
#else
static void stream_proc_prepare_playback_sync(void)
{
}

static void stream_proc_deinit_playback_sync(void)
{
}
#endif  /* CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM */

static const char *gmf_state_to_str(int state)
{
    switch (state) {
        case ESP_GMF_EVENT_STATE_NONE:
            return "NONE";
        case ESP_GMF_EVENT_STATE_INITIALIZED:
            return "INITIALIZED";
        case ESP_GMF_EVENT_STATE_OPENING:
            return "OPENING";
        case ESP_GMF_EVENT_STATE_RUNNING:
            return "RUNNING";
        case ESP_GMF_EVENT_STATE_PAUSED:
            return "PAUSED";
        case ESP_GMF_EVENT_STATE_STOPPED:
            return "STOPPED";
        case ESP_GMF_EVENT_STATE_FINISHED:
            return "FINISHED";
        case ESP_GMF_EVENT_STATE_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

static void local2bt_play(const char *uri)
{
    if (local2bt_pipe == NULL) {
        ESP_LOGE(TAG, "Local to BT pipeline is not initialized");
        return;
    }
    if (local2bt_stream == NULL) {
        ESP_LOGE(TAG, "Local to BT stream is not initialized");
        return;
    }
    esp_gmf_pipeline_stop(local2bt_pipe);
    esp_gmf_pipeline_reset(local2bt_pipe);
    esp_gmf_io_bt_set_stream(ESP_GMF_PIPELINE_GET_OUT_INSTANCE(local2bt_pipe), local2bt_stream);
    esp_gmf_pipeline_set_in_uri(local2bt_pipe, uri);
    ESP_LOGI(TAG, "A2DP Source play: %s (index %d)", uri, playlist_cur_index);
    esp_gmf_pipeline_loading_jobs(local2bt_pipe);
    esp_gmf_pipeline_run(local2bt_pipe);
}

void local2bt_play_next(void)
{
    if (local2bt_stream == NULL) {
        ESP_LOGE(TAG, "Local to BT stream is not initialized");
        return;
    }
    playlist_cur_index = (playlist_cur_index + 1) % playlist_len;
    local2bt_play(playlist[playlist_cur_index]);
}

void local2bt_play_prev(void)
{
    if (local2bt_stream == NULL) {
        ESP_LOGE(TAG, "Local to BT stream is not initialized");
        return;
    }
    playlist_cur_index = (playlist_cur_index + playlist_len - 1) % playlist_len;
    local2bt_play(playlist[playlist_cur_index]);
}

static void stream_proc_destroy(stream_user_data_t *user_d)
{
    ESP_LOGI(TAG, "stream_user_data_destroy %p", user_d);
    if (user_d) {
        free(user_d);
    }
    ESP_LOGI(TAG, "stream_user_data_destroy done");
}

static void stream_proc_prepare(esp_bt_audio_stream_handle_t stream, stream_user_data_t **out)
{
    stream_user_data_t *user_d = heap_caps_calloc(1, sizeof(stream_user_data_t), MALLOC_CAP_SPIRAM);
    if (user_d == NULL) {
        ESP_LOGE(TAG, "calloc user data failed");
        *out = NULL;
        return;
    }
    esp_bt_audio_stream_codec_info_t codec_info = {0};
    esp_bt_audio_stream_get_codec_info(stream, &codec_info);
    ESP_LOGI(TAG, "Codec Info: type=%d, bits=%d, channels=%d, sample_rate=%d, cfg_size=%d, codec_cfg=%p",
             codec_info.codec_type,
             codec_info.bits,
             codec_info.channels,
             codec_info.sample_rate,
             codec_info.cfg_size,
             codec_info.codec_cfg);

    esp_bt_audio_stream_dir_t dir = ESP_BT_AUDIO_STREAM_DIR_UNKNOWN;
    if (esp_bt_audio_stream_get_dir(stream, &dir) != ESP_OK) {
        ESP_LOGE(TAG, "Get stream dir failed, stream=%p", stream);
        free(user_d);
        *out = NULL;
        return;
    }

    if (dir == ESP_BT_AUDIO_STREAM_DIR_SINK) {
        ESP_LOGI(TAG, "Prepare bt to codec pipeline");
        user_d->pipe = bt2codec_pipe;
        esp_gmf_io_bt_set_stream(ESP_GMF_PIPELINE_GET_IN_INSTANCE(user_d->pipe), stream);
        esp_audio_simple_dec_cfg_t simple_dec_cfg = {
            .dec_type = codec_info.codec_type == ESP_BT_AUDIO_STREAM_CODEC_SBC ? ESP_AUDIO_TYPE_SBC : ESP_AUDIO_TYPE_LC3,
            .dec_cfg = codec_info.codec_cfg,
            .cfg_size = codec_info.cfg_size,
        };
        esp_gmf_audio_dec_reconfig(user_d->pipe->head_el, &simple_dec_cfg);

        esp_gmf_element_handle_t aud_rate_cvt = NULL;
        esp_gmf_pipeline_get_el_by_name(user_d->pipe, "aud_rate_cvt", &aud_rate_cvt);
        esp_gmf_rate_cvt_set_dest_rate(aud_rate_cvt, 48000);

        esp_gmf_element_handle_t aud_ch_cvt = NULL;
        esp_gmf_pipeline_get_el_by_name(user_d->pipe, "aud_ch_cvt", &aud_ch_cvt);
        esp_gmf_ch_cvt_set_dest_channel(aud_ch_cvt, 2);

        stream_proc_prepare_playback_sync();
        esp_gmf_pipeline_loading_jobs(user_d->pipe);
    } else {
        uint32_t context = 0;
        if (esp_bt_audio_stream_get_context(stream, &context) != ESP_OK) {
            ESP_LOGE(TAG, "Get stream context failed, stream=%p", stream);
        }
        if (context == ESP_BT_AUDIO_STREAM_CONTEXT_MEDIA) {
            ESP_LOGI(TAG, "Prepare local to bt pipeline");
            user_d->pipe = local2bt_pipe;
            local2bt_stream = stream;

            esp_audio_simple_dec_cfg_t simple_dec_cfg = {
                .dec_type = ESP_AUDIO_TYPE_MP3,
                .dec_cfg = NULL,
                .cfg_size = 0,
            };
            esp_gmf_audio_dec_reconfig(user_d->pipe->head_el, &simple_dec_cfg);
            esp_gmf_pipeline_set_in_uri(user_d->pipe, playlist[playlist_cur_index]);
            ESP_LOGI(TAG, "Set media file: %s (index %d)", playlist[playlist_cur_index], playlist_cur_index);
        } else {
            ESP_LOGI(TAG, "Prepare codec to bt pipeline");
            user_d->pipe = codec2bt_pipe;

            esp_gmf_info_sound_t info = {
                .sample_rates = CODEC_ADC_SAMPLE_RATE,
                .channels = CODEC_ADC_CHANNELS,
                .bits = CODEC_ADC_BITS_PER_SAMPLE,
            };
            esp_gmf_pipeline_report_info(user_d->pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));
        }
        esp_gmf_io_bt_set_stream(ESP_GMF_PIPELINE_GET_OUT_INSTANCE(user_d->pipe), stream);

        const void *iterator = NULL;
        esp_gmf_element_handle_t cur_el = NULL;
        uint8_t rate_cvt_count = 0;
        while (esp_gmf_pipeline_iterate_element(codec2bt_pipe, &iterator, &cur_el) == ESP_GMF_ERR_OK) {
            char *el_tag = NULL;
            esp_gmf_obj_get_tag((esp_gmf_obj_handle_t)cur_el, &el_tag);
            if (el_tag && strcasecmp(el_tag, "aud_rate_cvt") == 0) {
                rate_cvt_count++;
            }
            if (rate_cvt_count == 2) {
                esp_gmf_rate_cvt_set_dest_rate(cur_el, codec_info.sample_rate);
                break;
            }
        }

        esp_gmf_element_handle_t aud_ch_cvt = NULL;
        esp_gmf_pipeline_get_el_by_name(user_d->pipe, "aud_ch_cvt", &aud_ch_cvt);
        esp_gmf_ch_cvt_set_dest_channel(aud_ch_cvt, __builtin_popcount(codec_info.channels));

        esp_audio_enc_config_t enc_cfg = {
            .type = codec_info.codec_type == ESP_BT_AUDIO_STREAM_CODEC_SBC ? ESP_AUDIO_TYPE_SBC : ESP_AUDIO_TYPE_LC3,
            .cfg = codec_info.codec_cfg,
            .cfg_sz = codec_info.cfg_size,
        };
        esp_gmf_audio_enc_reconfig(user_d->pipe->last_el, &enc_cfg);
        esp_gmf_pipeline_loading_jobs(user_d->pipe);
    }
    *out = user_d;
}

void stream_proc_state_chg(esp_bt_audio_stream_handle_t stream, esp_bt_audio_stream_state_t state)
{
    const char *state_str[] = {"ALLOCATED", "STARTED", "STOPPED", "RELEASED"};
    esp_bt_audio_stream_dir_t dir = ESP_BT_AUDIO_STREAM_DIR_UNKNOWN;
    esp_bt_audio_stream_get_dir(stream, &dir);
    ESP_LOGI(TAG, "Stream state changed: stream %p, dir %d, state %s", stream, dir, state_str[state]);
    switch (state) {
        case ESP_BT_AUDIO_STREAM_STATE_ALLOCATED: {
            stream_user_data_t *user_dat = NULL;
            esp_bt_audio_stream_get_local_data(stream, (void **)&user_dat);
            if (user_dat) {
                stream_proc_destroy(user_dat);
                esp_bt_audio_stream_set_local_data(stream, NULL);
                user_dat = NULL;
            }
            stream_proc_prepare(stream, &user_dat);
            esp_bt_audio_stream_set_local_data(stream, user_dat);
            break;
        }
        case ESP_BT_AUDIO_STREAM_STATE_STARTED: {
            stream_user_data_t *user_d = NULL;
            esp_bt_audio_stream_get_local_data(stream, (void **)&user_d);
            if (user_d && user_d->pipe) {
                esp_gmf_pipeline_run(user_d->pipe);
            } else {
                ESP_LOGE(TAG, "Stream user data not prepared for stream %p", stream);
            }
            break;
        }
        case ESP_BT_AUDIO_STREAM_STATE_STOPPED: {
            stream_user_data_t *user_d = NULL;
            esp_bt_audio_stream_get_local_data(stream, (void **)&user_d);
            if (user_d && user_d->pipe) {
                ESP_LOGI(TAG, "Reset pipeline %p", user_d->pipe);
                esp_gmf_pipeline_stop(user_d->pipe);
                esp_gmf_pipeline_reset(user_d->pipe);
            }
            if (dir == ESP_BT_AUDIO_STREAM_DIR_SINK) {
                stream_proc_deinit_playback_sync();
            }
            break;
        }
        case ESP_BT_AUDIO_STREAM_STATE_RELEASED: {
            stream_user_data_t *user_d = NULL;
            esp_bt_audio_stream_get_local_data(stream, (void **)&user_d);
            if (dir == ESP_BT_AUDIO_STREAM_DIR_SINK) {
                stream_proc_deinit_playback_sync();
            }
            if (user_d) {
                stream_proc_destroy(user_d);
                esp_bt_audio_stream_set_local_data(stream, NULL);
            }
            if (local2bt_stream == stream) {
                local2bt_stream = NULL;
            }
            break;
        }
        default:
            break;
    }
}

static esp_gmf_err_t bt2codec_pipe_event_cb(esp_gmf_event_pkt_t *pkt, void *ctx)
{
    if (pkt == NULL) {
        return ESP_GMF_ERR_OK;
    }
    if (pkt->type == ESP_GMF_EVT_TYPE_CHANGE_STATE) {
        ESP_LOGI(TAG, "[bt2codec pipeline] state => %s(%d)", gmf_state_to_str(pkt->sub), pkt->sub);
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t codec2bt_pipe_event_cb(esp_gmf_event_pkt_t *pkt, void *ctx)
{
    if (pkt == NULL) {
        return ESP_GMF_ERR_OK;
    }
    if (pkt->type == ESP_GMF_EVT_TYPE_CHANGE_STATE) {
        ESP_LOGI(TAG, "[codec2bt pipeline] state => %s(%d)", gmf_state_to_str(pkt->sub), pkt->sub);
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t local2bt_pipe_event_cb(esp_gmf_event_pkt_t *pkt, void *ctx)
{
    if (pkt == NULL) {
        return ESP_GMF_ERR_OK;
    }
    if (pkt->type == ESP_GMF_EVT_TYPE_CHANGE_STATE) {
        ESP_LOGI(TAG, "[a2dp source pipeline] state => %s(%d)", gmf_state_to_str(pkt->sub), pkt->sub);
        if (pkt->sub == ESP_GMF_EVENT_STATE_FINISHED) {
            ESP_LOGI(TAG, "A2DP Source finished");
            if (local2bt_stream) {
                local2bt_play_next();
            }
        } else if (pkt->sub == ESP_GMF_EVENT_STATE_ERROR) {
            ESP_LOGE(TAG, "A2DP Source error");
            esp_bt_audio_media_stop(ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC);
        }
    }
    return ESP_GMF_ERR_OK;
}

static void setup_pipeline_bt2codec(esp_gmf_pool_handle_t pool)
{
    const char *name[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt"};
    esp_gmf_pool_new_pipeline(pool, "io_bt", name, sizeof(name) / sizeof(char *), "io_codec_dev", &bt2codec_pipe);
    esp_gmf_pipeline_set_event(bt2codec_pipe, bt2codec_pipe_event_cb, NULL);

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.thread.core = 0;
    cfg.thread.stack = 5120;
    cfg.thread.prio = 15;
    cfg.thread.stack_in_ext = true;
    cfg.name = "bt2codec_task";
    esp_gmf_task_init(&cfg, &bt2codec_task);

    esp_gmf_pipeline_bind_task(bt2codec_pipe, bt2codec_task);
}

static void setup_pipeline_codec2bt(esp_gmf_pool_handle_t pool)
{
    const char *name[] = {"aud_rate_cvt", "ai_aec", "aud_ch_cvt", "aud_rate_cvt", "aud_enc"};
    esp_gmf_pool_new_pipeline(pool, "io_codec_dev", name, sizeof(name) / sizeof(char *), "io_bt", &codec2bt_pipe);
    esp_gmf_pipeline_set_event(codec2bt_pipe, codec2bt_pipe_event_cb, NULL);

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.thread.core = 1;
    cfg.thread.stack = 5120;
    cfg.thread.prio = 15;
    cfg.thread.stack_in_ext = true;
    cfg.name = "codec2bt_task";
    esp_gmf_task_init(&cfg, &codec2bt_task);

    const void *iterator = NULL;
    esp_gmf_element_handle_t cur_el = NULL;
    while (esp_gmf_pipeline_iterate_element(codec2bt_pipe, &iterator, &cur_el) == ESP_GMF_ERR_OK) {
        char *el_tag = NULL;
        esp_gmf_obj_get_tag((esp_gmf_obj_handle_t)cur_el, &el_tag);
        if (el_tag && strcasecmp(el_tag, "aud_rate_cvt") == 0) {
            esp_gmf_rate_cvt_set_dest_rate(cur_el, 8000);
            break;
        }
    }

    esp_gmf_pipeline_bind_task(codec2bt_pipe, codec2bt_task);
}

static void setup_pipeline_local2bt(esp_gmf_pool_handle_t pool)
{
    const char *name[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt", "aud_enc"};
    esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), "io_bt", &local2bt_pipe);
    esp_gmf_pipeline_set_event(local2bt_pipe, local2bt_pipe_event_cb, NULL);

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.thread.core = 1;
    cfg.thread.stack = 5120;
    cfg.thread.prio = 15;
    cfg.thread.stack_in_ext = true;
    cfg.name = "local2bt_task";
    esp_gmf_task_init(&cfg, &local2bt_task);

    esp_gmf_pipeline_bind_task(local2bt_pipe, local2bt_task);
}

void stream_proc_init(esp_gmf_pool_handle_t pool)
{
    setup_pipeline_bt2codec(pool);
    setup_pipeline_codec2bt(pool);
    setup_pipeline_local2bt(pool);
}

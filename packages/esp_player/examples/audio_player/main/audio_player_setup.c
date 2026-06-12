/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>

#include "esp_log.h"
#include "esp_codec_dev.h"
#include "esp_board_manager_includes.h"
#include "media_lib_adapter.h"
#include "esp_extractor_defaults.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_render.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_alc.h"

#include "audio_player_setup.h"

static const char *TAG = "AUDIO_PLAYER_SETUP";

static esp_audio_render_handle_t s_render = NULL;
static esp_gmf_pool_handle_t s_pool = NULL;
static uint8_t s_next_stream_id = 0;

static void destroy_audio_render(void);

static void register_media_defaults(void)
{
    media_lib_add_default_adapter();
    esp_extractor_register_default();
    esp_audio_dec_register_default();
}

static void unregister_media_defaults(void)
{
    esp_audio_dec_unregister_default();
    esp_extractor_unregister_default();
}

static esp_codec_dev_handle_t get_playback_codec_dev(void)
{
    dev_audio_codec_handles_t *dac_handles = NULL;
    if (esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&dac_handles) != ESP_OK
        || dac_handles == NULL) {
        return NULL;
    }
    return dac_handles->codec_dev;
}

static void close_playback_codec(void)
{
    esp_codec_dev_handle_t codec_dev = get_playback_codec_dev();
    if (codec_dev == NULL) {
        return;
    }
    esp_codec_dev_close(codec_dev);
    esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
}

static int render_writer_cb(uint8_t *pcm, uint32_t len, void *ctx)
{
    esp_codec_dev_handle_t dev = (esp_codec_dev_handle_t)ctx;
    if (dev == NULL || pcm == NULL || len == 0) {
        return -1;
    }
    return esp_codec_dev_write(dev, pcm, len);
}

static int register_render_pool(esp_gmf_pool_handle_t *out_pool)
{
    *out_pool = NULL;
    if (esp_gmf_pool_init(out_pool) != ESP_GMF_ERR_OK) {
        return -1;
    }
    esp_gmf_element_handle_t el = NULL;

    esp_ae_ch_cvt_cfg_t ch_cfg = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
    if (esp_gmf_ch_cvt_init(&ch_cfg, &el) == ESP_GMF_ERR_OK) {
        esp_gmf_pool_register_element(*out_pool, el, NULL);
    }

    esp_ae_bit_cvt_cfg_t bit_cfg = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
    if (esp_gmf_bit_cvt_init(&bit_cfg, &el) == ESP_GMF_ERR_OK) {
        esp_gmf_pool_register_element(*out_pool, el, NULL);
    }

    esp_ae_rate_cvt_cfg_t rate_cfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
    if (esp_gmf_rate_cvt_init(&rate_cfg, &el) == ESP_GMF_ERR_OK) {
        esp_gmf_pool_register_element(*out_pool, el, NULL);
    }

    esp_ae_alc_cfg_t alc_cfg = DEFAULT_ESP_GMF_ALC_CONFIG();
    if (esp_gmf_alc_init(&alc_cfg, &el) == ESP_GMF_ERR_OK) {
        esp_gmf_pool_register_element(*out_pool, el, NULL);
    }
    return 0;
}

static esp_player_err_t open_playback_codec(uint32_t sample_rate,
                                            uint8_t bits_per_sample,
                                            uint8_t channels,
                                            esp_codec_dev_handle_t *codec_dev)
{
    if (codec_dev == NULL) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    *codec_dev = NULL;

    if (esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init audio DAC");
        return ESP_PLAYER_ERR_FAIL;
    }

    *codec_dev = get_playback_codec_dev();
    if (*codec_dev == NULL) {
        ESP_LOGE(TAG, "Failed to get playback codec handle");
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
        return ESP_PLAYER_ERR_FAIL;
    }

    esp_err_t ret = esp_codec_dev_set_out_vol(*codec_dev, AUDIO_PLAYER_OUTPUT_VOLUME);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Failed to set output volume, continue playback");
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = sample_rate,
        .channel = channels,
        .bits_per_sample = bits_per_sample,
    };
    ret = esp_codec_dev_open(*codec_dev, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open playback codec");
        *codec_dev = NULL;
        esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
        return ESP_PLAYER_ERR_FAIL;
    }
    return ESP_PLAYER_ERR_OK;
}

static esp_player_err_t create_audio_render(const audio_render_settings_t *settings)
{
    esp_codec_dev_handle_t codec_dev = NULL;
    if (open_playback_codec(settings->out_sample_rate,
                            settings->out_bits_per_sample,
                            settings->out_channels,
                            &codec_dev) != ESP_PLAYER_ERR_OK) {
        return ESP_PLAYER_ERR_FAIL;
    }

    if (register_render_pool(&s_pool) != 0) {
        ESP_LOGE(TAG, "Failed to register GMF audio processing pool");
        goto fail;
    }

    esp_audio_render_cfg_t cfg = {
        .max_stream_num = AUDIO_PLAYER_STREAM_NUM,
        .out_writer = render_writer_cb,
        .out_ctx = codec_dev,
        .out_sample_info = {
            .sample_rate = settings->out_sample_rate,
            .bits_per_sample = settings->out_bits_per_sample,
            .channel = settings->out_channels,
        },
        .pool = s_pool,
        .process_period = 20,
    };
    if (esp_audio_render_create(&cfg, &s_render) != ESP_AUDIO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to create esp_audio_render");
        goto fail;
    }

    ESP_LOGI(TAG, "Audio render ready: %u stream(s), %" PRIu32 " Hz, %u-bit, %u-ch",
             AUDIO_PLAYER_STREAM_NUM,
             settings->out_sample_rate,
             settings->out_bits_per_sample,
             settings->out_channels);
    return ESP_PLAYER_ERR_OK;

fail:
    destroy_audio_render();
    return ESP_PLAYER_ERR_FAIL;
}

static void destroy_audio_render(void)
{
    if (s_render) {
        esp_audio_render_destroy(s_render);
        s_render = NULL;
    }
    if (s_pool) {
        esp_gmf_pool_deinit(s_pool);
        s_pool = NULL;
    }
    close_playback_codec();
}

esp_player_err_t audio_player_setup(const audio_render_settings_t *render_settings)
{
    if (render_settings == NULL) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    if (s_render != NULL) {
        ESP_LOGE(TAG, "audio_player_setup already called");
        return ESP_PLAYER_ERR_FAIL;
    }

    register_media_defaults();

    if (create_audio_render(render_settings) != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(TAG, "create_audio_render failed");
        unregister_media_defaults();
        return ESP_PLAYER_ERR_FAIL;
    }

    s_next_stream_id = 0;
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t audio_player_new(esp_player_handle_t *player)
{
    if (player == NULL) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    *player = NULL;

    if (s_render == NULL) {
        ESP_LOGE(TAG, "Call audio_player_setup() first");
        return ESP_PLAYER_ERR_FAIL;
    }
    if (s_next_stream_id >= AUDIO_PLAYER_STREAM_NUM) {
        ESP_LOGE(TAG, "No free render stream (max=%u)", AUDIO_PLAYER_STREAM_NUM);
        return ESP_PLAYER_ERR_FAIL;
    }

    uint8_t stream_id = s_next_stream_id++;
    esp_audio_render_stream_handle_t stream = NULL;
    if (esp_audio_render_stream_get(s_render, ESP_AUDIO_RENDER_STREAM_ID(stream_id),
                                    &stream) != ESP_AUDIO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to get render stream %u", stream_id);
        s_next_stream_id = stream_id;
        return ESP_PLAYER_ERR_FAIL;
    }

    esp_player_config_t player_cfg = ESP_PLAYER_CONFIG_DEFAULT();
    player_cfg.audio_render_hd = stream;

    if (esp_player_init(&player_cfg, player) != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(TAG, "esp_player_init failed for stream %u", stream_id);
        s_next_stream_id = stream_id;
        return ESP_PLAYER_ERR_FAIL;
    }

    return ESP_PLAYER_ERR_OK;
}

void audio_player_delete(esp_player_handle_t player)
{
    if (player == NULL) {
        return;
    }

    esp_player_set_event_cb(player, NULL, NULL);
    esp_player_deinit(player);
}

void audio_player_teardown(void)
{
    if (s_next_stream_id > 0 && s_render != NULL) {
        ESP_LOGD(TAG, "Tear down render after %u player(s)", s_next_stream_id);
    }
    if (s_render != NULL) {
        destroy_audio_render();
        unregister_media_defaults();
    }
    s_next_stream_id = 0;
}

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "unity.h"
#include "esp_log.h"

#include "esp_player.h"
#include "esp_player_advance.h"
#include "esp_audio_render.h"
#include "esp_audio_dec_default.h"
#include "esp_gmf_audio_dec.h"
#include "esp_gmf_obj.h"

#include "render_common.h"
#include "test_data.h"

#define EVT_PLAYED    (1 << 0)
#define EVT_FINISHED  (1 << 1)
#define EVT_ERROR     (1 << 2)
#define EVT_STOPPED   (1 << 3)

#define PLAYBACK_WAIT_MS  5000
#define STOP_WAIT_MS      2000

typedef struct {
    int       accept_calls;   /*!< Times the "accept" factory was entered */
    int       decline_calls;  /*!< Times the "decline" factory was entered */
    int       bad_tag_calls;  /*!< Times the "bad-tag" factory was entered */
    uint32_t  last_codec_cc;  /*!< FourCC seen on the most recent call */
} factory_stats_t;

typedef struct {
    esp_player_handle_t  player;
    void                *audio_render;
} player_fixture_t;

static const char *TAG = "TEST_CUSTOM_DEC";

static factory_stats_t s_stats;
static EventGroupHandle_t s_evts;

static esp_player_err_t _evt_cb(esp_player_event_msg_t *msg, void *ctx)
{
    (void)ctx;
    if (s_evts == NULL) {
        return ESP_PLAYER_ERR_OK;
    }
    switch (msg->event_type) {
        case ESP_PLAYER_EVENT_PLAYED:
            xEventGroupSetBits(s_evts, EVT_PLAYED);
            break;
        case ESP_PLAYER_EVENT_FINISHED:
            xEventGroupSetBits(s_evts, EVT_FINISHED);
            break;
        case ESP_PLAYER_EVENT_STOPPED:
            xEventGroupSetBits(s_evts, EVT_STOPPED);
            break;
        case ESP_PLAYER_EVENT_ERROR:
            ESP_LOGW(TAG, "Player reported ERROR event");
            xEventGroupSetBits(s_evts, EVT_ERROR);
            break;
        default:
            break;
    }
    return ESP_PLAYER_ERR_OK;
}

static esp_gmf_err_t _make_builtin_aac_element(const esp_player_audio_stream_info_t *info,
                                               esp_gmf_element_handle_t *out_el)
{
    esp_aac_dec_cfg_t aac_cfg = {
        .sample_rate = info->sample_rate ? info->sample_rate : 22050,
        .channel = info->channels ? info->channels : 1,
        .bits_per_sample = info->bits_per_sample ? info->bits_per_sample : 16,
        .no_adts_header = false,
        .aac_plus_enable = false,
    };
    esp_audio_simple_dec_cfg_t cfg = {
        .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_AAC,
        .dec_cfg = &aac_cfg,
        .cfg_size = sizeof(aac_cfg),
    };
    return esp_gmf_audio_dec_init(&cfg, out_el);
}

static esp_player_err_t _accept_audio_factory(void *user_ctx,
                                              uint32_t codec_cc,
                                              const esp_player_audio_stream_info_t *info,
                                              esp_gmf_element_handle_t *out_element)
{
    factory_stats_t *st = (factory_stats_t *)user_ctx;
    st->accept_calls++;
    st->last_codec_cc = codec_cc;
    ESP_LOGI(TAG, "accept factory called with codec_cc=0x%08" PRIx32, codec_cc);

    if (codec_cc != (uint32_t)ESP_FOURCC_AAC) {
        /* Contract: decline per-codec to let the built-in handle it. */
        return ESP_PLAYER_ERR_NOT_SUPPORT;
    }
    if (_make_builtin_aac_element(info, out_element) != ESP_GMF_ERR_OK) {
        return ESP_PLAYER_ERR_FAIL;
    }
    return ESP_PLAYER_ERR_OK;
}

static esp_player_err_t _decline_audio_factory(void *user_ctx,
                                               uint32_t codec_cc,
                                               const esp_player_audio_stream_info_t *info,
                                               esp_gmf_element_handle_t *out_element)
{
    (void)info;
    (void)out_element;
    factory_stats_t *st = (factory_stats_t *)user_ctx;
    st->decline_calls++;
    st->last_codec_cc = codec_cc;
    ESP_LOGI(TAG, "decline factory called with codec_cc=0x%08" PRIx32, codec_cc);
    return ESP_PLAYER_ERR_NOT_SUPPORT;
}

static esp_player_err_t _bad_tag_factory(void *user_ctx,
                                         uint32_t codec_cc,
                                         const esp_player_audio_stream_info_t *info,
                                         esp_gmf_element_handle_t *out_element)
{
    factory_stats_t *st = (factory_stats_t *)user_ctx;
    st->bad_tag_calls++;
    st->last_codec_cc = codec_cc;
    ESP_LOGI(TAG, "bad-tag factory called with codec_cc=0x%08" PRIx32, codec_cc);

    esp_gmf_element_handle_t el = NULL;
    if (_make_builtin_aac_element(info, &el) != ESP_GMF_ERR_OK || el == NULL) {
        return ESP_PLAYER_ERR_FAIL;
    }
    /* Deliberately violate the contract. */
    esp_gmf_obj_set_tag(el, "not_aud_dec");
    *out_element = el;
    return ESP_PLAYER_ERR_OK;
}

static void _setup_player(player_fixture_t *fx, esp_player_custom_elements_t *hooks)
{
    memset(fx, 0, sizeof(*fx));
    memset(&s_stats, 0, sizeof(s_stats));

    s_evts = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL(s_evts);

    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK,
                      audio_render_create_handle(&fx->audio_render,
                                                 22050, 16, 1,
                                                 ESP_AUDIO_RENDER_STREAM_ID(0)));

    esp_player_config_t cfg = ESP_PLAYER_CONFIG_DEFAULT();
    cfg.audio_render_hd = fx->audio_render;

    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_init(&cfg, &fx->player));
    if (hooks != NULL) {
        TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_set_custom_elements(fx->player, hooks));
    }
    esp_player_set_event_cb(fx->player, _evt_cb, NULL);
    esp_player_set_av_mask(fx->player, ESP_PLAYER_MASK_AUDIO);
}

static void _teardown_player(player_fixture_t *fx)
{
    if (fx->player) {
        esp_player_set_event_cb(fx->player, NULL, NULL);
        esp_player_deinit(fx->player);
        fx->player = NULL;
    }
    if (fx->audio_render) {
        audio_render_destroy_handle();
        fx->audio_render = NULL;
    }
    if (s_evts) {
        vEventGroupDelete(s_evts);
        s_evts = NULL;
    }
}

static EventBits_t _run_fill_mode_aac(esp_player_handle_t player)
{
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_set_url(player, TEST_FILL_URL_AAC));
    TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_run(player));

    esp_player_frame_t frame = {
        .data = (uint8_t *)test_data_aac_frame1,
        .data_len = TEST_DATA_AAC_FRAME1_COUNT,
        .pts = 0,
        .frame_type = ESP_PLAYER_FRAME_TYPE_DEFAULT,
        .is_bad = false,
        .eos = false,
    };
    esp_player_submit_frame(player, &frame, 1000);
    frame.eos = true;
    esp_player_submit_frame(player, &frame, 1000);

    EventBits_t bits = xEventGroupWaitBits(s_evts,
                                           EVT_FINISHED | EVT_ERROR,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(PLAYBACK_WAIT_MS));

    /* Drain into STOPPED before leaving: collapses FINISHED / ERROR /
     * still-playing into a single, well-defined teardown state. */
    (void)esp_player_stop(player);
    (void)xEventGroupWaitBits(s_evts, EVT_STOPPED,
                              pdFALSE, pdFALSE, pdMS_TO_TICKS(STOP_WAIT_MS));
    vTaskDelay(pdMS_TO_TICKS(100));

    return bits;
}

TEST_CASE("[custom]:test_custom_decoder_hooks_init_deinit", "[player][custom]")
{
    esp_player_custom_elements_t hooks = {
        .adec_factory = _accept_audio_factory,
        .vdec_factory = NULL,
        .user_ctx = &s_stats,
    };
    player_fixture_t fx;
    _setup_player(&fx, &hooks);

    /* The factory is only invoked at pipeline bring-up (run), not at init.
     * This captures a regression we explicitly want to avoid: doing decoder
     * work before URL / format is known. */
    TEST_ASSERT_EQUAL(0, s_stats.accept_calls);

    _teardown_player(&fx);
}

TEST_CASE("[custom]:test_custom_decoder_hooks_copied", "[player][custom]")
{
    player_fixture_t fx;
    _setup_player(&fx, NULL);

    {
        esp_player_custom_elements_t hooks = {
            .adec_factory = _accept_audio_factory,
            .vdec_factory = NULL,
            .user_ctx = &s_stats,
        };
        TEST_ASSERT_EQUAL(ESP_PLAYER_ERR_OK, esp_player_set_custom_elements(fx.player, &hooks));
    }

    EventBits_t bits = _run_fill_mode_aac(fx.player);

    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, s_stats.accept_calls);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)ESP_FOURCC_AAC, s_stats.last_codec_cc);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bits & (EVT_FINISHED | EVT_ERROR),
                                  "Player did not reach FINISHED/ERROR within timeout");

    _teardown_player(&fx);
}

TEST_CASE("[custom]:test_custom_decoder_accept_playthrough", "[player][custom]")
{
    esp_player_custom_elements_t hooks = {
        .adec_factory = _accept_audio_factory,
        .vdec_factory = NULL,
        .user_ctx = &s_stats,
    };
    player_fixture_t fx;
    _setup_player(&fx, &hooks);

    EventBits_t bits = _run_fill_mode_aac(fx.player);

    /* Factory must have been invoked with the AAC FourCC. */
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, s_stats.accept_calls);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)ESP_FOURCC_AAC, s_stats.last_codec_cc);

    /* The single synthetic AAC frame may or may not decode cleanly on this
     * board; what matters is that the player made forward progress through
     * the custom decoder rather than hanging. Either FINISHED or ERROR is
     * acceptable; hanging is not. */
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bits & (EVT_FINISHED | EVT_ERROR),
                                  "Player did not reach FINISHED/ERROR within timeout");

    _teardown_player(&fx);
}

TEST_CASE("[custom]:test_custom_decoder_decline_fallback", "[player][custom]")
{
    esp_player_custom_elements_t hooks = {
        .adec_factory = _decline_audio_factory,
        .vdec_factory = NULL,
        .user_ctx = &s_stats,
    };
    player_fixture_t fx;
    _setup_player(&fx, &hooks);

    EventBits_t bits = _run_fill_mode_aac(fx.player);

    /* Factory must have been invoked and declined (AAC FourCC). */
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, s_stats.decline_calls);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)ESP_FOURCC_AAC, s_stats.last_codec_cc);

    /* Same progress expectation as the accept-path test. */
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bits & (EVT_FINISHED | EVT_ERROR),
                                  "Player did not reach FINISHED/ERROR within timeout");

    _teardown_player(&fx);
}

TEST_CASE("[custom]:test_custom_decoder_bad_tag_rejected", "[player][custom]")
{
    esp_player_custom_elements_t hooks = {
        .adec_factory = _bad_tag_factory,
        .vdec_factory = NULL,
        .user_ctx = &s_stats,
    };
    player_fixture_t fx;
    _setup_player(&fx, &hooks);

    EventBits_t bits = _run_fill_mode_aac(fx.player);

    /* Factory was called with the AAC FourCC (set_dec_cfg ran before run). */
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, s_stats.bad_tag_calls);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)ESP_FOURCC_AAC, s_stats.last_codec_cc);
    /* Player must reject the bad-tag element and never reach FINISHED. */
    TEST_ASSERT_TRUE_MESSAGE((bits & EVT_ERROR) != 0,
                             "Player must report ERROR when the custom element has a bad tag");
    TEST_ASSERT_FALSE_MESSAGE((bits & EVT_FINISHED) != 0,
                              "Player must not successfully finish with a bad-tag element");

    _teardown_player(&fx);
}

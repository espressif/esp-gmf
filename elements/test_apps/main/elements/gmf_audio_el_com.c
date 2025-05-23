/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_method_helper.h"
#include "esp_gmf_eq.h"
#include "esp_gmf_fade.h"
#include "esp_gmf_mixer.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_audio_dec.h"
#include "esp_gmf_audio_param.h"
#include "esp_gmf_args_desc.h"
#include "gmf_audio_el_com.h"
#include "esp_audio_enc_default.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec_default.h"

#define PREPARE_METHOD()                                                                      \
    esp_gmf_method_exec_ctx_t exec_ctx    = {};                                               \
    const esp_gmf_method_t   *method_head = NULL;                                             \
    esp_gmf_element_get_method(self, &method_head);                                           \
    esp_gmf_err_t ret = esp_gmf_method_prepare_exec_ctx(method_head, method_name, &exec_ctx); \
    if (ret != ESP_GMF_ERR_OK) {                                                              \
        return ret;                                                                           \
    }

#define SET_METHOD_ARG(arg_name, value) \
    esp_gmf_args_set_value(exec_ctx.method->args_desc, arg_name, exec_ctx.exec_buf, (uint8_t *)&value, sizeof(value));

#define EXEC_METHOD() \
    ret = exec_ctx.method->func(self, exec_ctx.method->args_desc, exec_ctx.exec_buf, exec_ctx.buf_size);

#define GET_METHOD_ARG(arg_name, value) \
    esp_gmf_args_extract_value(exec_ctx.method->args_desc, arg_name, exec_ctx.exec_buf, exec_ctx.buf_size, (uint32_t *)value);

#define RELEASE_METHOD() \
    esp_gmf_method_release_exec_ctx(&exec_ctx);

// Audio effect parameter setting methods
static esp_gmf_err_t audio_el_test_set_eq_filter_para(esp_gmf_element_handle_t self, uint8_t idx, esp_ae_eq_filter_para_t *para)
{
    const char *method_name = AMETHOD(EQ, SET_PARA);
    PREPARE_METHOD();

    SET_METHOD_ARG(AMETHOD_ARG(EQ, SET_PARA, IDX), idx);
    SET_METHOD_ARG(AMETHOD_ARG(EQ, SET_PARA, PARA), *para);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_get_eq_filter_para(esp_gmf_element_handle_t self, uint8_t idx, esp_ae_eq_filter_para_t *para)
{
    const char *method_name = AMETHOD(EQ, GET_PARA);
    PREPARE_METHOD();

    SET_METHOD_ARG(AMETHOD_ARG(EQ, GET_PARA, IDX), idx);
    EXEC_METHOD();
    GET_METHOD_ARG(AMETHOD_ARG(EQ, GET_PARA, PARA), para);
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_enable_eq_filter(esp_gmf_element_handle_t self, uint8_t idx, bool enable)
{
    const char *method_name = AMETHOD(EQ, ENABLE_FILTER);
    PREPARE_METHOD();

    SET_METHOD_ARG(AMETHOD_ARG(EQ, ENABLE_FILTER, IDX), idx);
    SET_METHOD_ARG(AMETHOD_ARG(EQ, ENABLE_FILTER, ENABLE), enable);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_get_encoder_frame_size(esp_gmf_element_handle_t self, uint32_t *in_size, uint32_t *out_size)
{
    const char *method_name = AMETHOD(ENCODER, GET_FRAME_SZ);
    PREPARE_METHOD();
    EXEC_METHOD();
    GET_METHOD_ARG(AMETHOD_ARG(ENCODER, GET_FRAME_SZ, INSIZE), in_size);
    GET_METHOD_ARG(AMETHOD_ARG(ENCODER, GET_FRAME_SZ, OUTSIZE), out_size);
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_set_encoder_bitrate(esp_gmf_element_handle_t self, uint32_t bitrate)
{
    const char *method_name = AMETHOD(ENCODER, SET_BITRATE);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(ENCODER, SET_BITRATE, BITRATE), bitrate);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_get_encoder_bitrate(esp_gmf_element_handle_t self, uint32_t *bitrate)
{
    const char *method_name = AMETHOD(ENCODER, GET_BITRATE);
    PREPARE_METHOD();
    EXEC_METHOD();
    GET_METHOD_ARG(AMETHOD_ARG(ENCODER, GET_BITRATE, BITRATE), bitrate);
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_reconfig_encoder(esp_gmf_element_handle_t self, esp_audio_enc_config_t *cfg)
{
    const char *method_name = AMETHOD(ENCODER, RECONFIG);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(ENCODER, RECONFIG, CFG), *cfg);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_reconfig_encoder_by_sound_info(esp_gmf_element_handle_t self, esp_gmf_info_sound_t *info)
{
    const char *method_name = AMETHOD(ENCODER, RECONFIG_BY_SND_INFO);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(ENCODER, RECONFIG_BY_SND_INFO, INFO), *info);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_reconfig_decoder(esp_gmf_element_handle_t self, esp_audio_simple_dec_cfg_t *cfg)
{
    const char *method_name = AMETHOD(DECODER, RECONFIG);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(DECODER, RECONFIG, CFG), *cfg);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_reconfig_decoder_by_sound_info(esp_gmf_element_handle_t self, esp_gmf_info_sound_t *info)
{
    const char *method_name = AMETHOD(DECODER, RECONFIG_BY_SND_INFO);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(DECODER, RECONFIG_BY_SND_INFO, INFO), *info);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_set_fade_mode(esp_gmf_element_handle_t self, esp_ae_fade_mode_t mode)
{
    const char *method_name = AMETHOD(FADE, SET_MODE);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(FADE, SET_MODE, MODE), mode);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_get_fade_mode(esp_gmf_element_handle_t self, esp_ae_fade_mode_t *mode)
{
    const char *method_name = AMETHOD(FADE, GET_MODE);
    PREPARE_METHOD();
    EXEC_METHOD();
    GET_METHOD_ARG(AMETHOD_ARG(FADE, GET_MODE, MODE), mode);
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_reset_fade_weight(esp_gmf_element_handle_t self)
{
    const char *method_name = AMETHOD(FADE, RESET_WEIGHT);
    PREPARE_METHOD();
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_set_mixer_mode(esp_gmf_element_handle_t self, uint8_t idx, esp_ae_mixer_mode_t mode)
{
    const char *method_name = AMETHOD(MIXER, SET_MODE);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(MIXER, SET_MODE, IDX), idx);
    SET_METHOD_ARG(AMETHOD_ARG(MIXER, SET_MODE, MODE), mode);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_set_mixer_info(esp_gmf_element_handle_t self, uint32_t sample_rate, uint32_t channels, uint32_t bits)
{
    const char *method_name = AMETHOD(MIXER, SET_INFO);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(MIXER, SET_INFO, RATE), sample_rate);
    SET_METHOD_ARG(AMETHOD_ARG(MIXER, SET_INFO, CH), channels);
    SET_METHOD_ARG(AMETHOD_ARG(MIXER, SET_INFO, BITS), bits);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_set_sonic_speed(esp_gmf_element_handle_t self, float speed)
{
    const char *method_name = AMETHOD(SONIC, SET_SPEED);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(SONIC, SET_SPEED, SPEED), speed);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_get_sonic_speed(esp_gmf_element_handle_t self, float *speed)
{
    const char *method_name = AMETHOD(SONIC, GET_SPEED);
    PREPARE_METHOD();
    EXEC_METHOD();
    GET_METHOD_ARG(AMETHOD_ARG(SONIC, GET_SPEED, SPEED), speed);
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_set_sonic_pitch(esp_gmf_element_handle_t self, float pitch)
{
    const char *method_name = AMETHOD(SONIC, SET_PITCH);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(SONIC, SET_PITCH, PITCH), pitch);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_get_sonic_pitch(esp_gmf_element_handle_t self, float *pitch)
{
    const char *method_name = AMETHOD(SONIC, GET_PITCH);
    PREPARE_METHOD();
    EXEC_METHOD();
    GET_METHOD_ARG(AMETHOD_ARG(SONIC, GET_PITCH, PITCH), pitch);
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_set_alc_gain(esp_gmf_element_handle_t self, uint8_t idx, int8_t gain)
{
    const char *method_name = AMETHOD(ALC, SET_GAIN);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(ALC, SET_GAIN, IDX), idx);
    SET_METHOD_ARG(AMETHOD_ARG(ALC, SET_GAIN, GAIN), gain);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_get_alc_gain(esp_gmf_element_handle_t self, uint8_t idx, int8_t *gain)
{
    const char *method_name = AMETHOD(ALC, GET_GAIN);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(ALC, GET_GAIN, IDX), idx);
    EXEC_METHOD();
    GET_METHOD_ARG(AMETHOD_ARG(ALC, GET_GAIN, GAIN), gain);
    RELEASE_METHOD();
    return ret;
}

void encoder_config_callback(esp_gmf_element_handle_t el, void *ctx)
{
    audio_el_res_t *res = (audio_el_res_t *)ctx;
    esp_audio_enc_config_t enc_cfg = {0};
    uint32_t bitrate = 0;
    esp_gmf_event_state_t event_state = 0;
    esp_gmf_element_get_state(el, &event_state);
    uint32_t in_size = 0, out_size = 0;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    if (event_state >= ESP_GMF_EVENT_STATE_OPENING) {
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_encoder_frame_size(el, &in_size, &out_size));
        ret = esp_gmf_audio_enc_reconfig(el, &enc_cfg);
        esp_gmf_element_get_state(el, &event_state);
        if (event_state >= ESP_GMF_EVENT_STATE_OPENING) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_FAIL, ret);
        }
        ret = esp_gmf_audio_enc_reconfig_by_sound_info(el, &res->in_inst[0].src_info);
        esp_gmf_element_get_state(el, &event_state);
        if (event_state >= ESP_GMF_EVENT_STATE_OPENING) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_FAIL, ret);
        }
        return;
    }
    // Test AMRNB encoder configuration
    esp_amrnb_enc_config_t new_amrnb_cfg = {
        .sample_rate = 8000,
        .channel = 1,
        .bits_per_sample = 16,
        .dtx_enable = true,
        .bitrate_mode = ESP_AMRNB_ENC_BITRATE_MR122,
        .no_file_header = false};
    enc_cfg.type = ESP_AUDIO_TYPE_AMRNB;
    enc_cfg.cfg = &new_amrnb_cfg;
    enc_cfg.cfg_sz = sizeof(esp_amrnb_enc_config_t);
    ret = audio_el_test_reconfig_encoder(el, &enc_cfg);
    esp_gmf_element_get_state(el, &event_state);
    if (event_state < ESP_GMF_EVENT_STATE_OPENING) {
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);
        // Verify AMRNB configuration
        esp_audio_enc_config_t *get_cfg = OBJ_GET_CFG(el);
        TEST_ASSERT_EQUAL(ESP_AUDIO_TYPE_AMRNB, get_cfg->type);
        esp_amrnb_enc_config_t *amrnb_cfg = (esp_amrnb_enc_config_t *)get_cfg->cfg;
        TEST_ASSERT_EQUAL(new_amrnb_cfg.sample_rate, amrnb_cfg->sample_rate);
        TEST_ASSERT_EQUAL(new_amrnb_cfg.channel, amrnb_cfg->channel);
        TEST_ASSERT_EQUAL(new_amrnb_cfg.bits_per_sample, amrnb_cfg->bits_per_sample);
        TEST_ASSERT_EQUAL(new_amrnb_cfg.dtx_enable, amrnb_cfg->dtx_enable);
        TEST_ASSERT_EQUAL(new_amrnb_cfg.bitrate_mode, amrnb_cfg->bitrate_mode);
        TEST_ASSERT_EQUAL(new_amrnb_cfg.no_file_header, amrnb_cfg->no_file_header);
        // Test bitrate interface
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_encoder_bitrate(el, 12200));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_encoder_bitrate(el, &bitrate));
        TEST_ASSERT_EQUAL(12200, bitrate);
    }
    // Test reconfiguration using sound info
    ret = audio_el_test_reconfig_encoder_by_sound_info(el, &res->in_inst[0].src_info);
    esp_gmf_element_get_state(el, &event_state);
    if (event_state < ESP_GMF_EVENT_STATE_OPENING) {
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);
        // Verify AMRNB configuration
        esp_audio_enc_config_t *get_cfg = OBJ_GET_CFG(el);
        TEST_ASSERT_EQUAL(res->in_inst[0].src_info.format_id, get_cfg->type);
    }
    // Verify frame size after reconfiguration
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_encoder_frame_size(el, &in_size, &out_size));
}

void decoder_config_callback(esp_gmf_element_handle_t el, void *ctx)
{
    audio_el_res_t *res = (audio_el_res_t *)ctx;
    esp_audio_simple_dec_cfg_t dec_cfg = {0};
    esp_gmf_event_state_t event_state = 0;
    esp_gmf_element_get_state(el, &event_state);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    if (event_state >= ESP_GMF_EVENT_STATE_OPENING) {
        ret = audio_el_test_reconfig_decoder(el, &dec_cfg);
        esp_gmf_element_get_state(el, &event_state);
        if (event_state >= ESP_GMF_EVENT_STATE_OPENING) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_FAIL, ret);
        }
        ret = audio_el_test_reconfig_decoder_by_sound_info(el, &res->in_inst[0].src_info);
        esp_gmf_element_get_state(el, &event_state);
        if (event_state >= ESP_GMF_EVENT_STATE_OPENING) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_FAIL, ret);
        }
        return;
    }
    // Test LC3 decoder configuration
    esp_lc3_dec_cfg_t new_lc3_cfg = {
        .sample_rate = 48000,
        .channel = 2,
        .bits_per_sample = 16,
        .frame_dms = 100,
        .is_cbr = 1,
        .len_prefixed = 1,
        .enable_plc = 1,
        .nbyte = 120};
    dec_cfg.dec_type = ESP_AUDIO_TYPE_LC3;
    dec_cfg.dec_cfg = &new_lc3_cfg;
    dec_cfg.cfg_size = sizeof(esp_lc3_dec_cfg_t);
    ret = audio_el_test_reconfig_decoder(el, &dec_cfg);
    esp_gmf_element_get_state(el, &event_state);
    if (event_state < ESP_GMF_EVENT_STATE_OPENING) {
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);
        // Verify LC3 configuration
        esp_audio_simple_dec_cfg_t *get_cfg = OBJ_GET_CFG(el);
        TEST_ASSERT_EQUAL(ESP_AUDIO_TYPE_LC3, get_cfg->dec_type);
        esp_lc3_dec_cfg_t *lc3_cfg = (esp_lc3_dec_cfg_t *)get_cfg->dec_cfg;
        TEST_ASSERT_EQUAL(new_lc3_cfg.sample_rate, lc3_cfg->sample_rate);
        TEST_ASSERT_EQUAL(new_lc3_cfg.channel, lc3_cfg->channel);
        TEST_ASSERT_EQUAL(new_lc3_cfg.bits_per_sample, lc3_cfg->bits_per_sample);
        TEST_ASSERT_EQUAL(new_lc3_cfg.frame_dms, lc3_cfg->frame_dms);
        TEST_ASSERT_EQUAL(new_lc3_cfg.is_cbr, lc3_cfg->is_cbr);
        TEST_ASSERT_EQUAL(new_lc3_cfg.len_prefixed, lc3_cfg->len_prefixed);
        TEST_ASSERT_EQUAL(new_lc3_cfg.enable_plc, lc3_cfg->enable_plc);
    }
    // Test FLAC decoder configuration using sound info
    esp_gmf_info_sound_t dec_info = {
        .format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC,
        .sample_rates = 48000,
        .channels = 2,
        .bits = 16,
        .bitrate = 0};
    ret = audio_el_test_reconfig_decoder_by_sound_info(el, &dec_info);
    esp_gmf_element_get_state(el, &event_state);
    if (event_state < ESP_GMF_EVENT_STATE_OPENING) {
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);
        esp_audio_simple_dec_cfg_t *get_cfg = OBJ_GET_CFG(el);
        TEST_ASSERT_EQUAL(ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC, get_cfg->dec_type);
    }
}

void eq_config_callback(esp_gmf_element_handle_t self, void *ctx)
{
    esp_ae_eq_filter_para_t filter_para[] = {
        {.fc = 100,
         .gain = 6.0f,
         .q = 1.0f,
         .filter_type = ESP_AE_EQ_FILTER_LOW_SHELF},
        {.fc = 1000,
         .gain = -3.0f,
         .q = 2.0f,
         .filter_type = ESP_AE_EQ_FILTER_PEAK},
        {.fc = 10000,
         .gain = 3.0f,
         .q = 1.0f,
         .filter_type = ESP_AE_EQ_FILTER_HIGH_SHELF}};
    esp_ae_eq_filter_para_t read_para = {0};
    for (int i = 0; i < sizeof(filter_para) / sizeof(filter_para[0]); i++) {
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_eq_filter_para(self, i, &filter_para[i]));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_eq_filter_para(self, i, &read_para));
        TEST_ASSERT_EQUAL(filter_para[i].fc, read_para.fc);
        TEST_ASSERT_EQUAL_FLOAT(filter_para[i].gain, read_para.gain);
        TEST_ASSERT_EQUAL_FLOAT(filter_para[i].q, read_para.q);
        TEST_ASSERT_EQUAL(filter_para[i].filter_type, read_para.filter_type);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_enable_eq_filter(self, i, true));
    }
}

void fade_config_callback(esp_gmf_element_handle_t self, void *ctx)
{
    esp_ae_fade_mode_t mode = 0;
    // Test fade mode setting and getting
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_fade_mode(self, ESP_AE_FADE_MODE_FADE_IN));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_fade_mode(self, &mode));
    TEST_ASSERT_EQUAL(ESP_AE_FADE_MODE_FADE_IN, mode);
    // Test fade weight reset
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_reset_fade_weight(self));
}

void mixer_config_callback(esp_gmf_element_handle_t self, void *ctx)
{
    // Test mixer mode setting for each channel
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_mixer_mode(self, 0, ESP_AE_MIXER_MODE_FADE_UPWARD));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_mixer_mode(self, 1, ESP_AE_MIXER_MODE_FADE_DOWNWARD));
    // Test mixer info setting
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_mixer_info(self, 48000, 2, 16));
}

void sonic_config_callback(esp_gmf_element_handle_t self, void *ctx)
{
    float speed = 0.0f;
    float pitch = 0.0f;
    // Test speed setting and getting
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_sonic_speed(self, 1.5f));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_sonic_speed(self, &speed));
    TEST_ASSERT_EQUAL_FLOAT(1.5f, speed);
    // Test pitch setting and getting
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_sonic_pitch(self, 1.2f));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_sonic_pitch(self, &pitch));
    TEST_ASSERT_EQUAL_FLOAT(1.2f, pitch);
}

void alc_config_callback(esp_gmf_element_handle_t self, void *ctx)
{
    audio_el_res_t *res = (audio_el_res_t *)ctx;
    int8_t gain = 0;
    for (int i = 0; i < res->in_inst[0].src_info.channels; i++) {
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_alc_gain(self, i, 2));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_alc_gain(self, i, &gain));
        TEST_ASSERT_EQUAL(2, gain);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_alc_gain(self, i, 1));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_alc_gain(self, i, &gain));
        TEST_ASSERT_EQUAL(1, gain);
    }
}

void bit_cvt_config_callback(esp_gmf_element_handle_t self, void *ctx)
{
    audio_el_res_t *res = (audio_el_res_t *)ctx;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_bits(self, res->out_inst[0].out_info.bits));
}

void ch_cvt_config_callback(esp_gmf_element_handle_t self, void *ctx)
{
    audio_el_res_t *res = (audio_el_res_t *)ctx;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_ch(self, res->out_inst[0].out_info.channels));
}

void rate_cvt_config_callback(esp_gmf_element_handle_t self, void *ctx)
{
    audio_el_res_t *res = (audio_el_res_t *)ctx;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_rate(self, res->out_inst[0].out_info.sample_rates));
}

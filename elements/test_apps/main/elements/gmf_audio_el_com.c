/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_method_helper.h"
#include "esp_gmf_eq.h"
#include "esp_gmf_fade.h"
#include "esp_gmf_mixer.h"
#include "esp_gmf_drc.h"
#include "esp_gmf_mbc.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_audio_dec.h"
#include "esp_gmf_audio_param.h"
#include "esp_gmf_args_desc.h"
#include "gmf_audio_el_com.h"
#include "esp_audio_enc_default.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_ch_cvt.h"
#include "decoder/impl/esp_pcm_dec.h"

#define PREPARE_METHOD()                                                                      \
    esp_gmf_method_exec_ctx_t exec_ctx    = {};                                               \
    const esp_gmf_method_t   *method_head = NULL;                                             \
    esp_gmf_element_get_method(self, &method_head);                                           \
    esp_gmf_err_t ret = esp_gmf_method_prepare_exec_ctx(method_head, method_name, &exec_ctx); \
    if (ret != ESP_GMF_ERR_OK) {                                                              \
        return ret; \
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

static esp_gmf_err_t audio_el_test_reset_fade(esp_gmf_element_handle_t self)
{
    const char *method_name = AMETHOD(FADE, RESET);
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

static esp_gmf_err_t audio_el_test_set_drc_attack(esp_gmf_element_handle_t self, uint16_t attack)
{
    const char *method_name = AMETHOD(DRC, SET_ATTACK);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(DRC, SET_ATTACK, ATTACK), attack);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_get_drc_attack(esp_gmf_element_handle_t self, uint16_t *attack)
{
    const char *method_name = AMETHOD(DRC, GET_ATTACK);
    PREPARE_METHOD();
    EXEC_METHOD();
    GET_METHOD_ARG(AMETHOD_ARG(DRC, GET_ATTACK, ATTACK), attack);
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_set_drc_release(esp_gmf_element_handle_t self, uint16_t release)
{
    const char *method_name = AMETHOD(DRC, SET_RELEASE);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(DRC, SET_RELEASE, RELEASE), release);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_get_drc_release(esp_gmf_element_handle_t self, uint16_t *release)
{
    const char *method_name = AMETHOD(DRC, GET_RELEASE);
    PREPARE_METHOD();
    EXEC_METHOD();
    GET_METHOD_ARG(AMETHOD_ARG(DRC, GET_RELEASE, RELEASE), release);
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_set_drc_hold(esp_gmf_element_handle_t self, uint16_t hold)
{
    const char *method_name = AMETHOD(DRC, SET_HOLD);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(DRC, SET_HOLD, HOLD), hold);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_get_drc_hold(esp_gmf_element_handle_t self, uint16_t *hold)
{
    const char *method_name = AMETHOD(DRC, GET_HOLD);
    PREPARE_METHOD();
    EXEC_METHOD();
    GET_METHOD_ARG(AMETHOD_ARG(DRC, GET_HOLD, HOLD), hold);
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_set_drc_makeup(esp_gmf_element_handle_t self, float makeup)
{
    const char *method_name = AMETHOD(DRC, SET_MAKEUP);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(DRC, SET_MAKEUP, MAKEUP), makeup);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_get_drc_makeup(esp_gmf_element_handle_t self, float *makeup)
{
    const char *method_name = AMETHOD(DRC, GET_MAKEUP);
    PREPARE_METHOD();
    EXEC_METHOD();
    GET_METHOD_ARG(AMETHOD_ARG(DRC, GET_MAKEUP, MAKEUP), makeup);
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_set_drc_knee(esp_gmf_element_handle_t self, float knee)
{
    const char *method_name = AMETHOD(DRC, SET_KNEE);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(DRC, SET_KNEE, KNEE), knee);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_get_drc_knee(esp_gmf_element_handle_t self, float *knee)
{
    const char *method_name = AMETHOD(DRC, GET_KNEE);
    PREPARE_METHOD();
    EXEC_METHOD();
    GET_METHOD_ARG(AMETHOD_ARG(DRC, GET_KNEE, KNEE), knee);
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_set_drc_points(esp_gmf_element_handle_t self, esp_ae_drc_curve_point *points, uint8_t num)
{
    const char *method_name = AMETHOD(DRC, SET_POINTS);
    PREPARE_METHOD();
    esp_ae_drc_curve_point *points_ptr = points;
    uint8_t point_num = num;
    SET_METHOD_ARG(AMETHOD_ARG(DRC, SET_POINTS, POINTS), points_ptr);
    SET_METHOD_ARG(AMETHOD_ARG(DRC, SET_POINTS, POINT_NUM), point_num);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_get_drc_point_num(esp_gmf_element_handle_t self, uint8_t *num)
{
    const char *method_name = AMETHOD(DRC, GET_POINT_NUM);
    PREPARE_METHOD();
    EXEC_METHOD();
    GET_METHOD_ARG(AMETHOD_ARG(DRC, GET_POINT_NUM, POINT_NUM), num);
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_get_drc_points(esp_gmf_element_handle_t self, esp_ae_drc_curve_point *points, uint8_t num)
{
    const char *method_name = AMETHOD(DRC, GET_POINTS);
    PREPARE_METHOD();
    esp_ae_drc_curve_point *points_ptr = points;
    uint8_t point_num = num;
    SET_METHOD_ARG(AMETHOD_ARG(DRC, GET_POINTS, POINTS), points_ptr);
    SET_METHOD_ARG(AMETHOD_ARG(DRC, GET_POINTS, POINT_NUM), point_num);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_set_mbc_para(esp_gmf_element_handle_t self, uint8_t idx, esp_ae_mbc_para_t *para)
{
    const char *method_name = AMETHOD(MBC, SET_PARA);
    PREPARE_METHOD();
    esp_ae_mbc_para_t para_val = *para;
    SET_METHOD_ARG(AMETHOD_ARG(MBC, SET_PARA, IDX), idx);
    SET_METHOD_ARG(AMETHOD_ARG(MBC, SET_PARA, PARA), para_val);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_get_mbc_para(esp_gmf_element_handle_t self, uint8_t idx, esp_ae_mbc_para_t *para)
{
    const char *method_name = AMETHOD(MBC, GET_PARA);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(MBC, GET_PARA, IDX), idx);
    EXEC_METHOD();
    GET_METHOD_ARG(AMETHOD_ARG(MBC, GET_PARA, PARA), para);
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_set_mbc_fc(esp_gmf_element_handle_t self, uint8_t idx, uint32_t fc)
{
    const char *method_name = AMETHOD(MBC, SET_FC);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(MBC, SET_FC, IDX), idx);
    SET_METHOD_ARG(AMETHOD_ARG(MBC, SET_FC, FC), fc);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_get_mbc_fc(esp_gmf_element_handle_t self, uint8_t idx, uint32_t *fc)
{
    const char *method_name = AMETHOD(MBC, GET_FC);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(MBC, GET_FC, IDX), idx);
    EXEC_METHOD();
    GET_METHOD_ARG(AMETHOD_ARG(MBC, GET_FC, FC), fc);
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_set_mbc_solo(esp_gmf_element_handle_t self, uint8_t idx, bool enable)
{
    const char *method_name = AMETHOD(MBC, SET_SOLO);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(MBC, SET_SOLO, IDX), idx);
    SET_METHOD_ARG(AMETHOD_ARG(MBC, SET_SOLO, ENABLE), enable);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_get_mbc_solo(esp_gmf_element_handle_t self, uint8_t idx, bool *enable)
{
    const char *method_name = AMETHOD(MBC, GET_SOLO);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(MBC, GET_SOLO, IDX), idx);
    EXEC_METHOD();
    uint8_t enable_u8 = 0;
    GET_METHOD_ARG(AMETHOD_ARG(MBC, GET_SOLO, ENABLE), &enable_u8);
    *enable = (enable_u8 != 0);
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_set_mbc_bypass(esp_gmf_element_handle_t self, uint8_t idx, bool enable)
{
    const char *method_name = AMETHOD(MBC, SET_BYPASS);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(MBC, SET_BYPASS, IDX), idx);
    SET_METHOD_ARG(AMETHOD_ARG(MBC, SET_BYPASS, ENABLE), enable);
    EXEC_METHOD();
    RELEASE_METHOD();
    return ret;
}

static esp_gmf_err_t audio_el_test_get_mbc_bypass(esp_gmf_element_handle_t self, uint8_t idx, bool *enable)
{
    const char *method_name = AMETHOD(MBC, GET_BYPASS);
    PREPARE_METHOD();
    SET_METHOD_ARG(AMETHOD_ARG(MBC, GET_BYPASS, IDX), idx);
    EXEC_METHOD();
    uint8_t enable_u8 = 0;
    GET_METHOD_ARG(AMETHOD_ARG(MBC, GET_BYPASS, ENABLE), &enable_u8);
    *enable = (enable_u8 != 0);
    RELEASE_METHOD();
    return ret;
}

void encoder_config_callback(esp_gmf_element_handle_t el, void *ctx)
{
    audio_el_res_t *res = (audio_el_res_t *)ctx;
    esp_gmf_event_state_t event_state = 0;
    esp_gmf_element_get_state(el, &event_state);
    uint32_t bitrate_read = 0;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    uint32_t set_bitrate = res->is_first_open == true ? 70000 : 80000;
    if (res->in_inst[0].src_info.format_id == ESP_AUDIO_TYPE_AMRNB) {
        set_bitrate = res->is_first_open == true ? 5900 : 6700;
    } else if (res->in_inst[0].src_info.format_id == ESP_AUDIO_TYPE_AMRWB) {
        set_bitrate = res->is_first_open == true ? 12650 : 14250;
    }
    if (event_state < ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            ret = audio_el_test_reconfig_encoder_by_sound_info(el, &res->in_inst[0].src_info);
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);
            esp_audio_enc_config_t *get_cfg = OBJ_GET_CFG(el);
            TEST_ASSERT_EQUAL(res->in_inst[0].src_info.format_id, get_cfg->type);
            if (get_cfg->type == ESP_AUDIO_TYPE_AMRNB || get_cfg->type == ESP_AUDIO_TYPE_AMRWB) {
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_FAIL, audio_el_test_set_encoder_bitrate(el, 60000));
            }
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_encoder_bitrate(el, set_bitrate));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_encoder_bitrate(el, &bitrate_read));
            if (get_cfg->type == ESP_AUDIO_TYPE_AAC || get_cfg->type == ESP_AUDIO_TYPE_LC3 ||
                get_cfg->type == ESP_AUDIO_TYPE_OPUS || get_cfg->type == ESP_AUDIO_TYPE_AMRNB ||
                get_cfg->type == ESP_AUDIO_TYPE_AMRWB) {
                uint32_t diff = (set_bitrate > bitrate_read) ? (set_bitrate - bitrate_read) : (bitrate_read - set_bitrate);
                TEST_ASSERT_LESS_THAN(1000, diff);
            }
            res->is_do_open_set = true;
        } else if (res->is_first_open == false) {
            if (res->is_do_open_set == true) {
                esp_audio_enc_config_t *get_cfg = OBJ_GET_CFG(el);
                TEST_ASSERT_EQUAL(res->in_inst[0].src_info.format_id, get_cfg->type);
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_encoder_bitrate(el, &bitrate_read));
                if (get_cfg->type == ESP_AUDIO_TYPE_AAC || get_cfg->type == ESP_AUDIO_TYPE_LC3 ||
                    get_cfg->type == ESP_AUDIO_TYPE_OPUS || get_cfg->type == ESP_AUDIO_TYPE_AMRNB ||
                    get_cfg->type == ESP_AUDIO_TYPE_AMRWB) {
                    uint32_t diff = (set_bitrate > bitrate_read) ? (set_bitrate - bitrate_read) : (bitrate_read - set_bitrate);
                    TEST_ASSERT_LESS_THAN(1000, diff);
                }
            }
        }
    } else if (event_state >= ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            if (res->is_do_open_set == true) {
                esp_audio_enc_config_t *get_cfg = OBJ_GET_CFG(el);
                TEST_ASSERT_EQUAL(res->in_inst[0].src_info.format_id, get_cfg->type);
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_encoder_bitrate(el, &bitrate_read));
                if (get_cfg->type == ESP_AUDIO_TYPE_AAC || get_cfg->type == ESP_AUDIO_TYPE_LC3 ||
                    get_cfg->type == ESP_AUDIO_TYPE_OPUS || get_cfg->type == ESP_AUDIO_TYPE_AMRNB ||
                    get_cfg->type == ESP_AUDIO_TYPE_AMRWB) {
                    uint32_t diff = (set_bitrate > bitrate_read) ? (set_bitrate - bitrate_read) : (bitrate_read - set_bitrate);
                    TEST_ASSERT_LESS_THAN(1000, diff);
                }
                uint32_t in_size = 0;
                uint32_t out_size = 0;
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_encoder_frame_size(el, &in_size, &out_size));
                TEST_ASSERT_EQUAL(ESP_GMF_ELEMENT_GET(el)->in_attr.data_size, in_size);
                TEST_ASSERT_EQUAL(ESP_GMF_ELEMENT_GET(el)->out_attr.data_size, out_size);
                esp_audio_enc_config_t enc_cfg = {0};
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_FAIL, audio_el_test_reconfig_encoder(el, &enc_cfg));
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_FAIL, audio_el_test_reconfig_encoder_by_sound_info(el, &res->in_inst[0].src_info));
            }
            res->is_first_open = false;
        } else if (res->is_first_open == false) {
            esp_audio_enc_config_t *get_cfg = OBJ_GET_CFG(el);
            if (get_cfg->type == ESP_AUDIO_TYPE_AMRNB || get_cfg->type == ESP_AUDIO_TYPE_AMRWB) {
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_FAIL, audio_el_test_set_encoder_bitrate(el, 60000));
            }
            if (audio_el_test_set_encoder_bitrate(el, set_bitrate) == ESP_GMF_ERR_OK &&
                audio_el_test_get_encoder_bitrate(el, &bitrate_read) == ESP_GMF_ERR_OK) {
                if (get_cfg->type == ESP_AUDIO_TYPE_AAC || get_cfg->type == ESP_AUDIO_TYPE_LC3 ||
                    get_cfg->type == ESP_AUDIO_TYPE_OPUS || get_cfg->type == ESP_AUDIO_TYPE_AMRNB ||
                    get_cfg->type == ESP_AUDIO_TYPE_AMRWB) {
                    uint32_t diff = (set_bitrate > bitrate_read) ? (set_bitrate - bitrate_read) : (bitrate_read - set_bitrate);
                    TEST_ASSERT_LESS_THAN(1000, diff);
                }
            }
            uint32_t in_size = 0;
            uint32_t out_size = 0;
            if (audio_el_test_get_encoder_frame_size(el, &in_size, &out_size) == ESP_GMF_ERR_OK) {
                TEST_ASSERT_EQUAL(ESP_GMF_ELEMENT_GET(el)->in_attr.data_size, in_size);
                TEST_ASSERT_EQUAL(ESP_GMF_ELEMENT_GET(el)->out_attr.data_size, out_size);
            }
        }
    }
}

void decoder_config_callback(esp_gmf_element_handle_t el, void *ctx)
{
    audio_el_res_t *res = (audio_el_res_t *)ctx;
    esp_gmf_event_state_t event_state = 0;
    esp_gmf_element_get_state(el, &event_state);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_info_sound_t src_info = {
        .format_id = ESP_AUDIO_TYPE_PCM,
        .sample_rates = res->in_inst[0].src_info.sample_rates,
        .channels = res->in_inst[0].src_info.channels,
        .bits = res->in_inst[0].src_info.bits,
    };
    if (event_state < ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            ret = audio_el_test_reconfig_decoder_by_sound_info(el, &res->in_inst[0].src_info);
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);
            esp_audio_simple_dec_cfg_t *get_cfg = OBJ_GET_CFG(el);
            TEST_ASSERT_EQUAL(res->in_inst[0].src_info.format_id, get_cfg->dec_type);
            res->is_do_open_set = true;
        } else if (res->is_first_open == false) {
            esp_audio_simple_dec_cfg_t *get_cfg = NULL;
            if (res->is_do_open_set == true) {
                get_cfg = OBJ_GET_CFG(el);
                TEST_ASSERT_EQUAL(ESP_AUDIO_TYPE_PCM, get_cfg->dec_type);
            }
            esp_pcm_dec_cfg_t pcm_cfg = {
                .sample_rate = 8000,
                .channel = 1,
                .bits_per_sample = 16,
            };
            esp_audio_simple_dec_cfg_t dec_cfg = {
                .dec_type = ESP_AUDIO_TYPE_PCM,
                .dec_cfg = &pcm_cfg,
                .cfg_size = sizeof(esp_pcm_dec_cfg_t),
            };
            audio_el_test_reconfig_decoder(el, &dec_cfg);
            get_cfg = OBJ_GET_CFG(el);
            esp_pcm_dec_cfg_t *get_pcm_cfg = (esp_pcm_dec_cfg_t *)get_cfg->dec_cfg;
            TEST_ASSERT_EQUAL(ESP_AUDIO_TYPE_PCM, get_cfg->dec_type);
            TEST_ASSERT_EQUAL(8000, get_pcm_cfg->sample_rate);
            TEST_ASSERT_EQUAL(1, get_pcm_cfg->channel);
            TEST_ASSERT_EQUAL(16, get_pcm_cfg->bits_per_sample);
        }
    } else if (event_state >= ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            if (res->is_do_open_set == true) {
                esp_audio_simple_dec_cfg_t *get_cfg = OBJ_GET_CFG(el);
                TEST_ASSERT_EQUAL(res->in_inst[0].src_info.format_id, get_cfg->dec_type);
            }
            res->is_first_open = false;
        } else if (res->is_first_open == false) {
            ret = audio_el_test_reconfig_decoder_by_sound_info(el, &src_info);
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);
        }
    }
}

void eq_config_callback(esp_gmf_element_handle_t self, void *ctx)
{
    audio_el_res_t *res = (audio_el_res_t *)ctx;
    esp_gmf_event_state_t event_state = 0;
    esp_gmf_element_get_state(self, &event_state);
    esp_ae_eq_filter_para_t filter_para[3] = {0};
    esp_ae_eq_filter_para_t read_para = {0};
    if (res->is_first_open == true) {
        esp_ae_eq_filter_para_t para[] = {
            {.fc = 100, .gain = 6.0f, .q = 1.0f, .filter_type = ESP_AE_EQ_FILTER_LOW_SHELF},
            {.fc = 1000, .gain = -3.0f, .q = 2.0f, .filter_type = ESP_AE_EQ_FILTER_PEAK},
            {.fc = 10000, .gain = 3.0f, .q = 1.0f, .filter_type = ESP_AE_EQ_FILTER_HIGH_SHELF}};
        memcpy(filter_para, para, sizeof(filter_para));
    } else {
        esp_ae_eq_filter_para_t para[] = {
            {.fc = 200, .gain = 6.0f, .q = 1.0f, .filter_type = ESP_AE_EQ_FILTER_LOW_SHELF},
            {.fc = 2000, .gain = -3.0f, .q = 2.0f, .filter_type = ESP_AE_EQ_FILTER_PEAK},
            {.fc = 4000, .gain = 3.0f, .q = 1.0f, .filter_type = ESP_AE_EQ_FILTER_HIGH_SHELF}};
        memcpy(filter_para, para, sizeof(filter_para));
    }
    if (event_state < ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            for (int i = 0; i < sizeof(filter_para) / sizeof(filter_para[0]); i++) {
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_eq_filter_para(self, i, &filter_para[i]));
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_enable_eq_filter(self, i, true));
            }
            res->is_do_open_set = true;
        } else if (res->is_first_open == false) {
            for (int i = 0; i < sizeof(filter_para) / sizeof(filter_para[0]); i++) {
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_eq_filter_para(self, i, &read_para));
                TEST_ASSERT_EQUAL(filter_para[i].fc, read_para.fc);
                TEST_ASSERT_EQUAL_FLOAT(filter_para[i].gain, read_para.gain);
                TEST_ASSERT_EQUAL_FLOAT(filter_para[i].q, read_para.q);
                TEST_ASSERT_EQUAL(filter_para[i].filter_type, read_para.filter_type);
            }
        }
    } else if (event_state >= ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            if (res->is_do_open_set == true) {
                for (int i = 0; i < sizeof(filter_para) / sizeof(filter_para[0]); i++) {
                    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_eq_filter_para(self, i, &read_para));
                    TEST_ASSERT_EQUAL(filter_para[i].fc, read_para.fc);
                    TEST_ASSERT_EQUAL_FLOAT(filter_para[i].gain, read_para.gain);
                    TEST_ASSERT_EQUAL_FLOAT(filter_para[i].q, read_para.q);
                    TEST_ASSERT_EQUAL(filter_para[i].filter_type, read_para.filter_type);
                }
            }
            res->is_first_open = false;
        } else if (res->is_first_open == false) {
            for (int i = 0; i < sizeof(filter_para) / sizeof(filter_para[0]); i++) {
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_eq_filter_para(self, i, &filter_para[i]));
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_enable_eq_filter(self, i, true));
            }
        }
    }
}

void fade_config_callback(esp_gmf_element_handle_t self, void *ctx)
{
    audio_el_res_t *res = (audio_el_res_t *)ctx;
    esp_gmf_event_state_t event_state = 0;
    esp_gmf_element_get_state(self, &event_state);
    esp_ae_fade_mode_t set_mode = res->is_first_open == true ? ESP_AE_FADE_MODE_FADE_OUT : ESP_AE_FADE_MODE_FADE_IN;
    esp_ae_fade_mode_t mode_read = 0;
    if (event_state < ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_fade_mode(self, set_mode));
            res->is_do_open_set = true;
        } else if (res->is_first_open == false) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_fade_mode(self, &mode_read));
            TEST_ASSERT_EQUAL(set_mode, mode_read);
        }
    } else if (event_state >= ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            if (res->is_do_open_set == true) {
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_fade_mode(self, &mode_read));
                TEST_ASSERT_EQUAL(set_mode, mode_read);
            }
            res->is_first_open = false;
        } else if (res->is_first_open == false) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_fade_mode(self, set_mode));
        }
    }
}

void mixer_config_callback(esp_gmf_element_handle_t self, void *ctx)
{
    audio_el_res_t *res = (audio_el_res_t *)ctx;
    esp_gmf_event_state_t event_state = 0;
    esp_gmf_element_get_state(self, &event_state);
    esp_ae_mixer_mode_t set_mode_0 = res->is_first_open == true ? ESP_AE_MIXER_MODE_FADE_UPWARD : ESP_AE_MIXER_MODE_FADE_DOWNWARD;
    esp_ae_mixer_mode_t set_mode_1 = res->is_first_open == true ? ESP_AE_MIXER_MODE_FADE_DOWNWARD : ESP_AE_MIXER_MODE_FADE_UPWARD;
    uint32_t set_rate = res->is_first_open == true ? 48000 : 44100;
    uint32_t set_channels = res->is_first_open == true ? 2 : 1;
    uint32_t set_bits = res->is_first_open == true ? 16 : 32;
    if (event_state < ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_mixer_mode(self, 0, set_mode_0));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_mixer_mode(self, 1, set_mode_1));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_mixer_info(self, set_rate, set_channels, set_bits));
            res->is_do_open_set = true;
        } else if (res->is_first_open == false) {
            esp_ae_mixer_cfg_t *cfg = (esp_ae_mixer_cfg_t *)OBJ_GET_CFG(self);
            TEST_ASSERT_EQUAL(set_rate, cfg->sample_rate);
            TEST_ASSERT_EQUAL(set_channels, cfg->channel);
            TEST_ASSERT_EQUAL(set_bits, cfg->bits_per_sample);
        }
    } else if (event_state >= ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            if (res->is_do_open_set == true) {
                esp_ae_mixer_cfg_t *cfg = (esp_ae_mixer_cfg_t *)OBJ_GET_CFG(self);
                TEST_ASSERT_EQUAL(set_rate, cfg->sample_rate);
                TEST_ASSERT_EQUAL(set_channels, cfg->channel);
                TEST_ASSERT_EQUAL(set_bits, cfg->bits_per_sample);
            }
            res->is_first_open = false;
        } else if (res->is_first_open == false) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_mixer_mode(self, 0, set_mode_0));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_mixer_mode(self, 1, set_mode_1));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_mixer_info(self, set_rate, set_channels, set_bits));
        }
    }
}

void sonic_config_callback(esp_gmf_element_handle_t self, void *ctx)
{
    audio_el_res_t *res = (audio_el_res_t *)ctx;
    esp_gmf_event_state_t event_state = 0;
    esp_gmf_element_get_state(self, &event_state);
    float set_speed = res->is_first_open == true ? 1.5f : 0.75f;
    float set_pitch = res->is_first_open == true ? 1.2f : 0.9f;
    float speed_read = 0.0f;
    float pitch_read = 0.0f;
    if (event_state < ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_sonic_speed(self, set_speed));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_sonic_pitch(self, set_pitch));
            res->is_do_open_set = true;
        } else if (res->is_first_open == false) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_sonic_speed(self, &speed_read));
            TEST_ASSERT_EQUAL_FLOAT(set_speed, speed_read);
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_sonic_pitch(self, &pitch_read));
            TEST_ASSERT_EQUAL_FLOAT(set_pitch, pitch_read);
        }
    } else if (event_state >= ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            if (res->is_do_open_set == true) {
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_sonic_speed(self, &speed_read));
                TEST_ASSERT_EQUAL_FLOAT(set_speed, speed_read);
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_sonic_pitch(self, &pitch_read));
                TEST_ASSERT_EQUAL_FLOAT(set_pitch, pitch_read);
            }
            res->is_first_open = false;
        } else if (res->is_first_open == false) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_sonic_speed(self, set_speed));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_sonic_pitch(self, set_pitch));
        }
    }
}

void drc_config_callback(esp_gmf_element_handle_t self, void *ctx)
{
    audio_el_res_t *res = (audio_el_res_t *)ctx;
    esp_gmf_event_state_t event_state = 0;
    esp_gmf_element_get_state(self, &event_state);
    uint16_t set_attack = res->is_first_open == true ? 25 : 50;
    uint16_t set_release = res->is_first_open == true ? 90 : 180;
    float set_makeup = res->is_first_open == true ? 2.5f : 5.0f;
    uint16_t set_hold = res->is_first_open == true ? 10 : 20;
    float set_knee = res->is_first_open == true ? 1.0f : 2.0f;
    static esp_ae_drc_curve_point points_1[3] = {
        {.x = 0.0f, .y = -12.0f},
        {.x = -40.0f, .y = -38.0f},
        {.x = -100.0f, .y = -80.0f},
    };
    static esp_ae_drc_curve_point points_2[3] = {
        {.x = 0.0f, .y = -10.0f},
        {.x = -30.0f, .y = -35.0f},
        {.x = -100.0f, .y = -75.0f},
    };
    esp_ae_drc_curve_point *set_points = res->is_first_open == true ? points_1 : points_2;
    uint8_t set_point_num = 3;
    uint16_t attack_read = 0;
    uint16_t release_read = 0;
    float makeup_read = 0.0f;
    uint16_t hold_read = 0;
    float knee_read = 0.0f;
    uint8_t point_num_read = 0;
    esp_ae_drc_curve_point points_read[3] = {0};
    if (event_state < ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_drc_attack(self, set_attack));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_drc_release(self, set_release));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_drc_makeup(self, set_makeup));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_drc_hold(self, set_hold));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_drc_knee(self, set_knee));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_drc_points(self, set_points, set_point_num));
            res->is_do_open_set = true;
        } else if (res->is_first_open == false) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_drc_attack(self, &attack_read));
            TEST_ASSERT_EQUAL(set_attack, attack_read);
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_drc_release(self, &release_read));
            TEST_ASSERT_EQUAL(set_release, release_read);
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_drc_makeup(self, &makeup_read));
            TEST_ASSERT_EQUAL_FLOAT(set_makeup, makeup_read);
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_drc_hold(self, &hold_read));
            TEST_ASSERT_EQUAL(set_hold, hold_read);
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_drc_knee(self, &knee_read));
            TEST_ASSERT_EQUAL_FLOAT(set_knee, knee_read);
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_drc_point_num(self, &point_num_read));
            TEST_ASSERT_EQUAL(set_point_num, point_num_read);
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_drc_points(self, points_read, point_num_read));
            for (int i = 0; i < point_num_read; i++) {
                TEST_ASSERT_EQUAL_FLOAT(set_points[i].x, points_read[i].x);
                TEST_ASSERT_EQUAL_FLOAT(set_points[i].y, points_read[i].y);
            }
        }
    } else if (event_state >= ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            if (res->is_do_open_set == true) {
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_drc_attack(self, &attack_read));
                TEST_ASSERT_EQUAL(set_attack, attack_read);
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_drc_release(self, &release_read));
                TEST_ASSERT_EQUAL(set_release, release_read);
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_drc_makeup(self, &makeup_read));
                TEST_ASSERT_EQUAL_FLOAT(set_makeup, makeup_read);
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_drc_hold(self, &hold_read));
                TEST_ASSERT_EQUAL(set_hold, hold_read);
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_drc_knee(self, &knee_read));
                TEST_ASSERT_EQUAL_FLOAT(set_knee, knee_read);
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_drc_point_num(self, &point_num_read));
                TEST_ASSERT_EQUAL(set_point_num, point_num_read);
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_drc_points(self, points_read, point_num_read));
                for (int i = 0; i < point_num_read; i++) {
                    TEST_ASSERT_EQUAL_FLOAT(set_points[i].x, points_read[i].x);
                    TEST_ASSERT_EQUAL_FLOAT(set_points[i].y, points_read[i].y);
                }
            }
            res->is_first_open = false;
        } else if (res->is_first_open == false) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_drc_attack(self, set_attack));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_drc_release(self, set_release));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_drc_makeup(self, set_makeup));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_drc_hold(self, set_hold));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_drc_knee(self, set_knee));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_drc_points(self, set_points, set_point_num));
        }
    }
}

void mbc_config_callback(esp_gmf_element_handle_t self, void *ctx)
{
    audio_el_res_t *res = (audio_el_res_t *)ctx;
    esp_gmf_event_state_t event_state = 0;
    esp_gmf_element_get_state(self, &event_state);
    esp_ae_mbc_para_t set_para = {
        .threshold = res->is_first_open == true ? -18.0f : -20.0f,
        .ratio = res->is_first_open == true ? 2.5f : 3.0f,
        .makeup_gain = res->is_first_open == true ? 4.0f : 5.0f,
        .attack_time = res->is_first_open == true ? 5 : 10,
        .release_time = res->is_first_open == true ? 120 : 150,
        .hold_time = res->is_first_open == true ? 12 : 15,
        .knee_width = res->is_first_open == true ? 1.5f : 2.0f,
    };
    uint32_t set_fc = res->is_first_open == true ? 750 : 1000;
    bool set_solo = res->is_first_open == true ? true : false;
    bool set_bypass = res->is_first_open == true ? true : false;
    esp_ae_mbc_para_t para_read = {0};
    uint32_t fc_read = 0;
    bool solo_state = false;
    bool bypass_state = false;
    if (event_state < ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_mbc_para(self, 0, &set_para));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_mbc_fc(self, 0, set_fc));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_mbc_solo(self, 0, set_solo));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_mbc_bypass(self, 0, set_bypass));
            res->is_do_open_set = true;
        } else if (res->is_first_open == false) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_mbc_para(self, 0, &para_read));
            TEST_ASSERT_EQUAL_FLOAT(set_para.threshold, para_read.threshold);
            TEST_ASSERT_EQUAL_FLOAT(set_para.ratio, para_read.ratio);
            TEST_ASSERT_EQUAL_FLOAT(set_para.makeup_gain, para_read.makeup_gain);
            TEST_ASSERT_EQUAL_UINT16(set_para.attack_time, para_read.attack_time);
            TEST_ASSERT_EQUAL_UINT16(set_para.release_time, para_read.release_time);
            TEST_ASSERT_EQUAL_UINT16(set_para.hold_time, para_read.hold_time);
            TEST_ASSERT_EQUAL_FLOAT(set_para.knee_width, para_read.knee_width);
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_mbc_fc(self, 0, &fc_read));
            TEST_ASSERT_EQUAL(set_fc, fc_read);
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_mbc_solo(self, 0, &solo_state));
            TEST_ASSERT_EQUAL(set_solo, solo_state);
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_mbc_bypass(self, 0, &bypass_state));
            TEST_ASSERT_EQUAL(set_bypass, bypass_state);
        }
    } else if (event_state >= ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            if (res->is_do_open_set == true) {
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_mbc_para(self, 0, &para_read));
                TEST_ASSERT_EQUAL_FLOAT(set_para.threshold, para_read.threshold);
                TEST_ASSERT_EQUAL_FLOAT(set_para.ratio, para_read.ratio);
                TEST_ASSERT_EQUAL_FLOAT(set_para.makeup_gain, para_read.makeup_gain);
                TEST_ASSERT_EQUAL_UINT16(set_para.attack_time, para_read.attack_time);
                TEST_ASSERT_EQUAL_UINT16(set_para.release_time, para_read.release_time);
                TEST_ASSERT_EQUAL_UINT16(set_para.hold_time, para_read.hold_time);
                TEST_ASSERT_EQUAL_FLOAT(set_para.knee_width, para_read.knee_width);
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_mbc_fc(self, 0, &fc_read));
                TEST_ASSERT_EQUAL(set_fc, fc_read);
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_mbc_solo(self, 0, &solo_state));
                TEST_ASSERT_EQUAL(set_solo, solo_state);
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_mbc_bypass(self, 0, &bypass_state));
                TEST_ASSERT_EQUAL(set_bypass, bypass_state);
            }
            res->is_first_open = false;
        } else if (res->is_first_open == false) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_mbc_para(self, 0, &set_para));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_mbc_fc(self, 0, set_fc));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_mbc_solo(self, 0, set_solo));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_mbc_bypass(self, 0, set_bypass));
        }
    }
}

void alc_config_callback(esp_gmf_element_handle_t self, void *ctx)
{
    audio_el_res_t *res = (audio_el_res_t *)ctx;
    esp_gmf_event_state_t event_state = 0;
    esp_gmf_element_get_state(self, &event_state);
    int8_t set_gain = res->is_first_open == true ? 2 : -2;
    int8_t gain_read = 0;
    if (event_state < ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            for (int i = 0; i < res->in_inst[0].src_info.channels; i++) {
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_alc_gain(self, i, set_gain));
            }
            res->is_do_open_set = true;
        } else {
            for (int i = 0; i < res->in_inst[0].src_info.channels; i++) {
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_alc_gain(self, i, &gain_read));
                TEST_ASSERT_EQUAL(set_gain, gain_read);
            }
        }
    } else if (event_state >= ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            if (res->is_do_open_set == true) {
                for (int i = 0; i < res->in_inst[0].src_info.channels; i++) {
                    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_get_alc_gain(self, i, &gain_read));
                    TEST_ASSERT_EQUAL(set_gain, gain_read);
                }
            }
            res->is_first_open = false;
        } else {
            for (int i = 0; i < res->in_inst[0].src_info.channels; i++) {
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, audio_el_test_set_alc_gain(self, i, set_gain));
            }
        }
    }
}

void bit_cvt_config_callback(esp_gmf_element_handle_t self, void *ctx)
{
    audio_el_res_t *res = (audio_el_res_t *)ctx;
    esp_gmf_event_state_t event_state = 0;
    esp_gmf_element_get_state(self, &event_state);
    uint8_t set_bits = res->is_first_open == true ? 16 : 32;
    if (event_state < ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_bits(self, set_bits));
        } else if (res->is_first_open == false) {
            esp_ae_bit_cvt_cfg_t *cfg = (esp_ae_bit_cvt_cfg_t *)OBJ_GET_CFG(self);
            TEST_ASSERT_EQUAL(set_bits, cfg->dest_bits);
        }
    } else if (event_state >= ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            esp_ae_bit_cvt_cfg_t *cfg = (esp_ae_bit_cvt_cfg_t *)OBJ_GET_CFG(self);
            TEST_ASSERT_EQUAL(set_bits, cfg->dest_bits);
            res->is_first_open = false;
        } else if (res->is_first_open == false) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_bits(self, set_bits));
        }
    }
}

void ch_cvt_config_callback(esp_gmf_element_handle_t self, void *ctx)
{
    audio_el_res_t *res = (audio_el_res_t *)ctx;
    esp_gmf_event_state_t event_state = 0;
    esp_gmf_element_get_state(self, &event_state);
    uint32_t set_channels = res->is_first_open == true ? 4 : 2;
    if (event_state < ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_ch(self, set_channels));
        } else if (res->is_first_open == false) {
            esp_ae_ch_cvt_cfg_t *cfg = (esp_ae_ch_cvt_cfg_t *)OBJ_GET_CFG(self);
            TEST_ASSERT_EQUAL(set_channels, cfg->dest_ch);
        }
    } else if (event_state >= ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            esp_ae_ch_cvt_cfg_t *cfg = (esp_ae_ch_cvt_cfg_t *)OBJ_GET_CFG(self);
            TEST_ASSERT_EQUAL(set_channels, cfg->dest_ch);
            res->is_first_open = false;
        } else if (res->is_first_open == false) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_ch(self, set_channels));
        }
    }
}

void rate_cvt_config_callback(esp_gmf_element_handle_t self, void *ctx)
{
    audio_el_res_t *res = (audio_el_res_t *)ctx;
    esp_gmf_event_state_t event_state = 0;
    esp_gmf_element_get_state(self, &event_state);
    uint32_t set_rate = res->is_first_open == true ? 48000 : 44100;
    if (event_state < ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_rate(self, set_rate));
        } else if (res->is_first_open == false) {
            esp_ae_rate_cvt_cfg_t *cfg = (esp_ae_rate_cvt_cfg_t *)OBJ_GET_CFG(self);
            TEST_ASSERT_EQUAL(set_rate, cfg->dest_rate);
        }
    } else if (event_state >= ESP_GMF_EVENT_STATE_OPENING) {
        if (res->is_first_open == true) {
            esp_ae_rate_cvt_cfg_t *cfg = (esp_ae_rate_cvt_cfg_t *)OBJ_GET_CFG(self);
            TEST_ASSERT_EQUAL(set_rate, cfg->dest_rate);
            res->is_first_open = false;
        } else if (res->is_first_open == false) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_rate(self, set_rate));
        }
    }
}

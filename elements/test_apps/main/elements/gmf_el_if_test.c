/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "unity.h"
#include <string.h>
#include <stdbool.h>

#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_eq.h"
#include "esp_gmf_alc.h"
#include "esp_gmf_fade.h"
#include "esp_gmf_mixer.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_asrc.h"
#include "esp_gmf_drc.h"
#include "esp_gmf_mbc.h"
#include "esp_gmf_sonic.h"
#include "esp_gmf_interleave.h"
#include "esp_gmf_deinterleave.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_audio_dec.h"
#include "esp_gmf_audio_muxer.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_howl.h"
#include "esp_gmf_reverb.h"
#include "esp_gmf_delay.h"
#include "esp_gmf_io_embed_flash.h"
#include "esp_gmf_io_http.h"
#include "esp_gmf_io_file.h"
#include "esp_gmf_io_i2s_pdm.h"

#include "esp_gmf_copier.h"

#include "esp_gmf_element.h"
#include "esp_gmf_audio_element.h"

static const esp_gmf_args_desc_t *find_config_arg(const esp_gmf_args_desc_t *args_desc, const char *name)
{
    const esp_gmf_args_desc_t *arg = args_desc;
    while (arg != NULL) {
        if (strcmp(arg->name, name) == 0) {
            return arg;
        }
        arg = arg->next;
    }
    TEST_FAIL_MESSAGE("Method argument not found");
    return NULL;
}

static const esp_gmf_method_t *get_element_method(esp_gmf_element_handle_t handle, const char *name)
{
    const esp_gmf_method_t *methods = NULL;
    const esp_gmf_method_t *method = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_get_method(handle, &methods));
    TEST_ASSERT_NOT_NULL(methods);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_method_found(methods, name, &method));
    TEST_ASSERT_NOT_NULL(method);
    return method;
}

static void check_runtime_method_pair(esp_gmf_element_handle_t handle, const char *setter_name,
                                      const char *getter_name)
{
    const esp_gmf_method_t *setter = get_element_method(handle, setter_name);
    const esp_gmf_method_t *getter = get_element_method(handle, getter_name);
    TEST_ASSERT_TRUE(setter->runtime_safe);
    TEST_ASSERT_NOT_NULL(setter->getter);
    TEST_ASSERT_EQUAL_STRING(getter_name, setter->getter);
    TEST_ASSERT_TRUE(getter->runtime_safe);
    TEST_ASSERT_NULL(getter->getter);
}

static void check_element_bypass(esp_gmf_element_handle_t handle)
{
    bool enable = true;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_element_set_bypass(NULL, true));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_element_get_bypass(NULL, &enable));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_element_get_bypass(handle, NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_get_bypass(handle, &enable));
    TEST_ASSERT_FALSE(enable);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_set_bypass(handle, true));
    enable = false;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_get_bypass(handle, &enable));
    TEST_ASSERT_TRUE(enable);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_set_bypass(handle, false));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_get_bypass(handle, &enable));
    TEST_ASSERT_FALSE(enable);
}

void test_esp_gmf_alc_if()
{
    esp_ae_alc_cfg_t config = DEFAULT_ESP_GMF_ALC_CONFIG();
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_alc_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_alc_init(&config, &handle), ESP_GMF_ERR_OK);

    esp_gmf_info_sound_t sound_info = {
        .sample_rates = 44100,
        .channels = 1,
        .bits = 24,
    };
    esp_gmf_event_pkt_t sound_event = {
        .from = handle,
        .type = ESP_GMF_EVT_TYPE_REPORT_INFO,
        .sub = ESP_GMF_INFO_SOUND,
        .payload = &sound_info,
        .payload_size = sizeof(sound_info),
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      ESP_GMF_ELEMENT_GET(handle)->ops.event_receiver(&sound_event, handle));
    TEST_ASSERT_EQUAL(esp_gmf_element_process_open((esp_gmf_element_handle_t)handle, NULL), ESP_GMF_ERR_OK);
    // Set gain function test
    TEST_ASSERT_EQUAL(esp_gmf_alc_set_gain(NULL, 0, 10), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_alc_set_gain(handle, config.channel + 1, 10), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_alc_set_gain(handle, config.channel - 1, -10), ESP_GMF_ERR_OK);
    // Test gain boundary values [-64, 63]
    TEST_ASSERT_EQUAL(esp_gmf_alc_set_gain(handle, 0, -64), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_alc_set_gain(handle, 0, 63), ESP_GMF_ERR_OK);
    // Test gain out of range (should be rejected or clamped)
    // Note: According to API doc, values below -64 set to mute, values above 63 not supported
    TEST_ASSERT_EQUAL(esp_gmf_alc_set_gain(handle, 0, 64), ESP_GMF_ERR_FAIL);
    // Get gain function test
    int8_t gain = 0;
    TEST_ASSERT_EQUAL(esp_gmf_alc_get_gain(NULL, 0, &gain), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_alc_get_gain(handle, 0, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_alc_get_gain(handle, config.channel + 1, &gain), ESP_GMF_ERR_INVALID_ARG);
    check_element_bypass(handle);
    TEST_ASSERT_EQUAL(esp_gmf_element_process_close((esp_gmf_element_handle_t)handle, NULL), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_alc_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_bit_cvt_if()
{
    esp_ae_bit_cvt_cfg_t config = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_bit_cvt_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_bit_cvt_init(&config, &handle), ESP_GMF_ERR_OK);
    check_runtime_method_pair(handle, AMETHOD(BIT_CVT, SET_DEST_BITS),
                              AMETHOD(BIT_CVT, GET_DEST_BITS));
    uint8_t dest_bits = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_bit_cvt_get_dest_bits(handle, &dest_bits));
    TEST_ASSERT_EQUAL_UINT8(config.dest_bits, dest_bits);
    dest_bits = 24;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(BIT_CVT, SET_DEST_BITS), &dest_bits, sizeof(dest_bits)));
    dest_bits = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(BIT_CVT, GET_DEST_BITS), &dest_bits, sizeof(dest_bits)));
    TEST_ASSERT_EQUAL_UINT8(24, dest_bits);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_bit_cvt_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_ch_cvt_if()
{
    esp_ae_ch_cvt_cfg_t config = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_ch_cvt_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_ch_cvt_init(&config, &handle), ESP_GMF_ERR_OK);
    check_runtime_method_pair(handle, AMETHOD(CH_CVT, SET_DEST_CH),
                              AMETHOD(CH_CVT, GET_DEST_CH));
    // Set dest channel function test
    TEST_ASSERT_EQUAL(esp_gmf_ch_cvt_set_dest_channel(NULL, 1), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_ch_cvt_set_dest_channel(handle, 1), ESP_GMF_ERR_OK);
    uint8_t dest_ch = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_ch_cvt_get_dest_channel(handle, &dest_ch));
    TEST_ASSERT_EQUAL_UINT8(1, dest_ch);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    float weight[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    config = (esp_ae_ch_cvt_cfg_t)DEFAULT_ESP_GMF_CH_CVT_CONFIG();
    config.weight = weight;
    config.weight_len = 4;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_ch_cvt_init(&config, &handle));
    esp_ae_ch_cvt_cfg_t *weight_cfg = OBJ_GET_CFG(handle);
    TEST_ASSERT_NOT_EQUAL(weight, weight_cfg->weight);
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_ch_cvt_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_deinterleave_if()
{
    esp_gmf_deinterleave_cfg config = DEFAULT_ESP_GMF_DEINTERLEAVE_CONFIG();
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_deinterleave_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_deinterleave_init(&config, &handle), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_deinterleave_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_eq_if()
{
    esp_ae_eq_cfg_t config = DEFAULT_ESP_GMF_EQ_CONFIG();
    esp_gmf_obj_handle_t handle;
    esp_ae_eq_filter_para_t para = {0};
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_eq_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_eq_init(&config, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_element_process_open((esp_gmf_element_handle_t)handle, NULL), ESP_GMF_ERR_OK);
    // Set para function test
    TEST_ASSERT_EQUAL(esp_gmf_eq_set_para(NULL, 0, &para), ESP_GMF_ERR_INVALID_ARG);
    // sample_rate = 48000, so fc must be < 24000
    para.filter_type = ESP_AE_EQ_FILTER_PEAK;
    para.q = 1.0f;
    para.gain = 0.0f;
    para.fc = 25000;  // Greater than sample_rate/2, should be rejected
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_eq_set_para(handle, 0, &para));
    // Test valid fc value
    para.fc = 1000;  // Less than sample_rate/2, should be OK
    TEST_ASSERT_EQUAL(esp_gmf_eq_set_para(handle, 0, &para), ESP_GMF_ERR_OK);
    esp_ae_eq_filter_para_t config_para = {0};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_eq_get_para(handle, 0, &config_para));
    TEST_ASSERT_EQUAL_UINT32(para.filter_type, config_para.filter_type);
    TEST_ASSERT_EQUAL_UINT32(para.fc, config_para.fc);
    TEST_ASSERT_EQUAL_FLOAT(para.q, config_para.q);
    TEST_ASSERT_EQUAL_FLOAT(para.gain, config_para.gain);
    // Test EQ constraint: only one high pass filter allowed
    para.filter_type = ESP_AE_EQ_FILTER_HIGH_PASS;
    para.fc = 100;
    TEST_ASSERT_EQUAL(esp_gmf_eq_set_para(handle, 0, &para), ESP_GMF_ERR_OK);
    // Try to set a second high pass filter, should be rejected
    para.fc = 200;
    TEST_ASSERT_EQUAL(esp_gmf_eq_set_para(handle, 1, &para), ESP_GMF_ERR_OK);
    check_element_bypass(handle);
    TEST_ASSERT_EQUAL(esp_gmf_element_process_close((esp_gmf_element_handle_t)handle, NULL), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_eq_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_drc_if()
{
    esp_ae_drc_cfg_t config = DEFAULT_ESP_GMF_DRC_CONFIG();
    esp_gmf_obj_handle_t handle;
    uint16_t attack_time = 25;
    uint16_t release_time = 90;
    uint16_t hold_time = 6;
    float makeup_gain = 2.5f;
    float knee_width = 3.0f;
    esp_ae_drc_curve_point points[3] = {
        {.x = 0.0f, .y = -10.0f},
        {.x = -30.0f, .y = -35.0f},
        {.x = -60.0f, .y = -60.0f},
    };

    TEST_ASSERT_EQUAL(esp_gmf_drc_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_drc_init(&config, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_element_process_open((esp_gmf_element_handle_t)handle, NULL), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_drc_set_attack_time(NULL, attack_time), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_drc_set_attack_time(handle, attack_time), ESP_GMF_ERR_OK);
    uint16_t attack_read = 0;
    TEST_ASSERT_EQUAL(esp_gmf_drc_get_attack_time(NULL, &attack_read), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_drc_get_attack_time(handle, &attack_read), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(attack_time, attack_read);

    TEST_ASSERT_EQUAL(esp_gmf_drc_set_release_time(handle, release_time), ESP_GMF_ERR_OK);
    uint16_t release_read = 0;
    TEST_ASSERT_EQUAL(esp_gmf_drc_get_release_time(handle, &release_read), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(release_time, release_read);

    TEST_ASSERT_EQUAL(esp_gmf_drc_set_hold_time(handle, hold_time), ESP_GMF_ERR_OK);
    uint16_t hold_read = 0;
    TEST_ASSERT_EQUAL(esp_gmf_drc_get_hold_time(handle, &hold_read), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(hold_time, hold_read);

    TEST_ASSERT_EQUAL(esp_gmf_drc_set_makeup_gain(handle, makeup_gain), ESP_GMF_ERR_OK);
    float makeup_read = 0.0f;
    TEST_ASSERT_EQUAL(esp_gmf_drc_get_makeup_gain(handle, &makeup_read), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(makeup_gain, makeup_read);

    TEST_ASSERT_EQUAL(esp_gmf_drc_set_knee_width(handle, knee_width), ESP_GMF_ERR_OK);
    float knee_read = 0.0f;
    TEST_ASSERT_EQUAL(esp_gmf_drc_get_knee_width(handle, &knee_read), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(knee_width, knee_read);
    // Test DRC point_num range [2, 6]
    TEST_ASSERT_EQUAL(esp_gmf_drc_set_points(handle, points, 0), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_drc_set_points(handle, points, 1), ESP_GMF_ERR_INVALID_ARG);  // point_num=1 should be rejected
    TEST_ASSERT_EQUAL(esp_gmf_drc_set_points(handle, points, 7), ESP_GMF_ERR_INVALID_ARG);
    // Test DRC curve must contain x=0.0 and x=-100.0
    esp_ae_drc_curve_point points_no_zero[2] = {
        {.x = -30.0f, .y = -35.0f},
        {.x = -60.0f, .y = -60.0f},
    };
    TEST_ASSERT_EQUAL(esp_gmf_drc_set_points(handle, points_no_zero, 2), ESP_GMF_ERR_FAIL);  // Missing x=0.0 and x=-100.0
    esp_ae_drc_curve_point points_no_minus100[2] = {
        {.x = 0.0f, .y = -10.0f},
        {.x = -30.0f, .y = -35.0f},
    };
    TEST_ASSERT_EQUAL(esp_gmf_drc_set_points(handle, points_no_minus100, 2), ESP_GMF_ERR_FAIL);  // Missing x=-100.0
    esp_ae_drc_curve_point points_valid[3] = {
        {.x = 0.0f, .y = -10.0f},
        {.x = -30.0f, .y = -35.0f},
        {.x = -100.0f, .y = -100.0f},
    };
    TEST_ASSERT_EQUAL(esp_gmf_drc_set_points(handle, points_valid, 3), ESP_GMF_ERR_OK);
    uint8_t point_num = 0;
    TEST_ASSERT_EQUAL(esp_gmf_drc_get_point_num(handle, &point_num), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(3, point_num);
    esp_ae_drc_curve_point out_points[3] = {0};
    TEST_ASSERT_EQUAL(esp_gmf_drc_get_points(handle, out_points), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(points_valid[1].x, out_points[1].x);
    TEST_ASSERT_EQUAL(points_valid[1].y, out_points[1].y);
    check_element_bypass(handle);
    TEST_ASSERT_EQUAL(esp_gmf_element_process_close((esp_gmf_element_handle_t)handle, NULL), ESP_GMF_ERR_OK);

    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_drc_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_mbc_if()
{
    esp_ae_mbc_config_t config = DEFAULT_ESP_GMF_MBC_CONFIG();
    esp_gmf_obj_handle_t handle;
    esp_ae_mbc_para_t para = {
        .threshold = -18.0f,
        .ratio = 2.5f,
        .makeup_gain = 4.0f,
        .attack_time = 5,
        .release_time = 120,
        .hold_time = 12,
        .knee_width = 1.5f,
    };
    uint32_t fc_value = 750;

    TEST_ASSERT_EQUAL(esp_gmf_mbc_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_mbc_init(&config, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_element_process_open((esp_gmf_element_handle_t)handle, NULL), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_mbc_set_para(NULL, 0, &para), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_mbc_set_para(handle, 0, &para), ESP_GMF_ERR_OK);
    esp_ae_mbc_para_t para_out = {0};
    TEST_ASSERT_EQUAL(esp_gmf_mbc_get_para(handle, 0, &para_out), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(para.threshold, para_out.threshold);
    TEST_ASSERT_EQUAL(para.ratio, para_out.ratio);
    TEST_ASSERT_EQUAL(para.attack_time, para_out.attack_time);
    TEST_ASSERT_EQUAL(para.release_time, para_out.release_time);
    TEST_ASSERT_EQUAL(para.hold_time, para_out.hold_time);
    TEST_ASSERT_EQUAL(para.knee_width, para_out.knee_width);
    TEST_ASSERT_EQUAL(para.makeup_gain, para_out.makeup_gain);

    TEST_ASSERT_EQUAL(esp_gmf_mbc_set_fc(handle, 0, fc_value), ESP_GMF_ERR_OK);
    uint32_t fc_read = 0;
    TEST_ASSERT_EQUAL(esp_gmf_mbc_get_fc(handle, 0, &fc_read), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(fc_value, fc_read);

    TEST_ASSERT_EQUAL(esp_gmf_mbc_set_solo(handle, 0, true), ESP_GMF_ERR_OK);
    bool solo_state = false;
    TEST_ASSERT_EQUAL(esp_gmf_mbc_get_solo(handle, 0, &solo_state), ESP_GMF_ERR_OK);
    TEST_ASSERT_TRUE(solo_state);

    TEST_ASSERT_EQUAL(esp_gmf_mbc_set_bypass(handle, 0, true), ESP_GMF_ERR_OK);
    bool bypass_state = false;
    TEST_ASSERT_EQUAL(esp_gmf_mbc_get_bypass(handle, 0, &bypass_state), ESP_GMF_ERR_OK);
    TEST_ASSERT_TRUE(bypass_state);
    // Test MBC band_idx coverage (bands 0-3)
    TEST_ASSERT_EQUAL(esp_gmf_mbc_set_para(handle, 1, &para), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_mbc_set_para(handle, 2, &para), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_mbc_set_para(handle, 3, &para), ESP_GMF_ERR_OK);
    // Test invalid band_idx (should be >= 4)
    TEST_ASSERT_EQUAL(esp_gmf_mbc_set_para(handle, 4, &para), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_mbc_get_para(handle, 1, &para_out), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_mbc_get_para(handle, 2, &para_out), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_mbc_get_para(handle, 3, &para_out), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_mbc_get_para(handle, 4, &para_out), ESP_GMF_ERR_INVALID_ARG);
    check_element_bypass(handle);
    TEST_ASSERT_EQUAL(esp_gmf_element_process_close((esp_gmf_element_handle_t)handle, NULL), ESP_GMF_ERR_OK);

    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_mbc_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_fade_if()
{
    esp_ae_fade_cfg_t config = DEFAULT_ESP_GMF_FADE_CONFIG();
    esp_gmf_obj_handle_t handle;
    esp_ae_fade_mode_t mode = ESP_AE_FADE_MODE_INVALID;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_fade_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_fade_init(&config, &handle), ESP_GMF_ERR_OK);
    check_runtime_method_pair(handle, AMETHOD(FADE, SET_MODE), AMETHOD(FADE, GET_MODE));
    const esp_gmf_method_t *reset = get_element_method(handle, AMETHOD(FADE, RESET));
    TEST_ASSERT_FALSE(reset->runtime_safe);
    TEST_ASSERT_EQUAL(esp_gmf_element_process_open((esp_gmf_element_handle_t)handle, NULL), ESP_GMF_ERR_OK);
    // Set mode function test
    TEST_ASSERT_EQUAL(esp_gmf_fade_set_mode(NULL, 0), ESP_GMF_ERR_INVALID_ARG);
    // Get mode function test
    TEST_ASSERT_EQUAL(esp_gmf_fade_get_mode(NULL, &mode), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_fade_get_mode(handle, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_fade_get_mode(handle, &mode), ESP_GMF_ERR_OK);
    // Reset function test
    TEST_ASSERT_EQUAL(esp_gmf_fade_reset(NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_fade_reset(handle), ESP_GMF_ERR_OK);
    check_element_bypass(handle);
    TEST_ASSERT_EQUAL(esp_gmf_element_process_close((esp_gmf_element_handle_t)handle, NULL), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_fade_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_interleave_if()
{
    esp_gmf_interleave_cfg config = DEFAULT_ESP_GMF_INTERLEAVE_CONFIG();
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_interleave_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_interleave_init(&config, &handle), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_interleave_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_mixer_if()
{
    esp_ae_mixer_cfg_t config = DEFAULT_ESP_GMF_MIXER_CONFIG();
    esp_gmf_obj_handle_t handle;
    esp_ae_mixer_mode_t mode = ESP_AE_MIXER_MODE_INVALID;
    uint32_t sample_rate = 48000;
    uint8_t channel = 2;
    uint8_t bits = 16;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_mixer_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_mixer_init(&config, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_element_process_open((esp_gmf_element_handle_t)handle, NULL), ESP_GMF_ERR_OK);
    // Set mode function test
    TEST_ASSERT_EQUAL(esp_gmf_mixer_set_mode(NULL, 0, mode), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_mixer_set_mode(handle, config.src_num + 1, mode), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_mixer_set_mode(handle, 0, mode), ESP_GMF_ERR_FAIL);
    // Set audio info function test
    TEST_ASSERT_EQUAL(esp_gmf_mixer_set_audio_info(NULL, sample_rate, channel, bits), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_mixer_set_audio_info(handle, sample_rate, channel, bits), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_element_process_close((esp_gmf_element_handle_t)handle, NULL), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_mixer_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_rate_cvt_if()
{
    esp_ae_rate_cvt_cfg_t config = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
    esp_gmf_obj_handle_t handle;
    uint32_t sample_rate = 48000;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_rate_cvt_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_rate_cvt_init(&config, &handle), ESP_GMF_ERR_OK);
    check_runtime_method_pair(handle, AMETHOD(RATE_CVT, SET_DEST_RATE),
                              AMETHOD(RATE_CVT, GET_DEST_RATE));
    // Set mode function test
    TEST_ASSERT_EQUAL(esp_gmf_rate_cvt_set_dest_rate(NULL, sample_rate), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_rate_cvt_set_dest_rate(handle, sample_rate), ESP_GMF_ERR_OK);
    uint32_t dest_rate = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_rate_cvt_get_dest_rate(handle, &dest_rate));
    TEST_ASSERT_EQUAL_UINT32(sample_rate, dest_rate);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_rate_cvt_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_asrc_if()
{
    esp_asrc_cfg_t config = DEFAULT_ESP_GMF_ASRC_CONFIG();
    esp_gmf_obj_handle_t handle;
    uint32_t sample_rate = 48000;
    uint8_t dest_ch = 1;
    uint8_t dest_bits = 16;
    TEST_ASSERT_EQUAL(esp_gmf_asrc_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_asrc_init(&config, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_asrc_set_dest_rate(NULL, sample_rate), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_asrc_set_dest_rate(handle, sample_rate), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_asrc_set_dest_ch(NULL, dest_ch), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_asrc_set_dest_ch(handle, dest_ch), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_asrc_set_dest_bits(NULL, dest_bits), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_asrc_set_dest_bits(handle, dest_bits), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_asrc_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_sonic_if()
{
    esp_ae_sonic_cfg_t config = DEFAULT_ESP_GMF_SONIC_CONFIG();
    esp_gmf_obj_handle_t handle;
    float speed = 1.0;
    float pitch = 1.0;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_sonic_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_sonic_init(&config, &handle), ESP_GMF_ERR_OK);
    check_runtime_method_pair(handle, AMETHOD(SONIC, SET_SPEED), AMETHOD(SONIC, GET_SPEED));
    check_runtime_method_pair(handle, AMETHOD(SONIC, SET_PITCH), AMETHOD(SONIC, GET_PITCH));
    TEST_ASSERT_EQUAL(esp_gmf_element_process_open((esp_gmf_element_handle_t)handle, NULL), ESP_GMF_ERR_OK);
    // Set speed function test
    TEST_ASSERT_EQUAL(esp_gmf_sonic_set_speed(NULL, speed), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_sonic_set_speed(handle, speed), ESP_GMF_ERR_OK);
    // Test speed range [0.5, 2.0] - boundary values
    TEST_ASSERT_EQUAL(esp_gmf_sonic_set_speed(handle, 0.5f), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_sonic_set_speed(handle, 2.0f), ESP_GMF_ERR_OK);
    // Test speed out of range values
    TEST_ASSERT_EQUAL(esp_gmf_sonic_set_speed(handle, 0.3f), ESP_GMF_ERR_FAIL);  // Below minimum
    TEST_ASSERT_EQUAL(esp_gmf_sonic_set_speed(handle, 2.5f), ESP_GMF_ERR_FAIL);  // Above maximum
    // Get speed function test
    TEST_ASSERT_EQUAL(esp_gmf_sonic_get_speed(NULL, &speed), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_sonic_get_speed(handle, &speed), ESP_GMF_ERR_OK);
    // Set pitch function test
    TEST_ASSERT_EQUAL(esp_gmf_sonic_set_pitch(NULL, pitch), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_sonic_set_pitch(handle, pitch), ESP_GMF_ERR_OK);
    // Test pitch range [0.5, 2.0] - boundary values
    TEST_ASSERT_EQUAL(esp_gmf_sonic_set_pitch(handle, 0.5f), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_sonic_set_pitch(handle, 2.0f), ESP_GMF_ERR_OK);
    // Test pitch out of range values
    TEST_ASSERT_EQUAL(esp_gmf_sonic_set_pitch(handle, 0.3f), ESP_GMF_ERR_FAIL);  // Below minimum
    TEST_ASSERT_EQUAL(esp_gmf_sonic_set_pitch(handle, 2.5f), ESP_GMF_ERR_FAIL);  // Above maximum
    // Get pitch function test
    TEST_ASSERT_EQUAL(esp_gmf_sonic_get_pitch(NULL, &pitch), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_sonic_get_pitch(handle, &pitch), ESP_GMF_ERR_OK);
    check_element_bypass(handle);
    TEST_ASSERT_EQUAL(esp_gmf_element_process_close((esp_gmf_element_handle_t)handle, NULL), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_sonic_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_dec_if()
{
    esp_audio_simple_dec_cfg_t config = DEFAULT_ESP_GMF_AUDIO_DEC_CONFIG();
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_audio_dec_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_audio_dec_init(&config, &handle), ESP_GMF_ERR_OK);
    const esp_gmf_method_t *method = get_element_method(
        handle, AMETHOD(DECODER, RECONFIG_BY_SND_INFO));
    const esp_gmf_args_desc_t *sound_args = method->args_desc->val;
    const esp_gmf_args_desc_t *arg = find_config_arg(
        sound_args, AMETHOD_ARG(DECODER, RECONFIG_BY_SND_INFO, INFO_CHANNEL));
    TEST_ASSERT_EQUAL(ESP_GMF_ARGS_TYPE_UINT8, arg->type);
    arg = find_config_arg(sound_args, AMETHOD_ARG(DECODER, RECONFIG_BY_SND_INFO, INFO_BITS));
    TEST_ASSERT_EQUAL(ESP_GMF_ARGS_TYPE_UINT8, arg->type);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_audio_dec_init(NULL, &handle), ESP_GMF_ERR_OK);
    esp_audio_simple_dec_cfg_t *cfg = OBJ_GET_CFG(handle);
    TEST_ASSERT_NOT_EQUAL(NULL, cfg);
    TEST_ASSERT_EQUAL(cfg->dec_type, ESP_AUDIO_SIMPLE_DEC_TYPE_NONE);
    TEST_ASSERT_NULL(cfg->dec_cfg);
    TEST_ASSERT_EQUAL(cfg->cfg_size, 0);
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_enc_if()
{
    esp_audio_enc_config_t config = DEFAULT_ESP_GMF_AUDIO_ENC_CONFIG();
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_audio_enc_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_audio_enc_init(&config, &handle), ESP_GMF_ERR_OK);
    check_runtime_method_pair(handle, AMETHOD(ENCODER, SET_BITRATE),
                              AMETHOD(ENCODER, GET_BITRATE));
    const esp_gmf_method_t *method = get_element_method(handle, AMETHOD(ENCODER, GET_FRAME_SZ));
    TEST_ASSERT_EQUAL(ESP_GMF_ARGS_TYPE_UINT32, method->args_desc->type);
    TEST_ASSERT_EQUAL(ESP_GMF_ARGS_TYPE_UINT32, method->args_desc->next->type);
    method = get_element_method(handle, AMETHOD(ENCODER, RECONFIG_BY_SND_INFO));
    const esp_gmf_args_desc_t *sound_args = method->args_desc->val;
    const esp_gmf_args_desc_t *arg = find_config_arg(
        sound_args, AMETHOD_ARG(ENCODER, RECONFIG_BY_SND_INFO, INFO_CHANNEL));
    TEST_ASSERT_EQUAL(ESP_GMF_ARGS_TYPE_UINT8, arg->type);
    arg = find_config_arg(sound_args, AMETHOD_ARG(ENCODER, RECONFIG_BY_SND_INFO, INFO_BITS));
    TEST_ASSERT_EQUAL(ESP_GMF_ARGS_TYPE_UINT8, arg->type);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_audio_enc_init(NULL, &handle), ESP_GMF_ERR_OK);
    esp_audio_enc_config_t *cfg = OBJ_GET_CFG(handle);
    TEST_ASSERT_NOT_EQUAL(NULL, cfg);
    TEST_ASSERT_EQUAL(cfg->type, ESP_AUDIO_TYPE_UNSUPPORT);
    TEST_ASSERT_NULL(cfg->cfg);
    TEST_ASSERT_EQUAL(cfg->cfg_sz, 0);
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_howl_if()
{
    esp_ae_howl_cfg_t config = DEFAULT_ESP_GMF_HOWL_CONFIG();
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_howl_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_howl_init(&config, &handle), ESP_GMF_ERR_OK);
    check_element_bypass(handle);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_howl_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_reverb_if()
{
    esp_ae_reverb_cfg_t config = DEFAULT_ESP_GMF_REVERB_CONFIG();
    esp_gmf_obj_handle_t handle;
    float room_size = 0.0f;
    float wet_level = 0.0f;
    TEST_ASSERT_EQUAL(esp_gmf_reverb_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_reverb_init(&config, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_element_process_open((esp_gmf_element_handle_t)handle, NULL), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_reverb_set_room_size(NULL, 0.5f), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_reverb_get_room_size(NULL, &room_size), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_reverb_get_room_size(handle, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_reverb_set_room_size(handle, 0.6f), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_reverb_get_room_size(handle, &room_size), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_reverb_set_wet_level(handle, -12.0f), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_reverb_get_wet_level(handle, &wet_level), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_reverb_reset(NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_reverb_reset(handle), ESP_GMF_ERR_OK);
    check_element_bypass(handle);
    TEST_ASSERT_EQUAL(esp_gmf_element_process_close((esp_gmf_element_handle_t)handle, NULL), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_reverb_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_delay_if()
{
    esp_ae_delay_cfg_t config = DEFAULT_ESP_GMF_DELAY_CONFIG();
    esp_gmf_obj_handle_t handle;
    uint16_t delay_time_ms = 0;
    float mix_ratio = 0.0f;
    TEST_ASSERT_EQUAL(esp_gmf_delay_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_delay_init(&config, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_element_process_open((esp_gmf_element_handle_t)handle, NULL), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_delay_set_delay_time(NULL, 100), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_delay_get_delay_time(NULL, &delay_time_ms), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_delay_get_delay_time(handle, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_delay_get_max_delay(NULL, &delay_time_ms), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_delay_get_max_delay(handle, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_delay_get_max_delay(handle, &delay_time_ms), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL_UINT16(config.max_delay_ms, delay_time_ms);
    TEST_ASSERT_EQUAL(esp_gmf_delay_set_delay_time(handle, 150), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_delay_get_delay_time(handle, &delay_time_ms), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_delay_set_mix_ratio(handle, 0.5f), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_delay_get_mix_ratio(handle, &mix_ratio), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_delay_reset(NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_delay_reset(handle), ESP_GMF_ERR_OK);
    check_element_bypass(handle);
    TEST_ASSERT_EQUAL(esp_gmf_element_process_close((esp_gmf_element_handle_t)handle, NULL), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_delay_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_io_embed_flash_if()
{
    embed_flash_io_cfg_t config = EMBED_FLASH_CFG_DEFAULT();
    esp_gmf_obj_handle_t handle;
    const embed_item_info_t context = {0};
    int max_num = 3;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_io_embed_flash_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_io_embed_flash_init(&config, &handle), ESP_GMF_ERR_OK);
    // Set context function test
    TEST_ASSERT_EQUAL(esp_gmf_io_embed_flash_set_context(NULL, &context, max_num), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_io_embed_flash_set_context(handle, NULL, max_num), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_io_embed_flash_set_context(handle, &context, max_num), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_io_file_if()
{
    file_io_cfg_t config = FILE_IO_CFG_DEFAULT();
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_io_file_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    config.dir = ESP_GMF_IO_DIR_NONE;
    TEST_ASSERT_EQUAL(esp_gmf_io_file_init(&config, &handle), ESP_GMF_ERR_NOT_SUPPORT);
    config.dir = ESP_GMF_IO_DIR_READER;
    TEST_ASSERT_EQUAL(esp_gmf_io_file_init(&config, &handle), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_io_http_if()
{
    http_io_cfg_t config = HTTP_STREAM_CFG_DEFAULT();
    esp_gmf_obj_handle_t handle;
    const char *cert = "test";
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_io_http_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    config.dir = ESP_GMF_IO_DIR_NONE;
    TEST_ASSERT_EQUAL(esp_gmf_io_http_init(&config, &handle), ESP_GMF_ERR_NOT_SUPPORT);
    config.dir = ESP_GMF_IO_DIR_READER;
    TEST_ASSERT_EQUAL(esp_gmf_io_http_init(&config, &handle), ESP_GMF_ERR_OK);
    // Reset function test
    TEST_ASSERT_EQUAL(esp_gmf_io_http_reset(NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_io_http_reset(handle), ESP_GMF_ERR_OK);
    // Set server cert function test
    TEST_ASSERT_EQUAL(esp_gmf_io_http_set_server_cert(NULL, cert), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_io_http_set_server_cert(handle, cert), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_io_i2s_if()
{
    i2s_pdm_io_cfg_t config = ESP_GMF_IO_I2S_PDM_CFG_DEFAULT();
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_io_i2s_pdm_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    config.dir = ESP_GMF_IO_DIR_NONE;
    TEST_ASSERT_EQUAL(esp_gmf_io_i2s_pdm_init(&config, &handle), ESP_GMF_ERR_NOT_SUPPORT);
    config.dir = ESP_GMF_IO_DIR_READER;
    TEST_ASSERT_EQUAL(esp_gmf_io_i2s_pdm_init(&config, &handle), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_copier_if()
{
    esp_gmf_copier_cfg_t config;
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_copier_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_copier_init(&config, &handle), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_audio_muxer_if()
{
    esp_gmf_audio_muxer_cfg_t config = {
        .muxer_type = ESP_MUXER_TYPE_TS,
        .output_type = ESP_GMF_AUDIO_MUXER_OUTPUT_STREAMING,
        .url_pattern = NULL,
        .url_ctx = NULL,
        .slice_duration = 600000,
        .codec = ESP_MUXER_ADEC_AAC,
    };
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_audio_muxer_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_audio_muxer_init(NULL, &handle), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_audio_muxer_init(&config, &handle), ESP_GMF_ERR_OK);
    // Verify configuration after init
    esp_gmf_audio_muxer_cfg_t *cfg = OBJ_GET_CFG(handle);
    TEST_ASSERT_NOT_EQUAL(NULL, cfg);
    TEST_ASSERT_EQUAL(cfg->muxer_type, ESP_MUXER_TYPE_TS);
    TEST_ASSERT_EQUAL(cfg->output_type, ESP_GMF_AUDIO_MUXER_OUTPUT_STREAMING);
    TEST_ASSERT_EQUAL(cfg->codec, ESP_MUXER_ADEC_AAC);
    TEST_ASSERT_EQUAL(cfg->slice_duration, 600000);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

static void test_audio_conversion_description(void)
{
    esp_gmf_obj_handle_t handle = NULL;

    esp_asrc_cfg_t asrc_config = DEFAULT_ESP_GMF_ASRC_CONFIG();
    float asrc_weight[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    asrc_config.weight = asrc_weight;
    asrc_config.weight_len = 4;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_asrc_init(&asrc_config, &handle));
    esp_asrc_cfg_t *effective_asrc_config = OBJ_GET_CFG(handle);
    TEST_ASSERT_NOT_EQUAL(asrc_weight, effective_asrc_config->weight);
    check_runtime_method_pair(handle, AMETHOD(RATE_CVT, SET_DEST_RATE),
                              AMETHOD(RATE_CVT, GET_DEST_RATE));
    check_runtime_method_pair(handle, AMETHOD(CH_CVT, SET_DEST_CH),
                              AMETHOD(CH_CVT, GET_DEST_CH));
    check_runtime_method_pair(handle, AMETHOD(BIT_CVT, SET_DEST_BITS),
                              AMETHOD(BIT_CVT, GET_DEST_BITS));
    uint32_t rate = 32000;
    uint8_t channel = 1;
    uint8_t bits = 24;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(RATE_CVT, SET_DEST_RATE), (uint8_t *)&rate, sizeof(rate)));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(CH_CVT, SET_DEST_CH), &channel, sizeof(channel)));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(BIT_CVT, SET_DEST_BITS), &bits, sizeof(bits)));
    rate = 0;
    channel = 0;
    bits = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(RATE_CVT, GET_DEST_RATE), (uint8_t *)&rate, sizeof(rate)));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(CH_CVT, GET_DEST_CH), &channel, sizeof(channel)));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(BIT_CVT, GET_DEST_BITS), &bits, sizeof(bits)));
    TEST_ASSERT_EQUAL_UINT32(32000, rate);
    TEST_ASSERT_EQUAL_UINT8(1, channel);
    TEST_ASSERT_EQUAL_UINT8(24, bits);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(handle));

    esp_ae_mixer_cfg_t mixer_config = DEFAULT_ESP_GMF_MIXER_CONFIG();
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_mixer_init(&mixer_config, &handle));
    check_runtime_method_pair(handle, AMETHOD(MIXER, SET_INFO), AMETHOD(MIXER, GET_INFO));
    check_runtime_method_pair(handle, AMETHOD(MIXER, SET_MODE), AMETHOD(MIXER, GET_MODE));
    const esp_gmf_method_t *mode_method = get_element_method(handle, AMETHOD(MIXER, SET_MODE));
    TEST_ASSERT_NOT_NULL(mode_method->args_desc->constraint);
    TEST_ASSERT_EQUAL_STRING("src_num", mode_method->args_desc->constraint->index_count_config_path);
    uint8_t mode_value[sizeof(uint8_t) + sizeof(int32_t)] = {0};
    int32_t mode = ESP_AE_MIXER_MODE_FADE_DOWNWARD;
    memcpy(mode_value + sizeof(uint8_t), &mode, sizeof(mode));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(MIXER, SET_MODE), mode_value, sizeof(mode_value)));
    memset(mode_value + sizeof(uint8_t), 0, sizeof(mode));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(MIXER, GET_MODE), mode_value, sizeof(mode_value)));
    memcpy(&mode, mode_value + sizeof(uint8_t), sizeof(mode));
    TEST_ASSERT_EQUAL_INT32(ESP_AE_MIXER_MODE_FADE_DOWNWARD, mode);
    uint8_t info_value[sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint8_t)] = {0};
    rate = 44100;
    channel = 1;
    bits = 24;
    memcpy(info_value, &rate, sizeof(rate));
    info_value[sizeof(rate)] = channel;
    info_value[sizeof(rate) + sizeof(channel)] = bits;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(MIXER, SET_INFO), info_value, sizeof(info_value)));
    memset(info_value, 0, sizeof(info_value));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(MIXER, GET_INFO), info_value, sizeof(info_value)));
    memcpy(&rate, info_value, sizeof(rate));
    TEST_ASSERT_EQUAL_UINT32(44100, rate);
    TEST_ASSERT_EQUAL_UINT8(1, info_value[sizeof(rate)]);
    TEST_ASSERT_EQUAL_UINT8(24, info_value[sizeof(rate) + sizeof(uint8_t)]);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(handle));
}

static void test_audio_dynamics_description(void)
{
    const esp_gmf_args_desc_t *arg = NULL;
    esp_gmf_obj_handle_t handle = NULL;

    esp_ae_drc_cfg_t drc_config = DEFAULT_ESP_GMF_DRC_CONFIG();
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_drc_init(&drc_config, &handle));
    check_runtime_method_pair(handle, AMETHOD(DRC, SET_ATTACK), AMETHOD(DRC, GET_ATTACK));
    check_runtime_method_pair(handle, AMETHOD(DRC, SET_RELEASE), AMETHOD(DRC, GET_RELEASE));
    check_runtime_method_pair(handle, AMETHOD(DRC, SET_HOLD), AMETHOD(DRC, GET_HOLD));
    check_runtime_method_pair(handle, AMETHOD(DRC, SET_MAKEUP), AMETHOD(DRC, GET_MAKEUP));
    check_runtime_method_pair(handle, AMETHOD(DRC, SET_KNEE), AMETHOD(DRC, GET_KNEE));
    const esp_gmf_method_t *points_method = get_element_method(handle, AMETHOD(DRC, SET_POINTS));
    TEST_ASSERT_FALSE(points_method->runtime_safe);
    TEST_ASSERT_NULL(points_method->getter);
    TEST_ASSERT_FALSE(get_element_method(handle, AMETHOD(DRC, GET_POINTS))->runtime_safe);
    TEST_ASSERT_TRUE(get_element_method(handle, AMETHOD(DRC, GET_POINT_NUM))->runtime_safe);
    uint16_t attack = 25;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(DRC, SET_ATTACK), (uint8_t *)&attack, sizeof(attack)));
    attack = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(DRC, GET_ATTACK), (uint8_t *)&attack, sizeof(attack)));
    TEST_ASSERT_EQUAL_UINT16(25, attack);
    float makeup = 2.5f;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(DRC, SET_MAKEUP), (uint8_t *)&makeup, sizeof(makeup)));
    esp_ae_drc_curve_point points[2] = {
        {.x = 0.0f, .y = -10.0f},
        {.x = -100.0f, .y = -100.0f},
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_drc_set_points(handle, points, 2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(handle));

    esp_ae_mbc_config_t mbc_config = DEFAULT_ESP_GMF_MBC_CONFIG();
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_mbc_init(&mbc_config, &handle));
    check_runtime_method_pair(handle, AMETHOD(MBC, SET_PARA), AMETHOD(MBC, GET_PARA));
    check_runtime_method_pair(handle, AMETHOD(MBC, SET_FC), AMETHOD(MBC, GET_FC));
    check_runtime_method_pair(handle, AMETHOD(MBC, SET_SOLO), AMETHOD(MBC, GET_SOLO));
    check_runtime_method_pair(handle, AMETHOD(MBC, SET_BYPASS), AMETHOD(MBC, GET_BYPASS));
    const esp_gmf_method_t *para_method = get_element_method(handle, AMETHOD(MBC, SET_PARA));
    TEST_ASSERT_NOT_NULL(para_method->args_desc->constraint);
    TEST_ASSERT_EQUAL_STRING("band_count", para_method->args_desc->constraint->index_count_config_path);
    const esp_gmf_args_desc_t *para_args = para_method->args_desc->next->val;
    arg = find_config_arg(para_args, AMETHOD_ARG(MBC, SET_PARA, PARA_MAKEUP));
    TEST_ASSERT_NOT_NULL(arg->constraint);
    TEST_ASSERT_EQUAL_FLOAT(-10.0, arg->constraint->minimum.f64);
    const esp_gmf_method_t *fc_method = get_element_method(handle, AMETHOD(MBC, SET_FC));
    TEST_ASSERT_EQUAL_STRING("fc_count", fc_method->args_desc->constraint->index_count_config_path);
    uint8_t fc_value[sizeof(uint8_t) + sizeof(uint32_t)] = {0};
    uint32_t fc = 500;
    memcpy(fc_value + sizeof(uint8_t), &fc, sizeof(fc));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(MBC, SET_FC), fc_value, sizeof(fc_value)));
    memset(fc_value + sizeof(uint8_t), 0, sizeof(fc));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(MBC, GET_FC), fc_value, sizeof(fc_value)));
    memcpy(&fc, fc_value + sizeof(uint8_t), sizeof(fc));
    TEST_ASSERT_EQUAL_UINT32(500, fc);
    esp_ae_mbc_para_t para = {
        .threshold = -18.0f,
        .ratio = 2.0f,
        .makeup_gain = 3.0f,
        .attack_time = 5,
        .release_time = 100,
        .hold_time = 2,
        .knee_width = 1.0f,
    };
    uint8_t para_value[sizeof(uint8_t) + sizeof(esp_ae_mbc_para_t)] = {0};
    memcpy(para_value + sizeof(uint8_t), &para, sizeof(para));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(MBC, SET_PARA), para_value, sizeof(para_value)));
    memset(para_value + sizeof(uint8_t), 0, sizeof(para));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(MBC, GET_PARA), para_value, sizeof(para_value)));
    esp_ae_mbc_para_t para_read = {0};
    memcpy(&para_read, para_value + sizeof(uint8_t), sizeof(para_read));
    TEST_ASSERT_EQUAL_FLOAT(para.makeup_gain, para_read.makeup_gain);
    uint8_t enable_value[2] = {0, 1};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(MBC, SET_SOLO), enable_value, sizeof(enable_value)));
    enable_value[1] = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(MBC, GET_SOLO), enable_value, sizeof(enable_value)));
    TEST_ASSERT_EQUAL_UINT8(1, enable_value[1]);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(handle));
}

static void test_audio_config_only_description(void)
{
    esp_gmf_obj_handle_t handle = NULL;

    esp_audio_simple_dec_cfg_t dec_config = DEFAULT_ESP_GMF_AUDIO_DEC_CONFIG();
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_init(&dec_config, &handle));
    TEST_ASSERT_FALSE(get_element_method(handle, AMETHOD(DECODER, RECONFIG))->runtime_safe);
    TEST_ASSERT_FALSE(get_element_method(handle, AMETHOD(DECODER, RECONFIG_BY_SND_INFO))->runtime_safe);
    dec_config.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    dec_config.use_frame_dec = true;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig(handle, &dec_config));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(handle));

    esp_gmf_info_sound_t sound_info = {
        .sample_rates = 16000,
        .channels = 1,
        .bits = 16,
    };
    esp_gmf_event_pkt_t sound_event = {
        .type = ESP_GMF_EVT_TYPE_REPORT_INFO,
        .sub = ESP_GMF_INFO_SOUND,
        .payload = &sound_info,
        .payload_size = sizeof(sound_info),
    };
    esp_ae_howl_cfg_t howl_config = DEFAULT_ESP_GMF_HOWL_CONFIG();
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_howl_init(&howl_config, &handle));
    check_runtime_method_pair(handle, AMETHOD(HOWL, SET_PAPR_TH), AMETHOD(HOWL, GET_PAPR_TH));
    check_runtime_method_pair(handle, AMETHOD(HOWL, SET_ENABLE_IMSD), AMETHOD(HOWL, GET_ENABLE_IMSD));
    const esp_gmf_method_t *papr_method = get_element_method(handle, AMETHOD(HOWL, SET_PAPR_TH));
    TEST_ASSERT_NOT_NULL(papr_method->args_desc->constraint);
    TEST_ASSERT_EQUAL_FLOAT(-10.0f, papr_method->args_desc->constraint->minimum.f64);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, papr_method->args_desc->constraint->maximum.f64);
    sound_event.from = handle;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      ESP_GMF_ELEMENT_GET(handle)->ops.event_receiver(&sound_event, handle));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(handle));

    esp_gmf_interleave_cfg interleave_config = DEFAULT_ESP_GMF_INTERLEAVE_CONFIG();
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_interleave_init(&interleave_config, &handle));
    sound_event.from = handle;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      ESP_GMF_ELEMENT_GET(handle)->ops.event_receiver(&sound_event, handle));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(handle));

    esp_gmf_deinterleave_cfg deinterleave_config = DEFAULT_ESP_GMF_DEINTERLEAVE_CONFIG();
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_deinterleave_init(&deinterleave_config, &handle));
    sound_event.from = handle;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      ESP_GMF_ELEMENT_GET(handle)->ops.event_receiver(&sound_event, handle));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(handle));

    esp_gmf_audio_muxer_cfg_t muxer_config = {
        .muxer_type = ESP_MUXER_TYPE_TS,
        .codec = ESP_MUXER_ADEC_AAC,
        .output_type = ESP_GMF_AUDIO_MUXER_OUTPUT_STREAMING,
        .slice_duration = 60000,
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_muxer_init(&muxer_config, &handle));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(handle));
}

static void test_audio_alc_eq_method_control(void)
{
    esp_gmf_obj_handle_t handle = NULL;

    esp_ae_alc_cfg_t alc_config = DEFAULT_ESP_GMF_ALC_CONFIG();
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_alc_init(&alc_config, &handle));
    check_runtime_method_pair(handle, AMETHOD(ALC, SET_GAIN), AMETHOD(ALC, GET_GAIN));
    const esp_gmf_method_t *setter = get_element_method(handle, AMETHOD(ALC, SET_GAIN));
    const esp_gmf_method_t *getter = get_element_method(handle, AMETHOD(ALC, GET_GAIN));
    const esp_gmf_args_desc_t *index_arg = setter->args_desc;
    const esp_gmf_args_desc_t *gain_arg = index_arg->next;
    TEST_ASSERT_EQUAL(ESP_GMF_ARGS_TYPE_UINT8, index_arg->type);
    TEST_ASSERT_NOT_NULL(index_arg->constraint);
    TEST_ASSERT_EQUAL_STRING("channel", index_arg->constraint->index_count_config_path);
    TEST_ASSERT_EQUAL(ESP_GMF_ARGS_TYPE_INT8, gain_arg->type);
    TEST_ASSERT_NOT_NULL(gain_arg->constraint);
    TEST_ASSERT_EQUAL_INT64(-64, gain_arg->constraint->minimum.i64);
    TEST_ASSERT_EQUAL_INT64(63, gain_arg->constraint->maximum.i64);
    TEST_ASSERT_EQUAL_INT64(1, gain_arg->constraint->step.i64);

    uint8_t channel = alc_config.channel;
    size_t alc_value_size = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_args_desc_get_total_size(setter->args_desc, &alc_value_size));
    TEST_ASSERT_EQUAL(sizeof(uint8_t) + sizeof(int8_t), alc_value_size);
    uint8_t alc_value[sizeof(uint8_t) + sizeof(int8_t)] = {0};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_process_open(handle, NULL));
    for (uint8_t idx = 0; idx < channel; idx++) {
        int8_t gain = idx == 0 ? -12 : 6;
        memset(alc_value, 0, sizeof(alc_value));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                          esp_gmf_args_set_value(setter->args_desc, AMETHOD_ARG(ALC, SET_GAIN, IDX),
                                                 alc_value, &idx, sizeof(idx)));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                          esp_gmf_args_set_value(setter->args_desc, AMETHOD_ARG(ALC, SET_GAIN, GAIN),
                                                 alc_value, (uint8_t *)&gain, sizeof(gain)));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                          esp_gmf_element_exe_method(handle, AMETHOD(ALC, SET_GAIN),
                                                     alc_value, alc_value_size));

        memset(alc_value, 0, sizeof(alc_value));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                          esp_gmf_args_set_value(getter->args_desc, AMETHOD_ARG(ALC, GET_GAIN, IDX),
                                                 alc_value, &idx, sizeof(idx)));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                          esp_gmf_element_exe_method(handle, AMETHOD(ALC, GET_GAIN),
                                                     alc_value, alc_value_size));
        const esp_gmf_args_desc_t *read_gain_arg =
            find_config_arg(getter->args_desc, AMETHOD_ARG(ALC, GET_GAIN, GAIN));
        int8_t read_gain = 0;
        memcpy(&read_gain, alc_value + read_gain_arg->offset, sizeof(read_gain));
        TEST_ASSERT_EQUAL_INT8(gain, read_gain);
    }
    memset(alc_value, 0, sizeof(alc_value));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_args_set_value(setter->args_desc, AMETHOD_ARG(ALC, SET_GAIN, IDX),
                                             alc_value, &channel, sizeof(channel)));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG,
                      esp_gmf_element_exe_method(handle, AMETHOD(ALC, SET_GAIN),
                                                 alc_value, alc_value_size));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_process_close(handle, NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(handle));

    esp_ae_eq_cfg_t eq_config = DEFAULT_ESP_GMF_EQ_CONFIG();
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_eq_init(&eq_config, &handle));
    check_runtime_method_pair(handle, AMETHOD(EQ, SET_PARA), AMETHOD(EQ, GET_PARA));
    setter = get_element_method(handle, AMETHOD(EQ, SET_PARA));
    getter = get_element_method(handle, AMETHOD(EQ, GET_PARA));
    index_arg = setter->args_desc;
    TEST_ASSERT_NOT_NULL(index_arg->constraint);
    TEST_ASSERT_EQUAL_STRING("filter_num", index_arg->constraint->index_count_config_path);
    const esp_gmf_args_desc_t *para_args = setter->args_desc->next->val;
    const esp_gmf_args_desc_t *type_arg =
        find_config_arg(para_args, AMETHOD_ARG(EQ, SET_PARA, PARA_FT));
    const esp_gmf_args_desc_t *q_arg =
        find_config_arg(para_args, AMETHOD_ARG(EQ, SET_PARA, PARA_Q));
    gain_arg = find_config_arg(para_args, AMETHOD_ARG(EQ, SET_PARA, PARA_GAIN));
    TEST_ASSERT_EQUAL(ESP_GMF_ARGS_TYPE_UINT32, type_arg->type);
    TEST_ASSERT_NOT_NULL(type_arg->constraint);
    TEST_ASSERT_EQUAL_UINT64(ESP_AE_EQ_FILTER_HIGH_PASS, type_arg->constraint->minimum.u64);
    TEST_ASSERT_EQUAL_UINT64(ESP_AE_EQ_FILTER_LOW_SHELF, type_arg->constraint->maximum.u64);
    TEST_ASSERT_EQUAL_UINT64(1, type_arg->constraint->step.u64);
    TEST_ASSERT_EQUAL(ESP_GMF_ARGS_TYPE_FLOAT, q_arg->type);
    TEST_ASSERT_NOT_NULL(q_arg->constraint);
    TEST_ASSERT_EQUAL_FLOAT(0.1, q_arg->constraint->minimum.f64);
    TEST_ASSERT_EQUAL_FLOAT(20.0, q_arg->constraint->maximum.f64);
    TEST_ASSERT_EQUAL_FLOAT(0.1, q_arg->constraint->step.f64);
    TEST_ASSERT_EQUAL(ESP_GMF_ARGS_TYPE_FLOAT, gain_arg->type);
    TEST_ASSERT_NOT_NULL(gain_arg->constraint);
    TEST_ASSERT_EQUAL_FLOAT(-15.0, gain_arg->constraint->minimum.f64);
    TEST_ASSERT_EQUAL_FLOAT(15.0, gain_arg->constraint->maximum.f64);
    TEST_ASSERT_EQUAL_FLOAT(0.1, gain_arg->constraint->step.f64);

    uint8_t filter_num = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_eq_get_filter_num(handle, &filter_num));
    TEST_ASSERT_EQUAL_UINT8(10, filter_num);
    size_t eq_value_size = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_args_desc_get_total_size(setter->args_desc, &eq_value_size));
    TEST_ASSERT_EQUAL(sizeof(uint8_t) + sizeof(esp_ae_eq_filter_para_t), eq_value_size);
    uint8_t eq_value[sizeof(uint8_t) + sizeof(esp_ae_eq_filter_para_t)] = {0};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_process_open(handle, NULL));
    for (uint8_t idx = 0; idx < filter_num; idx++) {
        uint32_t filter_type = ESP_AE_EQ_FILTER_PEAK;
        uint32_t fc = 31U << idx;
        float q = 1.0f + 0.5f * idx;
        float gain = -4.0f + idx;
        memset(eq_value, 0, sizeof(eq_value));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                          esp_gmf_args_set_value(setter->args_desc, AMETHOD_ARG(EQ, SET_PARA, IDX),
                                                 eq_value, &idx, sizeof(idx)));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                          esp_gmf_args_set_value(setter->args_desc, AMETHOD_ARG(EQ, SET_PARA, PARA_FT),
                                                 eq_value, (uint8_t *)&filter_type, sizeof(filter_type)));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                          esp_gmf_args_set_value(setter->args_desc, AMETHOD_ARG(EQ, SET_PARA, PARA_FC),
                                                 eq_value, (uint8_t *)&fc, sizeof(fc)));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                          esp_gmf_args_set_value(setter->args_desc, AMETHOD_ARG(EQ, SET_PARA, PARA_Q),
                                                 eq_value, (uint8_t *)&q, sizeof(q)));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                          esp_gmf_args_set_value(setter->args_desc, AMETHOD_ARG(EQ, SET_PARA, PARA_GAIN),
                                                 eq_value, (uint8_t *)&gain, sizeof(gain)));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                          esp_gmf_element_exe_method(handle, AMETHOD(EQ, SET_PARA),
                                                     eq_value, eq_value_size));

        memset(eq_value, 0, sizeof(eq_value));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                          esp_gmf_args_set_value(getter->args_desc, AMETHOD_ARG(EQ, GET_PARA, IDX),
                                                 eq_value, &idx, sizeof(idx)));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                          esp_gmf_element_exe_method(handle, AMETHOD(EQ, GET_PARA),
                                                     eq_value, eq_value_size));
        esp_ae_eq_filter_para_t read_para = {0};
        memcpy(&read_para, eq_value + getter->args_desc->next->offset, sizeof(read_para));
        TEST_ASSERT_EQUAL_UINT32(filter_type, read_para.filter_type);
        TEST_ASSERT_EQUAL_UINT32(fc, read_para.fc);
        TEST_ASSERT_EQUAL_FLOAT(q, read_para.q);
        TEST_ASSERT_EQUAL_FLOAT(gain, read_para.gain);
    }

    uint8_t invalid_index = filter_num;
    memset(eq_value, 0, sizeof(eq_value));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_args_set_value(setter->args_desc, AMETHOD_ARG(EQ, SET_PARA, IDX),
                                             eq_value, &invalid_index, sizeof(invalid_index)));
    TEST_ASSERT_NOT_EQUAL(ESP_GMF_ERR_OK,
                          esp_gmf_element_exe_method(handle, AMETHOD(EQ, SET_PARA),
                                                     eq_value, eq_value_size));
    uint8_t index = 0;
    uint32_t filter_type = ESP_AE_EQ_FILTER_PEAK;
    uint32_t fc = 1000;
    float q = 1.0f;
    float invalid_gain = 30.0f;
    memset(eq_value, 0, sizeof(eq_value));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_args_set_value(setter->args_desc, AMETHOD_ARG(EQ, SET_PARA, IDX),
                                             eq_value, &index, sizeof(index)));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_args_set_value(setter->args_desc, AMETHOD_ARG(EQ, SET_PARA, PARA_FT),
                                             eq_value, (uint8_t *)&filter_type, sizeof(filter_type)));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_args_set_value(setter->args_desc, AMETHOD_ARG(EQ, SET_PARA, PARA_FC),
                                             eq_value, (uint8_t *)&fc, sizeof(fc)));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_args_set_value(setter->args_desc, AMETHOD_ARG(EQ, SET_PARA, PARA_Q),
                                             eq_value, (uint8_t *)&q, sizeof(q)));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_args_set_value(setter->args_desc, AMETHOD_ARG(EQ, SET_PARA, PARA_GAIN),
                                             eq_value, (uint8_t *)&invalid_gain, sizeof(invalid_gain)));
    TEST_ASSERT_NOT_EQUAL(ESP_GMF_ERR_OK,
                          esp_gmf_element_exe_method(handle, AMETHOD(EQ, SET_PARA),
                                                     eq_value, eq_value_size));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_process_close(handle, NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(handle));
}

static void test_audio_space_effects_description(void)
{
    esp_gmf_obj_handle_t handle = NULL;

    esp_ae_delay_cfg_t delay_config = DEFAULT_ESP_GMF_DELAY_CONFIG();
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_delay_init(&delay_config, &handle));
    check_runtime_method_pair(handle, AMETHOD(DELAY, SET_DELAY_TIME), AMETHOD(DELAY, GET_DELAY_TIME));
    check_runtime_method_pair(handle, AMETHOD(DELAY, SET_MIX_RATIO), AMETHOD(DELAY, GET_MIX_RATIO));
    check_runtime_method_pair(handle, AMETHOD(DELAY, SET_FEEDBACK), AMETHOD(DELAY, GET_FEEDBACK));
    TEST_ASSERT_TRUE(get_element_method(handle, AMETHOD(DELAY, GET_MAX_DELAY))->runtime_safe);
    TEST_ASSERT_NULL(get_element_method(handle, AMETHOD(DELAY, GET_MAX_DELAY))->getter);
    TEST_ASSERT_FALSE(get_element_method(handle, AMETHOD(DELAY, RESET))->runtime_safe);
    const esp_gmf_method_t *delay_time_method =
        get_element_method(handle, AMETHOD(DELAY, SET_DELAY_TIME));
    TEST_ASSERT_NOT_NULL(delay_time_method->args_desc->constraint);
    TEST_ASSERT_EQUAL_UINT64(0, delay_time_method->args_desc->constraint->minimum.u64);
    TEST_ASSERT_EQUAL_UINT64(delay_config.max_delay_ms,
                             delay_time_method->args_desc->constraint->maximum.u64);
    TEST_ASSERT_EQUAL_UINT64(1, delay_time_method->args_desc->constraint->step.u64);
    const esp_gmf_method_t *mix_method = get_element_method(handle, AMETHOD(DELAY, SET_MIX_RATIO));
    TEST_ASSERT_NOT_NULL(mix_method->args_desc->constraint);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, mix_method->args_desc->constraint->minimum.f64);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, mix_method->args_desc->constraint->maximum.f64);

    uint16_t delay_time_ms = 120;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(DELAY, SET_DELAY_TIME),
                          (uint8_t *)&delay_time_ms, sizeof(delay_time_ms)));
    delay_time_ms = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(DELAY, GET_DELAY_TIME),
                          (uint8_t *)&delay_time_ms, sizeof(delay_time_ms)));
    TEST_ASSERT_EQUAL_UINT16(120, delay_time_ms);
    uint16_t max_delay_ms = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(DELAY, GET_MAX_DELAY),
                          (uint8_t *)&max_delay_ms, sizeof(max_delay_ms)));
    TEST_ASSERT_EQUAL_UINT16(delay_config.max_delay_ms, max_delay_ms);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(handle));

    esp_ae_delay_cfg_t delay_a = DEFAULT_ESP_GMF_DELAY_CONFIG();
    esp_ae_delay_cfg_t delay_b = DEFAULT_ESP_GMF_DELAY_CONFIG();
    delay_a.max_delay_ms = 200;
    delay_b.max_delay_ms = 800;
    esp_gmf_obj_handle_t handle_a = NULL;
    esp_gmf_obj_handle_t handle_b = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_delay_init(&delay_a, &handle_a));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_delay_init(&delay_b, &handle_b));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_delay_get_max_delay(handle_a, &max_delay_ms));
    TEST_ASSERT_EQUAL_UINT16(200, max_delay_ms);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_delay_get_max_delay(handle_b, &max_delay_ms));
    TEST_ASSERT_EQUAL_UINT16(800, max_delay_ms);
    const esp_gmf_method_t *constraint_a =
        get_element_method(handle_a, AMETHOD(DELAY, SET_DELAY_TIME));
    const esp_gmf_method_t *constraint_b =
        get_element_method(handle_b, AMETHOD(DELAY, SET_DELAY_TIME));
    TEST_ASSERT_EQUAL_UINT64(200, constraint_a->args_desc->constraint->maximum.u64);
    TEST_ASSERT_EQUAL_UINT64(800, constraint_b->args_desc->constraint->maximum.u64);
    delay_a.max_delay_ms = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(handle_a));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_delay_init(&delay_a, &handle_a));
    TEST_ASSERT_EQUAL_UINT64(1000, get_element_method(handle_a, AMETHOD(DELAY, SET_DELAY_TIME))
                                       ->args_desc->constraint->maximum.u64);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_delay_get_max_delay(handle_a, &max_delay_ms));
    TEST_ASSERT_EQUAL_UINT16(0, max_delay_ms);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(handle_a));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(handle_b));

    esp_ae_reverb_cfg_t reverb_config = DEFAULT_ESP_GMF_REVERB_CONFIG();
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_reverb_init(&reverb_config, &handle));
    check_runtime_method_pair(handle, AMETHOD(REVERB, SET_ROOM_SIZE), AMETHOD(REVERB, GET_ROOM_SIZE));
    check_runtime_method_pair(handle, AMETHOD(REVERB, SET_WET_LEVEL), AMETHOD(REVERB, GET_WET_LEVEL));
    check_runtime_method_pair(handle, AMETHOD(REVERB, SET_DAMPING), AMETHOD(REVERB, GET_DAMPING));
    check_runtime_method_pair(handle, AMETHOD(REVERB, SET_DRY_LEVEL), AMETHOD(REVERB, GET_DRY_LEVEL));
    check_runtime_method_pair(handle, AMETHOD(REVERB, SET_PRE_DELAY), AMETHOD(REVERB, GET_PRE_DELAY));
    TEST_ASSERT_FALSE(get_element_method(handle, AMETHOD(REVERB, RESET))->runtime_safe);
    const esp_gmf_method_t *room_method =
        get_element_method(handle, AMETHOD(REVERB, SET_ROOM_SIZE));
    TEST_ASSERT_NOT_NULL(room_method->args_desc->constraint);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, room_method->args_desc->constraint->minimum.f64);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, room_method->args_desc->constraint->maximum.f64);
    const esp_gmf_method_t *wet_method =
        get_element_method(handle, AMETHOD(REVERB, SET_WET_LEVEL));
    TEST_ASSERT_EQUAL_FLOAT(-96.0f, wet_method->args_desc->constraint->minimum.f64);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, wet_method->args_desc->constraint->maximum.f64);
    float room_size = 0.75f;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(REVERB, SET_ROOM_SIZE),
                          (uint8_t *)&room_size, sizeof(room_size)));
    room_size = 0.0f;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(REVERB, GET_ROOM_SIZE),
                          (uint8_t *)&room_size, sizeof(room_size)));
    TEST_ASSERT_EQUAL_FLOAT(0.75f, room_size);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(handle));

    esp_ae_fade_cfg_t fade_config = DEFAULT_ESP_GMF_FADE_CONFIG();
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_fade_init(&fade_config, &handle));
    check_runtime_method_pair(handle, AMETHOD(FADE, SET_CURVE), AMETHOD(FADE, GET_CURVE));
    check_runtime_method_pair(handle, AMETHOD(FADE, SET_TRANSIT_TIME),
                              AMETHOD(FADE, GET_TRANSIT_TIME));
    const esp_gmf_method_t *curve_method = get_element_method(handle, AMETHOD(FADE, SET_CURVE));
    TEST_ASSERT_NOT_NULL(curve_method->args_desc->constraint);
    TEST_ASSERT_EQUAL_INT64(ESP_AE_FADE_CURVE_LINE, curve_method->args_desc->constraint->minimum.i64);
    TEST_ASSERT_EQUAL_INT64(ESP_AE_FADE_CURVE_SQRT, curve_method->args_desc->constraint->maximum.i64);
    int32_t curve = ESP_AE_FADE_CURVE_SQRT;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(FADE, SET_CURVE), (uint8_t *)&curve, sizeof(curve)));
    curve = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method(
                          handle, AMETHOD(FADE, GET_CURVE), (uint8_t *)&curve, sizeof(curve)));
    TEST_ASSERT_EQUAL_INT32(ESP_AE_FADE_CURVE_SQRT, curve);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(handle));
}

TEST_CASE("Test audio element description group 1", "[ESP_GMF_IF_CHECK][leaks=1400]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    test_esp_gmf_bit_cvt_if();
    test_esp_gmf_ch_cvt_if();
    test_esp_gmf_fade_if();
    test_esp_gmf_rate_cvt_if();
    test_esp_gmf_sonic_if();
    test_esp_gmf_enc_if();
}

TEST_CASE("Test audio element description group 2", "[ESP_GMF_IF_CHECK][leaks=1400]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    test_audio_conversion_description();
    test_audio_dynamics_description();
    test_audio_config_only_description();
    test_audio_space_effects_description();
}

TEST_CASE("Test audio ALC and EQ generic method control", "[ESP_GMF_IF_CHECK][ESP_GMF_CONTROL][leaks=1400]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    test_audio_alc_eq_method_control();
}

TEST_CASE("Test element if check", "[ESP_GMF_IF_CHECK][leaks=1400]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    test_esp_gmf_alc_if();
    test_esp_gmf_bit_cvt_if();
    test_esp_gmf_ch_cvt_if();
    test_esp_gmf_deinterleave_if();
    test_esp_gmf_eq_if();
    test_esp_gmf_drc_if();
    test_esp_gmf_mbc_if();
    test_esp_gmf_fade_if();
    test_esp_gmf_interleave_if();
    test_esp_gmf_mixer_if();
    test_esp_gmf_rate_cvt_if();
    test_esp_gmf_asrc_if();
    test_esp_gmf_sonic_if();
    test_esp_gmf_dec_if();
    test_esp_gmf_enc_if();
    test_esp_gmf_howl_if();
    test_esp_gmf_reverb_if();
    test_esp_gmf_delay_if();
    test_esp_gmf_io_embed_flash_if();
    test_esp_gmf_io_file_if();
    test_esp_gmf_io_http_if();
    test_esp_gmf_io_i2s_if();
    test_esp_gmf_copier_if();
    test_esp_gmf_audio_muxer_if();
}

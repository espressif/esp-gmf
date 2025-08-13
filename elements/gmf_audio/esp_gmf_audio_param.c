/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_gmf_method_helper.h"
#include "esp_gmf_audio_param.h"
#include "esp_gmf_audio_methods_def.h"
#define PREPARE_AMETHOD_SETTING()                                                             \
    esp_gmf_method_exec_ctx_t exec_ctx = {};                                                  \
    const esp_gmf_method_t*   method_head = NULL;                                             \
    esp_gmf_element_get_method(self, &method_head);                                           \
    esp_gmf_err_t ret = esp_gmf_method_prepare_exec_ctx(method_head, method_name, &exec_ctx); \
    if (ret != ESP_GMF_ERR_OK) {                                                              \
        return ret;                                                                           \
    }

#define SET_AMETHOD_ARG(arg_name, value) \
    esp_gmf_args_set_value(exec_ctx.method->args_desc, arg_name, exec_ctx.exec_buf, (uint8_t*) &value, sizeof(value));

#define RELEASE_AMETHOD_SETTING()                                                                        \
    ret = exec_ctx.method->func(self, exec_ctx.method->args_desc, exec_ctx.exec_buf, exec_ctx.buf_size); \
    esp_gmf_method_release_exec_ctx(&exec_ctx);                                                          \
    return ret;

esp_gmf_err_t esp_gmf_audio_param_set_dest_rate(esp_gmf_element_handle_t self, uint32_t dest_rate)
{
    const char *method_name = AMETHOD(RATE_CVT, SET_DEST_RATE);
    PREPARE_AMETHOD_SETTING();

    SET_AMETHOD_ARG(AMETHOD_ARG(RATE_CVT, SET_DEST_RATE, RATE), dest_rate);

    RELEASE_AMETHOD_SETTING();
}

esp_gmf_err_t esp_gmf_audio_param_set_dest_bits(esp_gmf_element_handle_t self, uint8_t dest_bits)
{
    const char *method_name = AMETHOD(BIT_CVT, SET_DEST_BITS);
    PREPARE_AMETHOD_SETTING();

    SET_AMETHOD_ARG(AMETHOD_ARG(BIT_CVT, SET_DEST_BITS, BITS), dest_bits);

    RELEASE_AMETHOD_SETTING();
}

esp_gmf_err_t esp_gmf_audio_param_set_dest_ch(esp_gmf_element_handle_t self, uint8_t dest_ch)
{
    const char *method_name = AMETHOD(CH_CVT, SET_DEST_CH);
    PREPARE_AMETHOD_SETTING();

    SET_AMETHOD_ARG(AMETHOD_ARG(CH_CVT, SET_DEST_CH, CH), dest_ch);

    RELEASE_AMETHOD_SETTING();
}

esp_gmf_err_t esp_gmf_audio_param_set_speed(esp_gmf_element_handle_t self, float speed)
{
    const char *method_name = AMETHOD(SONIC, SET_SPEED);
    PREPARE_AMETHOD_SETTING();

    SET_AMETHOD_ARG(AMETHOD_ARG(SONIC, SET_SPEED, SPEED), speed);

    RELEASE_AMETHOD_SETTING();
}

esp_gmf_err_t esp_gmf_audio_param_set_pitch(esp_gmf_element_handle_t self, float pitch)
{
    const char *method_name = AMETHOD(SONIC, SET_PITCH);
    PREPARE_AMETHOD_SETTING();

    SET_AMETHOD_ARG(AMETHOD_ARG(SONIC, SET_PITCH, PITCH), pitch);

    RELEASE_AMETHOD_SETTING();
}

esp_gmf_err_t esp_gmf_audio_param_set_alc_channel_gain(esp_gmf_element_handle_t self, uint8_t ch_idx, float db)
{
    const char *method_name = AMETHOD(ALC, SET_GAIN);
    // ALC currently only support int8 db
    int8_t gain = (int8_t) db;
    PREPARE_AMETHOD_SETTING();

    SET_AMETHOD_ARG(AMETHOD_ARG(ALC, SET_GAIN, IDX), ch_idx);
    SET_AMETHOD_ARG(AMETHOD_ARG(ALC, SET_GAIN, GAIN), gain);

    RELEASE_AMETHOD_SETTING();
}

esp_gmf_err_t esp_gmf_audio_param_set_fade(esp_gmf_element_handle_t self, bool is_fade_in)
{
    const char *method_name = AMETHOD(FADE, SET_MODE);
    // Aligned with ESP_AE_FADE_MODE_FADE_IN  = 1, ESP_AE_FADE_MODE_FADE_OUT = 2
    int32_t mode = is_fade_in ? 1 : 2;

    PREPARE_AMETHOD_SETTING();

    SET_AMETHOD_ARG(AMETHOD_ARG(FADE, SET_MODE, MODE), mode);

    RELEASE_AMETHOD_SETTING();
}

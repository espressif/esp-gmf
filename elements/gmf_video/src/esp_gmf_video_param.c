/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_gmf_video_param.h"

#define PREPARE_VMETHOD_SETTING()                                                             \
    esp_gmf_method_exec_ctx_t exec_ctx = {};                                                  \
    const esp_gmf_method_t*   method_head = NULL;                                             \
    esp_gmf_element_get_method(self, &method_head);                                           \
    esp_gmf_err_t ret = esp_gmf_method_prepare_exec_ctx(method_head, method_name, &exec_ctx); \
    if (ret != ESP_GMF_ERR_OK) {                                                              \
        return ret;                                                                           \
    }

#define SET_METHOD_ARG(arg_name, value) \
    esp_gmf_args_set_value(exec_ctx.method->args_desc, arg_name, exec_ctx.exec_buf, (uint8_t*) &value, sizeof(value));

#define RELEASE_VMETHOD_SETTING()                                                                        \
    ret = exec_ctx.method->func(self, exec_ctx.method->args_desc, exec_ctx.exec_buf, exec_ctx.buf_size); \
    esp_gmf_method_release_exec_ctx(&exec_ctx);                                                          \
    return ret;

esp_gmf_err_t esp_gmf_video_param_set_dst_format(esp_gmf_element_handle_t self, uint32_t dst_fmt)
{
    const char *method_name = VMETHOD(CLR_CVT, SET_DST_FMT);
    PREPARE_VMETHOD_SETTING();

    SET_METHOD_ARG(VMETHOD_ARG(CLR_CVT, SET_DST_FMT, FMT), dst_fmt);

    RELEASE_VMETHOD_SETTING();
}

esp_gmf_err_t esp_gmf_video_param_set_fps(esp_gmf_element_handle_t self, uint16_t fps)
{
    const char *method_name = VMETHOD(FPS_CVT, SET_FPS);
    PREPARE_VMETHOD_SETTING();

    SET_METHOD_ARG(VMETHOD_ARG(FPS_CVT, SET_FPS, FPS), fps);

    RELEASE_VMETHOD_SETTING();
}

esp_gmf_err_t esp_gmf_video_param_set_dst_resolution(esp_gmf_element_handle_t self, esp_gmf_video_resolution_t *res)
{
    const char *method_name = VMETHOD(SCALER, SET_DST_RES);
    PREPARE_VMETHOD_SETTING();

    SET_METHOD_ARG(VMETHOD_ARG(SCALER, SET_DST_RES, WIDTH), res->width);
    SET_METHOD_ARG(VMETHOD_ARG(SCALER, SET_DST_RES, HEIGHT), res->height);

    RELEASE_VMETHOD_SETTING();
}

esp_gmf_err_t esp_gmf_video_param_set_dst_codec(esp_gmf_element_handle_t self, uint32_t dst_codec)
{
    const char *method_name = VMETHOD(ENCODER, SET_DST_CODEC);
    PREPARE_VMETHOD_SETTING();

    SET_METHOD_ARG(VMETHOD_ARG(ENCODER, SET_DST_CODEC, CODEC), dst_codec);

    RELEASE_VMETHOD_SETTING();
}

esp_gmf_err_t esp_gmf_video_param_venc_preset(esp_gmf_element_handle_t self, esp_gmf_info_video_t *vid_info,
                                              uint32_t dst_codec)
{
    const char *method_name = VMETHOD(ENCODER, PRESET);
    PREPARE_VMETHOD_SETTING();

    SET_METHOD_ARG(VMETHOD_ARG(ENCODER, PRESET, SRC_FMT), vid_info->format_id);
    SET_METHOD_ARG(VMETHOD_ARG(ENCODER, PRESET, SRC_WIDTH), vid_info->width);
    SET_METHOD_ARG(VMETHOD_ARG(ENCODER, PRESET, SRC_HEIGHT), vid_info->height);
    SET_METHOD_ARG(VMETHOD_ARG(ENCODER, PRESET, SRC_FPS), vid_info->fps);
    SET_METHOD_ARG(VMETHOD_ARG(ENCODER, PRESET, SRC_BITRATE), vid_info->bitrate);
    SET_METHOD_ARG(VMETHOD_ARG(ENCODER, PRESET, DST_CODEC), dst_codec);

    RELEASE_VMETHOD_SETTING();
}

esp_gmf_err_t esp_gmf_video_param_get_src_fmts_by_codec(esp_gmf_element_handle_t self, uint32_t dst_codec,
                                                        const uint32_t **src_fmts, uint8_t *src_fmts_num)
{
    const char *method_name = VMETHOD(ENCODER, GET_SRC_FMTS);
    PREPARE_VMETHOD_SETTING();

    SET_METHOD_ARG(VMETHOD_ARG(ENCODER, GET_SRC_FMTS, DST_CODEC), dst_codec);
    SET_METHOD_ARG(VMETHOD_ARG(ENCODER, GET_SRC_FMTS, SRC_FMTS_PTR), src_fmts);
    SET_METHOD_ARG(VMETHOD_ARG(ENCODER, GET_SRC_FMTS, SRC_FMTS_NUM_PTR), src_fmts_num);

    RELEASE_VMETHOD_SETTING();
}

esp_gmf_err_t esp_gmf_video_param_set_src_codec(esp_gmf_element_handle_t self, uint32_t src_codec)
{
    const char *method_name = VMETHOD(DECODER, SET_SRC_CODEC);
    PREPARE_VMETHOD_SETTING();

    SET_METHOD_ARG(VMETHOD_ARG(DECODER, SET_SRC_CODEC, CODEC), src_codec);

    RELEASE_VMETHOD_SETTING();
}

esp_gmf_err_t esp_gmf_video_param_set_rotate_angle(esp_gmf_element_handle_t self, uint16_t degree)
{
    const char *method_name = VMETHOD(ROTATOR, SET_ANGLE);
    PREPARE_VMETHOD_SETTING();

    SET_METHOD_ARG(VMETHOD_ARG(ROTATOR, SET_ANGLE, DEGREE), degree);

    RELEASE_VMETHOD_SETTING();
}

esp_gmf_err_t esp_gmf_video_param_set_cropped_region(esp_gmf_element_handle_t self, esp_gmf_video_rgn_t *rgn)
{
    const char *method_name = VMETHOD(CROP, SET_CROP_RGN);
    PREPARE_VMETHOD_SETTING();

    SET_METHOD_ARG(VMETHOD_ARG(CROP, SET_CROP_RGN, X), rgn->x);
    SET_METHOD_ARG(VMETHOD_ARG(CROP, SET_CROP_RGN, Y), rgn->y);
    SET_METHOD_ARG(VMETHOD_ARG(CROP, SET_CROP_RGN, WIDTH), rgn->width);
    SET_METHOD_ARG(VMETHOD_ARG(CROP, SET_CROP_RGN, HEIGHT), rgn->height);

    RELEASE_VMETHOD_SETTING();
}

esp_gmf_err_t esp_gmf_video_param_overlay_enable(esp_gmf_element_handle_t self, bool enable)
{
    const char *method_name = VMETHOD(OVERLAY, OVERLAY_ENABLE);
    PREPARE_VMETHOD_SETTING();

    SET_METHOD_ARG(VMETHOD_ARG(OVERLAY, OVERLAY_ENABLE, ENABLE), enable);

    RELEASE_VMETHOD_SETTING();
}

esp_gmf_err_t esp_gmf_video_param_set_overlay_port(esp_gmf_element_handle_t self, void *port)
{
    const char *method_name = VMETHOD(OVERLAY, SET_PORT);
    PREPARE_VMETHOD_SETTING();

    SET_METHOD_ARG(VMETHOD_ARG(OVERLAY, SET_PORT, PORT), port);

    RELEASE_VMETHOD_SETTING();
}

esp_gmf_err_t esp_gmf_video_param_set_overlay_rgn(esp_gmf_element_handle_t self, esp_gmf_overlay_rgn_info_t *rgn)
{
    const char *method_name = VMETHOD(OVERLAY, SET_RGN);
    PREPARE_VMETHOD_SETTING();

    SET_METHOD_ARG(VMETHOD_ARG(OVERLAY, SET_RGN, FMT), rgn->format_id);
    SET_METHOD_ARG(VMETHOD_ARG(OVERLAY, SET_RGN, X), rgn->dst_rgn.x);
    SET_METHOD_ARG(VMETHOD_ARG(OVERLAY, SET_RGN, Y), rgn->dst_rgn.y);
    SET_METHOD_ARG(VMETHOD_ARG(OVERLAY, SET_RGN, WIDTH), rgn->dst_rgn.width);
    SET_METHOD_ARG(VMETHOD_ARG(OVERLAY, SET_RGN, HEIGHT), rgn->dst_rgn.height);

    RELEASE_VMETHOD_SETTING();
}

esp_gmf_err_t esp_gmf_video_param_get_dst_fmts_by_codec(esp_gmf_element_handle_t self, uint32_t src_codec,
                                                        const uint32_t **dst_fmts, uint8_t *dst_fmts_num)
{
    const char *method_name = VMETHOD(DECODER, GET_DST_FMTS);
    PREPARE_VMETHOD_SETTING();

    SET_METHOD_ARG(VMETHOD_ARG(DECODER, GET_DST_FMTS, SRC_CODEC), src_codec);
    SET_METHOD_ARG(VMETHOD_ARG(DECODER, GET_DST_FMTS, DST_FMTS_PTR), dst_fmts);
    SET_METHOD_ARG(VMETHOD_ARG(DECODER, GET_DST_FMTS, DST_FMTS_NUM_PTR), dst_fmts_num);

    RELEASE_VMETHOD_SETTING();
}

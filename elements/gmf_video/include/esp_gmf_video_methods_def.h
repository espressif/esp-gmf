/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_method_helper.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief  Video general method argument definition
 */
#define ESP_GMF_VIDEO_FORMAT_ARG  "format"
#define ESP_GMF_VIDEO_WIDTH_ARG   "width"
#define ESP_GMF_VIDEO_HEIGHT_ARG  "height"
#define ESP_GMF_VIDEO_FPS_ARG     "fps"
#define ESP_GMF_VIDEO_BITRATE_ARG "bitrate"
#define ESP_GMF_VIDEO_CODEC_ARG   "codec"

/**
 * @brief  Video general region argument definition
 */
#define ESP_GMF_VIDEO_RGN_ARG_X      "x"
#define ESP_GMF_VIDEO_RGN_ARG_Y      "y"
#define ESP_GMF_VIDEO_RGN_ARG_WIDTH  ESP_GMF_VIDEO_WIDTH_ARG
#define ESP_GMF_VIDEO_RGN_ARG_HEIGHT ESP_GMF_VIDEO_HEIGHT_ARG

/**
 * @brief  GMF video method definition
 */
#define VMETHOD_DEF(module, method, str) ESP_GMF_METHOD_DEF(VIDEO, module, method, str)

/**
 * @brief  Get GMF video method string
 */
#define VMETHOD(module, method) ESP_GMF_METHOD_STR(VIDEO, module, method)

/**
 * @brief  GMF video method argument definition
 */
#define VMETHOD_ARG_DEF(module, method, arg, str) ESP_GMF_METHOD_ARG_DEF(VIDEO, module, method, arg, str)

/**
 * @brief  Get GMF video method argument string
 */
#define VMETHOD_ARG(module, method, arg) ESP_GMF_METHOD_ARG_STR(VIDEO, module, method, arg)

/**
 * @brief  Video FPS converter method definition for set frame per second
 */
VMETHOD_DEF(FPS_CVT, SET_FPS, "set_fps");
VMETHOD_ARG_DEF(FPS_CVT, SET_FPS, FPS, ESP_GMF_VIDEO_FPS_ARG);

/**
 * @brief  Video overlay mixer method definition for enable overlay
 */
VMETHOD_DEF(OVERLAY, OVERLAY_ENABLE, "overlay_enable");
VMETHOD_ARG_DEF(OVERLAY, OVERLAY_ENABLE, ENABLE, "enable");

/**
 * @brief  Video overlay mixer method definition for set overlay port
 */
VMETHOD_DEF(OVERLAY, SET_PORT, "overlay_port");
VMETHOD_ARG_DEF(OVERLAY, SET_PORT, PORT, "port");

/**
 * @brief  Video overlay mixer method definition for set overlay region information
 */
VMETHOD_DEF(OVERLAY, SET_RGN, "overlay_rgn");
VMETHOD_ARG_DEF(OVERLAY, SET_RGN, FMT, ESP_GMF_VIDEO_FORMAT_ARG);
VMETHOD_ARG_DEF(OVERLAY, SET_RGN, X, ESP_GMF_VIDEO_RGN_ARG_X);
VMETHOD_ARG_DEF(OVERLAY, SET_RGN, Y, ESP_GMF_VIDEO_RGN_ARG_Y);
VMETHOD_ARG_DEF(OVERLAY, SET_RGN, WIDTH, ESP_GMF_VIDEO_RGN_ARG_WIDTH);
VMETHOD_ARG_DEF(OVERLAY, SET_RGN, HEIGHT, ESP_GMF_VIDEO_RGN_ARG_HEIGHT);
VMETHOD_ARG_DEF(OVERLAY, SET_RGN, RGN_INDEX, "rgn_index");
VMETHOD_ARG_DEF(OVERLAY, SET_RGN, HAS_TRANS_COLOR, "has_trans_color");
VMETHOD_ARG_DEF(OVERLAY, SET_RGN, TRANS_COLOR, "trans_color");

/**
 * @brief  Video overlay mixer method definition for set overlay region alpha
 */
VMETHOD_DEF(OVERLAY, SET_ALPHA, "set_alpha");
VMETHOD_ARG_DEF(OVERLAY, SET_ALPHA, ALPHA, "alpha");

/**
 * @brief  Video color convert method definition for set destination format
 */
VMETHOD_DEF(CLR_CVT, SET_DST_FMT, "set_dst_fmt");
VMETHOD_ARG_DEF(CLR_CVT, SET_DST_FMT, FMT, ESP_GMF_VIDEO_FORMAT_ARG);

/**
 * @brief  Video scaler method definition for set destination resolution
 */
VMETHOD_DEF(SCALER, SET_DST_RES, "set_dst_res");
VMETHOD_ARG_DEF(SCALER, SET_DST_RES, WIDTH, ESP_GMF_VIDEO_RGN_ARG_WIDTH);
VMETHOD_ARG_DEF(SCALER, SET_DST_RES, HEIGHT, ESP_GMF_VIDEO_RGN_ARG_HEIGHT);

/**
 * @brief  Video cropper method definition for set cropped region
 */
VMETHOD_DEF(CROP, SET_CROP_RGN, "set_crop_rgn");
VMETHOD_ARG_DEF(CROP, SET_CROP_RGN, X, ESP_GMF_VIDEO_RGN_ARG_X);
VMETHOD_ARG_DEF(CROP, SET_CROP_RGN, Y, ESP_GMF_VIDEO_RGN_ARG_Y);
VMETHOD_ARG_DEF(CROP, SET_CROP_RGN, WIDTH, ESP_GMF_VIDEO_RGN_ARG_WIDTH);
VMETHOD_ARG_DEF(CROP, SET_CROP_RGN, HEIGHT, ESP_GMF_VIDEO_RGN_ARG_HEIGHT);

/**
 * @brief  Video rotator method definition for set rotate degree
 */
VMETHOD_DEF(ROTATOR, SET_ANGLE, "set_angle");
VMETHOD_ARG_DEF(ROTATOR, SET_ANGLE, DEGREE, "degree");

/**
 * @brief  Video encoder method definition for set output codec (e.g., H.264, MJPEG)
 */
VMETHOD_DEF(ENCODER, SET_DST_CODEC, "set_dst_codec");
VMETHOD_ARG_DEF(ENCODER, SET_DST_CODEC, CODEC, ESP_GMF_VIDEO_CODEC_ARG);

/**
 * @brief  Video encoder method definition for set output bitrate
 */
VMETHOD_DEF(ENCODER, SET_BITRATE, "set_bitrate");
VMETHOD_ARG_DEF(ENCODER, SET_BITRATE, BITRATE, ESP_GMF_VIDEO_BITRATE_ARG);

/**
 * @brief  Video encoder method definition for get input formats according output codec
 */
VMETHOD_DEF(ENCODER, GET_SRC_FMTS, "get_src_fmts");
VMETHOD_ARG_DEF(ENCODER, GET_SRC_FMTS, DST_CODEC, "dst_codec");
VMETHOD_ARG_DEF(ENCODER, GET_SRC_FMTS, SRC_FMTS_PTR, "src_fmts");
VMETHOD_ARG_DEF(ENCODER, GET_SRC_FMTS, SRC_FMTS_NUM_PTR, "src_fmts_num");

/**
 * @brief  Video encoder method definition for presetting including input video information and output codec
 *
 * @note  This method is specially used when user want to query things from encoder and provide encode basic settings
 */
VMETHOD_DEF(ENCODER, PRESET, "venc_preset");
VMETHOD_ARG_DEF(ENCODER, PRESET, SRC_FMT, "src_fmt");
VMETHOD_ARG_DEF(ENCODER, PRESET, SRC_WIDTH, ESP_GMF_VIDEO_WIDTH_ARG);
VMETHOD_ARG_DEF(ENCODER, PRESET, SRC_HEIGHT, ESP_GMF_VIDEO_HEIGHT_ARG);
VMETHOD_ARG_DEF(ENCODER, PRESET, SRC_FPS, ESP_GMF_VIDEO_FPS_ARG);
VMETHOD_ARG_DEF(ENCODER, PRESET, SRC_BITRATE, ESP_GMF_VIDEO_BITRATE_ARG);
VMETHOD_ARG_DEF(ENCODER, PRESET, DST_CODEC, "dst_codec");

/**
 * @brief  Video encoder method definition for get encode frame size
 */
VMETHOD_DEF(ENCODER, GET_FRAME_SIZE, "get_frm_size");
VMETHOD_ARG_DEF(ENCODER, GET_FRAME_SIZE, DST_FRM_SIZE_PTR, "dst_frm_size");

/**
 * @brief  Video decoder method definition for set source codec (e.g., H.264, MJPEG)
 */
VMETHOD_DEF(DECODER, SET_SRC_CODEC, "set_src_codec");
VMETHOD_ARG_DEF(DECODER, SET_SRC_CODEC, CODEC, ESP_GMF_VIDEO_CODEC_ARG);

/**
 * @brief  Video decoder method definition for set destination format (e.g., YUV420P, RGB565LE)
 *         Reuse `VMETHOD_DEF(CLR_CVT, SET_DST_FMT, "set_dst_fmt")` for same function
 */

/**
 * @brief  Video decoder method definition for get output formats according input codec
 */
VMETHOD_DEF(DECODER, GET_DST_FMTS, "get_dst_fmts");
VMETHOD_ARG_DEF(DECODER, GET_DST_FMTS, SRC_CODEC, "src_codec");
VMETHOD_ARG_DEF(DECODER, GET_DST_FMTS, DST_FMTS_PTR, "dst_fmts");
VMETHOD_ARG_DEF(DECODER, GET_DST_FMTS, DST_FMTS_NUM_PTR, "dst_fmts_num");

#ifdef __cplusplus
}
#endif /* __cplusplus */

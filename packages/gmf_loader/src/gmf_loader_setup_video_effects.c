/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"
#include "esp_gmf_err.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_video_ppa.h"
#include "esp_gmf_video_fps_cvt.h"
#include "esp_gmf_video_overlay.h"
#include "esp_gmf_video_color_convert.h"
#include "esp_gmf_video_rotate.h"
#include "esp_gmf_video_scale.h"
#include "esp_gmf_video_crop.h"

static const char *TAG = "GMF_SETUP_VID_EFFECTS";

#if defined(CONFIG_GMF_VIDEO_EFFECTS_INIT_PPA)
static esp_gmf_err_t gmf_loader_setup_default_video_ppa(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);

    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_element_handle_t ppa = NULL;
    ret = esp_gmf_video_ppa_init(NULL, &ppa);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init video ppa");
    ret = esp_gmf_pool_register_element(pool, ppa, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_element_deinit(ppa); return ret;}, "Failed to register video ppa");
    return ret;
}
#endif  /* defined(CONFIG_GMF_VIDEO_EFFECTS_INIT_PPA) */

#if defined(CONFIG_GMF_VIDEO_EFFECTS_INIT_FPS_CONVERT)
static esp_gmf_err_t gmf_loader_setup_default_video_fps_cvt(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);

    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_element_handle_t cvt = NULL;
    ret = esp_gmf_video_fps_cvt_init(NULL, &cvt);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init video fps cvt");
    ret = esp_gmf_pool_register_element(pool, cvt, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_element_deinit(cvt); return ret;}, "Failed to register video fps cvt");
    return ret;
}
#endif  /* defined(CONFIG_GMF_VIDEO_EFFECTS_INIT_FPS_CONVERT) */

#if defined(CONFIG_GMF_VIDEO_EFFECTS_INIT_OVERLAY)
static esp_gmf_err_t gmf_loader_setup_default_video_overlay(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);

    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_element_handle_t overlay = NULL;
    ret = esp_gmf_video_overlay_init(NULL, &overlay);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init video overlay");
    ret = esp_gmf_pool_register_element(pool, overlay, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_element_deinit(overlay); return ret;}, "Failed to register video overlay");
    return ret;
}
#endif  /* defined(CONFIG_GMF_VIDEO_EFFECTS_INIT_OVERLAY) */

esp_gmf_err_t gmf_loader_setup_video_effects_default(esp_gmf_pool_handle_t pool)
{
    ESP_LOGD(TAG, "Setting up video effects");
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);

    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_element_handle_t element __attribute__((unused)) = NULL;

#if defined(CONFIG_GMF_VIDEO_EFFECTS_INIT_PPA)
    ret = gmf_loader_setup_default_video_ppa(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to setup video ppa");
#endif  /* defined(CONFIG_GMF_VIDEO_EFFECTS_INIT_PPA) */
#if defined(CONFIG_GMF_VIDEO_EFFECTS_INIT_FPS_CONVERT)
    ret = gmf_loader_setup_default_video_fps_cvt(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to setup video fps cvt");
#endif  /* defined(CONFIG_GMF_VIDEO_EFFECTS_INIT_FPS_CONVERT) */
#if defined(CONFIG_GMF_VIDEO_EFFECTS_INIT_OVERLAY)
    ret = gmf_loader_setup_default_video_overlay(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to setup video overlay");
#endif  /* defined(CONFIG_GMF_VIDEO_EFFECTS_INIT_OVERLAY) */

#if CONFIG_GMF_VIDEO_EFFECTS_INIT_COLOR_CONVERT
    ESP_LOGD(TAG, "Setting up color convert effect");
    esp_imgfx_color_convert_cfg_t cc_cfg = {
        .in_res = {
            .width = CONFIG_GMF_VIDEO_EFFECTS_CC_INPUT_WIDTH,
            .height = CONFIG_GMF_VIDEO_EFFECTS_CC_INPUT_HEIGHT,
        },
        .in_pixel_fmt = CONFIG_GMF_VIDEO_EFFECTS_CC_INPUT_PIXEL_FORMAT,
        .out_pixel_fmt = CONFIG_GMF_VIDEO_EFFECTS_CC_OUTPUT_PIXEL_FORMAT,
        .color_space_std = CONFIG_GMF_VIDEO_EFFECTS_CC_COLOR_SPACE,
    };
    ret = esp_gmf_video_color_convert_init(&cc_cfg, &element);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to setup video color convert");
    ret = esp_gmf_pool_register_element(pool, element, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_element_deinit(element); return ret;}, "Failed to register video color convert");
#endif  /* CONFIG_GMF_VIDEO_EFFECTS_INIT_COLOR_CONVERT */

#if CONFIG_GMF_VIDEO_EFFECTS_INIT_ROTATE
    ESP_LOGD(TAG, "Setting up rotate effect");
    esp_imgfx_rotate_cfg_t rotate_cfg = {
        .in_res = {
            .width = CONFIG_GMF_VIDEO_EFFECTS_ROTATE_INPUT_WIDTH,
            .height = CONFIG_GMF_VIDEO_EFFECTS_ROTATE_INPUT_HEIGHT,
        },
        .in_pixel_fmt = CONFIG_GMF_VIDEO_EFFECTS_ROTATE_INPUT_PIXEL_FORMAT,
        .degree = CONFIG_GMF_VIDEO_EFFECTS_ROTATE_DEGREE,
    };
    ret = esp_gmf_video_rotate_init(&rotate_cfg, &element);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to setup video rotate");
    ret = esp_gmf_pool_register_element(pool, element, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_element_deinit(element); return ret;}, "Failed to register video rotate");
#endif  /* CONFIG_GMF_VIDEO_EFFECTS_INIT_ROTATE */

#if CONFIG_GMF_VIDEO_EFFECTS_INIT_SCALE
    ESP_LOGD(TAG, "Setting up scale effect");
    esp_imgfx_scale_cfg_t scale_cfg = {
        .in_res = {
            .width = CONFIG_GMF_VIDEO_EFFECTS_SCALE_INPUT_WIDTH,
            .height = CONFIG_GMF_VIDEO_EFFECTS_SCALE_INPUT_HEIGHT,
        },
        .in_pixel_fmt = CONFIG_GMF_VIDEO_EFFECTS_SCALE_INPUT_PIXEL_FORMAT,
        .scale_res = {
            .width = CONFIG_GMF_VIDEO_EFFECTS_SCALE_OUTPUT_WIDTH,
            .height = CONFIG_GMF_VIDEO_EFFECTS_SCALE_OUTPUT_HEIGHT,
        },
        .filter_type = CONFIG_GMF_VIDEO_EFFECTS_SCALE_FILTER_TYPE,
    };
    ret = esp_gmf_video_scale_init(&scale_cfg, &element);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to setup video scale");
    ret = esp_gmf_pool_register_element(pool, element, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_element_deinit(element); return ret;}, "Failed to register video scale");
#endif  /* CONFIG_GMF_VIDEO_EFFECTS_INIT_SCALE */

#if CONFIG_GMF_VIDEO_EFFECTS_INIT_CROP
    ESP_LOGD(TAG, "Setting up crop effect");
    esp_imgfx_crop_cfg_t crop_cfg = {
        .in_res = {
            .width = CONFIG_GMF_VIDEO_EFFECTS_CROP_INPUT_WIDTH,
            .height = CONFIG_GMF_VIDEO_EFFECTS_CROP_INPUT_HEIGHT,
        },
        .in_pixel_fmt = CONFIG_GMF_VIDEO_EFFECTS_CROP_INPUT_PIXEL_FORMAT,
        .cropped_res = {
            .width = CONFIG_GMF_VIDEO_EFFECTS_CROP_OUTPUT_WIDTH,
            .height = CONFIG_GMF_VIDEO_EFFECTS_CROP_OUTPUT_HEIGHT,
        },
        .x_pos = CONFIG_GMF_VIDEO_EFFECTS_CROP_X_POS,
        .y_pos = CONFIG_GMF_VIDEO_EFFECTS_CROP_Y_POS,
    };
    ret = esp_gmf_video_crop_init(&crop_cfg, &element);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to setup video crop");
    ret = esp_gmf_pool_register_element(pool, element, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_element_deinit(element); return ret;}, "Failed to register video crop");
#endif  /* CONFIG_GMF_VIDEO_EFFECTS_INIT_CROP */

    ESP_LOGD(TAG, "Video effects setup completed");
    return ret;
}

esp_gmf_err_t gmf_loader_teardown_video_effects_default(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);
    return ESP_GMF_ERR_OK;
}

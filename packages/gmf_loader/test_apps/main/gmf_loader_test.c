/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "esp_gmf_err.h"
#include "esp_gmf_pool.h"
#include "gmf_loader_setup_defaults.h"
#include "esp_gmf_video_color_convert.h"
#include "esp_gmf_video_rotate.h"
#include "esp_gmf_video_scale.h"
#include "esp_gmf_video_crop.h"

typedef esp_gmf_err_t (*gmf_loader_func_t)(esp_gmf_pool_handle_t);

typedef struct {
    gmf_loader_func_t setup;
    gmf_loader_func_t teardown;
} gmf_loader_func_pair_t;

static const gmf_loader_func_pair_t gmf_loader_funcs[] = {
    {gmf_loader_setup_io_default, gmf_loader_teardown_io_default},
    {gmf_loader_setup_audio_codec_default, gmf_loader_teardown_audio_codec_default},
    {gmf_loader_setup_audio_effects_default, gmf_loader_teardown_audio_effects_default},
    {gmf_loader_setup_ai_audio_default, gmf_loader_teardown_ai_audio_default},
    {gmf_loader_setup_video_codec_default, gmf_loader_teardown_video_codec_default},
    {gmf_loader_setup_video_effects_default, gmf_loader_teardown_video_effects_default},
};

TEST_CASE("GMF Loader one pool Test", "[GMF_LOADER]")
{
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;

    ret = esp_gmf_pool_init(&pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);

    ret = gmf_loader_setup_all_defaults(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);

    ret = gmf_loader_teardown_all_defaults(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);

    ret = esp_gmf_pool_deinit(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);
}

TEST_CASE("GMF Loader multiple pools Test", "[GMF_LOADER]")
{
    esp_gmf_pool_handle_t pools[2] = {NULL, NULL};
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;

    for (int i = 0; i < 2; ++i) {
        ret = esp_gmf_pool_init(&pools[i]);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);
    }

    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < sizeof(gmf_loader_funcs) / sizeof(gmf_loader_funcs[0]); ++j) {
            if (gmf_loader_funcs[j].setup) {
                ret = gmf_loader_funcs[j].setup(pools[i]);
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);
            }
        }
    }

    for (int i = 0; i < 2; ++i) {
        for (int j = (int)(sizeof(gmf_loader_funcs) / sizeof(gmf_loader_funcs[0])) - 1; j >= 0; --j) {
            if (gmf_loader_funcs[j].teardown) {
                ret = gmf_loader_funcs[j].teardown(pools[i]);
                TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);
            }
        }
    }
    for (int i = 0; i < 2; ++i) {
        ret = esp_gmf_pool_deinit(pools[i]);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);
    }
}

static esp_gmf_element_handle_t get_element_by_tag_from_pool(esp_gmf_pool_handle_t pool, const char *tag)
{
    const void *iter = NULL;
    esp_gmf_element_handle_t element = NULL;
    while (esp_gmf_pool_iterate_element(pool, &iter, &element) == ESP_GMF_ERR_OK) {
        if (strcmp(OBJ_GET_TAG(element), tag) == 0) {
            return element;
        }
    }
    return NULL;
}

TEST_CASE("GMF video effects", "[GMF_LOADER]")
{
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_element_handle_t element = NULL;

    ret = esp_gmf_pool_init(&pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);

    ret = gmf_loader_setup_video_effects_default(pool);

    // Test color convert effect
#ifdef CONFIG_GMF_VIDEO_EFFECTS_CC
    element = get_element_by_tag_from_pool(pool, "vid_color_cvt");
    TEST_ASSERT_NOT_NULL(element);

    esp_imgfx_color_convert_cfg_t *cc_cfg = (esp_imgfx_color_convert_cfg_t *)OBJ_GET_CFG(element);
    TEST_ASSERT_NOT_NULL(cc_cfg);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_CC_INPUT_PIXEL_FORMAT, cc_cfg->in_pixel_fmt);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_CC_OUTPUT_PIXEL_FORMAT, cc_cfg->out_pixel_fmt);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_CC_COLOR_SPACE, cc_cfg->color_space);
#endif  /* CONFIG_GMF_VIDEO_EFFECTS_CC */

    // Test scale effect
#ifdef CONFIG_GMF_VIDEO_EFFECTS_SCALE
    element = get_element_by_tag_from_pool(pool, "vid_scale");
    TEST_ASSERT_NOT_NULL(element);

    esp_imgfx_scale_cfg_t *scale_cfg = (esp_imgfx_scale_cfg_t *)OBJ_GET_CFG(element);
    TEST_ASSERT_NOT_NULL(scale_cfg);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_SCALE_INPUT_WIDTH, scale_cfg->in_res.width);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_SCALE_INPUT_HEIGHT, scale_cfg->in_res.height);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_SCALE_INPUT_PIXEL_FORMAT, scale_cfg->in_pixel_fmt);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_SCALE_OUTPUT_WIDTH, scale_cfg->scale_res.width);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_SCALE_OUTPUT_HEIGHT, scale_cfg->scale_res.height);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_SCALE_FILTER_TYPE, scale_cfg->filter_type);
#endif  /* CONFIG_GMF_VIDEO_EFFECTS_SCALE */

    // Test rotate effect
#ifdef CONFIG_GMF_VIDEO_EFFECTS_ROTATE
    element = get_element_by_tag_from_pool(pool, "vid_rotate");
    TEST_ASSERT_NOT_NULL(element);

    esp_imgfx_rotate_cfg_t *rotate_cfg = (esp_imgfx_rotate_cfg_t *)OBJ_GET_CFG(element);
    TEST_ASSERT_NOT_NULL(rotate_cfg);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_ROTATE_INPUT_PIXEL_FORMAT, rotate_cfg->in_pixel_fmt);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_ROTATE_INPUT_WIDTH, rotate_cfg->in_res.width);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_ROTATE_INPUT_HEIGHT, rotate_cfg->in_res.height);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_ROTATE_DEGREE, rotate_cfg->degree);
#endif  /* CONFIG_GMF_VIDEO_EFFECTS_ROTATE */

    // Test crop effect
#ifdef CONFIG_GMF_VIDEO_EFFECTS_CROP
    element = get_element_by_tag_from_pool(pool, "vid_crop");
    TEST_ASSERT_NOT_NULL(element);

    esp_imgfx_crop_cfg_t *crop_cfg = (esp_imgfx_crop_cfg_t *)OBJ_GET_CFG(element);
    TEST_ASSERT_NOT_NULL(crop_cfg);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_CROP_INPUT_PIXEL_FORMAT, crop_cfg->in_pixel_fmt);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_CROP_INPUT_WIDTH, crop_cfg->in_res.width);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_CROP_INPUT_HEIGHT, crop_cfg->in_res.height);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_CROP_X, crop_cfg->crop_x);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_CROP_Y, crop_cfg->crop_y);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_CROP_WIDTH, crop_cfg->crop_res.width);
    TEST_ASSERT_EQUAL(CONFIG_GMF_VIDEO_EFFECTS_CROP_HEIGHT, crop_cfg->crop_res.height);
#endif  /* CONFIG_GMF_VIDEO_EFFECTS_CROP */

    ret = gmf_loader_teardown_video_effects_default(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);

    ret = esp_gmf_pool_deinit(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ret);
}

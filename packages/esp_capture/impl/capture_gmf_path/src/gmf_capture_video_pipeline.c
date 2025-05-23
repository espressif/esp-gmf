/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "capture_pipeline_builder.h"
#include "capture_share_copy_el.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_video_enc.h"
#include "esp_gmf_video_overlay.h"
#include "esp_gmf_video_ppa.h"
#include "esp_gmf_video_fps_cvt.h"
#include "capture_video_src_el.h"
#include "esp_gmf_video_scale.h"
#include "esp_gmf_video_crop.h"
#include "esp_gmf_video_color_convert.h"
#include "esp_gmf_pool.h"
#include "capture_pipeline_utils.h"
#include "capture_pipeline_nego.h"
#include "capture_utils.h"
#include "esp_log.h"

#define TAG "GMF_VID_PIPE"

typedef struct video_pipeline_t video_pipeline_t;
typedef struct {
    uint8_t           path;
    video_pipeline_t *parent;
} video_path_ctx_t;

struct video_pipeline_t {
    esp_capture_pipeline_builder_if_t base;
    esp_gmf_pool_handle_t             pool;
    uint8_t                           sink_num;
    esp_gmf_pipeline_handle_t         src_pipeline;
    esp_gmf_pipeline_handle_t         enc_pipeline[2];
    video_path_ctx_t                  path_ctx[2];
    esp_capture_stream_info_t         sink_cfg[2];
};

static int buildup_pipelines(video_pipeline_t *video_pipe)
{
    // Buildup pipelines
    const char *src_elements[] = {"vid_src", "share_copier"};
    int ret = esp_gmf_pool_new_pipeline(video_pipe->pool, NULL, src_elements, CAPTURE_ELEMS(src_elements), NULL, &video_pipe->src_pipeline);
    CAPTURE_RETURN_ON_FAIL(ret);
    for (int i = 0; i < CAPTURE_ELEMS(video_pipe->enc_pipeline); i++) {
#if CONFIG_IDF_TARGET_ESP32P4
        const char *process_elements[] = {"vid_fps_cvt", "vid_overlay", "vid_ppa", "vid_enc"};
#else
        const char *process_elements[] = {"vid_fps_cvt", "vid_scale", "vid_overlay", "vid_color_cvt", "vid_enc"};
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
        ret = esp_gmf_pool_new_pipeline(video_pipe->pool, NULL,
                                        process_elements, CAPTURE_ELEMS(process_elements), NULL, &video_pipe->enc_pipeline[i]);
        CAPTURE_RETURN_ON_FAIL(ret);
    }
    return ret;
}

static esp_capture_err_t gmf_video_pipeline_create(esp_capture_pipeline_builder_if_t *pipeline)
{
    video_pipeline_t *video_pipe = (video_pipeline_t *)pipeline;
    esp_gmf_obj_handle_t el = NULL;
    do {
        esp_gmf_pool_init(&video_pipe->pool);
        if (video_pipe->pool == NULL) {
            break;
        }
        // TODO add other elements
        esp_gmf_video_fps_cvt_init(NULL, &el);
        CAPTURE_BREAK_ON_ERR(esp_gmf_pool_register_element(video_pipe->pool, el, NULL));
        // Default add source
        capture_video_src_el_init(NULL, &el);
        CAPTURE_BREAK_ON_ERR(esp_gmf_pool_register_element(video_pipe->pool, el, NULL));

        capture_share_copy_el_cfg_t copy_cfg = {};
        capture_share_copy_el_init(&copy_cfg, &el);
        CAPTURE_BREAK_ON_ERR(esp_gmf_pool_register_element(video_pipe->pool, el, NULL));

        esp_gmf_video_enc_init(NULL, &el);
        CAPTURE_BREAK_ON_ERR(esp_gmf_pool_register_element(video_pipe->pool, el, NULL));

        esp_gmf_video_overlay_init(NULL, &el);
        CAPTURE_BREAK_ON_ERR(esp_gmf_pool_register_element(video_pipe->pool, el, NULL));
        // Only add ppa for P4
#if CONFIG_IDF_TARGET_ESP32P4
        esp_gmf_video_ppa_init(NULL, &el);
        CAPTURE_BREAK_ON_ERR(esp_gmf_pool_register_element(video_pipe->pool, el, NULL));
#else
        esp_imgfx_scale_cfg_t scale_cfg = {
            .filter_type = ESP_IMGFX_SCALE_FILTER_TYPE_BILINEAR};
        esp_gmf_video_scale_init(&scale_cfg, &el);
        CAPTURE_BREAK_ON_ERR(esp_gmf_pool_register_element(video_pipe->pool, el, NULL));

        esp_imgfx_crop_cfg_t crop_cfg = {};
        esp_gmf_video_crop_init(&crop_cfg, &el);
        CAPTURE_BREAK_ON_ERR(esp_gmf_pool_register_element(video_pipe->pool, el, NULL));

        esp_imgfx_color_convert_cfg_t color_convert_cfg = {
            .color_space_std = ESP_IMGFX_COLOR_SPACE_STD_BT601};
        esp_gmf_video_color_convert_init(&color_convert_cfg, &el);
        CAPTURE_BREAK_ON_ERR(esp_gmf_pool_register_element(video_pipe->pool, el, NULL));
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
        CAPTURE_BREAK_ON_ERR(buildup_pipelines(video_pipe));
        return ESP_CAPTURE_ERR_OK;
    } while (0);
    return ESP_CAPTURE_ERR_NO_MEM;
}

static esp_capture_err_t gmf_video_pipeline_get(esp_capture_pipeline_builder_if_t *builder, esp_capture_gmf_pipeline_t *pipe, uint8_t *pipeline_num)
{
    video_pipeline_t *video_pipe = (video_pipeline_t *)builder;
    if (pipe == NULL) {
        *pipeline_num = video_pipe->sink_num + 1;
        return 0;
    }
    if (*pipeline_num < video_pipe->sink_num + 1) {
        return -1;
    }
    pipe[0].pipeline = video_pipe->src_pipeline;
    pipe[0].path_mask = 0x1;
    pipe[0].name = "vid_src";
    pipe[1].pipeline = video_pipe->enc_pipeline[0];
    pipe[1].path_mask = 0x1;
    pipe[1].name = "venc_0";
    if (video_pipe->sink_num > 1) {
        pipe[0].path_mask |= 0x2;
        pipe[2].pipeline = video_pipe->enc_pipeline[1];
        pipe[2].path_mask = 0x2;
        pipe[2].name = "venc_1";
    }
    *pipeline_num = video_pipe->sink_num + 1;
    return 0;
}

static esp_capture_err_t gmf_video_pipeline_get_element(esp_capture_pipeline_builder_if_t *builder, uint8_t path_idx, const char *tag, esp_gmf_element_handle_t *element)
{
    video_pipeline_t *video_pipe = (video_pipeline_t *)builder;
    if (path_idx > video_pipe->sink_num || video_pipe->enc_pipeline[path_idx] == NULL) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    int ret = esp_gmf_pipeline_get_el_by_name(video_pipe->enc_pipeline[path_idx], tag, element);
    return (ret == ESP_GMF_ERR_OK) ? ESP_CAPTURE_ERR_OK : ESP_CAPTURE_ERR_NOT_FOUND;
}

static esp_capture_err_t gmf_video_pipeline_set_cfg(esp_capture_pipeline_builder_if_t *builder, uint8_t path_idx, esp_capture_stream_info_t *sink_cfg)
{
    video_pipeline_t *video_pipe = (video_pipeline_t *)builder;
    if (video_pipe == NULL || path_idx >= video_pipe->sink_num || sink_cfg == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    video_pipe->sink_cfg[path_idx] = *sink_cfg;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t gmf_video_pipeline_get_cfg(esp_capture_pipeline_builder_if_t *builder, uint8_t path_idx,
                                                    esp_capture_stream_info_t *sink_cfg)
{
    video_pipeline_t *video_pipe = (video_pipeline_t *)builder;
    if (video_pipe == NULL || path_idx >= video_pipe->sink_num || sink_cfg == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    *sink_cfg = video_pipe->sink_cfg[path_idx];
    return ESP_CAPTURE_ERR_OK;
}

static esp_gmf_err_t video_pipe_prev_run(void *handle)
{
    video_path_ctx_t *ctx = (video_path_ctx_t *)handle;
    if (ctx == NULL || ctx->parent == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    // Disable outport firstly
    video_pipeline_t *video_pipe = ctx->parent;
    esp_gmf_element_handle_t cp_element = NULL;
    esp_gmf_pipeline_get_el_by_name(video_pipe->src_pipeline, "share_copier", &cp_element);
    capture_share_copy_el_enable(cp_element, ctx->path, true);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t video_pipe_prev_stop(void *handle)
{
    video_path_ctx_t *ctx = (video_path_ctx_t *)handle;
    if (ctx == NULL || ctx->parent == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    // Disable outport firstly
    video_pipeline_t *video_pipe = ctx->parent;
    esp_gmf_element_handle_t cp_element = NULL;
    esp_gmf_pipeline_get_el_by_name(video_pipe->src_pipeline, "share_copier", &cp_element);
    ESP_LOGD(TAG, "Begin to disable share copy for video");
    capture_share_copy_el_enable(cp_element, ctx->path, false);
    ESP_LOGD(TAG, "End to disable share copy for video");
    return ESP_GMF_ERR_OK;
}

static esp_capture_err_t video_pipeline_cfg(video_pipeline_t *video_pipe, esp_capture_gmf_video_pipeline_cfg_t *cfg)
{
    esp_gmf_element_handle_t cp_element = NULL;
    esp_gmf_pipeline_get_el_by_name(video_pipe->src_pipeline, "share_copier", &cp_element);

    esp_gmf_element_handle_t src_element = NULL;
    esp_gmf_pipeline_get_el_by_name(video_pipe->src_pipeline, "vid_src", &src_element);

    capture_video_src_el_set_src_if(src_element, cfg->vid_src[0]);

    // Connect pipelines
    for (int i = 0; i < video_pipe->sink_num; i++) {
        // Set pre stop callback to avoid read or write blocked when stop
        esp_gmf_element_handle_t element = NULL;
        video_pipe->path_ctx[i].path = i;
        video_pipe->path_ctx[i].parent = video_pipe;
        esp_gmf_pipeline_set_prev_run_cb(video_pipe->enc_pipeline[i], video_pipe_prev_run, &video_pipe->path_ctx[i]);
        esp_gmf_pipeline_set_prev_stop_cb(video_pipe->enc_pipeline[i], video_pipe_prev_stop, &video_pipe->path_ctx[i]);
        // Should manually link pipeline from copier for it have multiple outports
        esp_gmf_port_handle_t port = capture_share_copy_el_new_out_port(cp_element, i);
        esp_gmf_pipeline_get_head_el(video_pipe->enc_pipeline[i], &element);
        esp_gmf_pipeline_connect_pipe(video_pipe->src_pipeline, "share_copier", NULL,
                                      video_pipe->enc_pipeline[i], OBJ_GET_TAG(element), port);
        if (i >= cfg->vid_sink_num) {
            ESP_LOGE(TAG, "Sink %d not set", i);
        }
    }
    return ESP_CAPTURE_ERR_OK;
}

static void gmf_video_pipeline_destroy(esp_capture_pipeline_builder_if_t *pipeline)
{
    video_pipeline_t *video_pipe = (video_pipeline_t *)pipeline;
    for (int i = 0; i < video_pipe->sink_num; i++) {
        if (video_pipe->enc_pipeline[i]) {
            esp_gmf_pipeline_destroy(video_pipe->enc_pipeline[i]);
            video_pipe->enc_pipeline[i] = NULL;
        }
    }
    if (video_pipe->src_pipeline) {
        esp_gmf_pipeline_destroy(video_pipe->src_pipeline);
        video_pipe->src_pipeline = NULL;
    }
    if (video_pipe->pool) {
        esp_gmf_pool_deinit(video_pipe->pool);
        video_pipe->pool = NULL;
    }
    capture_free(video_pipe);
}

esp_capture_pipeline_builder_if_t *esp_capture_create_video_pipeline(esp_capture_gmf_video_pipeline_cfg_t *cfg)
{
    video_pipeline_t *video_pipe = (video_pipeline_t *)calloc(1, sizeof(video_pipeline_t));
    if (video_pipe == NULL) {
        return NULL;
    }
    video_pipe->base.create = gmf_video_pipeline_create;
    video_pipe->base.get_pipelines = gmf_video_pipeline_get;
    video_pipe->base.get_element = gmf_video_pipeline_get_element;
    video_pipe->base.set_sink_cfg = gmf_video_pipeline_set_cfg;
    video_pipe->base.get_sink_cfg = gmf_video_pipeline_get_cfg;
    video_pipe->base.negotiate = esp_capture_video_pipeline_auto_negotiate;
    video_pipe->base.destroy = gmf_video_pipeline_destroy;

    video_pipe->sink_num = cfg->vid_sink_num;
    if (video_pipe->sink_num > CAPTURE_ELEMS(video_pipe->enc_pipeline)) {
        video_pipe->sink_num = CAPTURE_ELEMS(video_pipe->enc_pipeline);
    }
    int ret = video_pipe->base.create(&video_pipe->base);
    if (ret == ESP_CAPTURE_ERR_OK) {
        ret = video_pipeline_cfg(video_pipe, cfg);
    }
    if (ret != 0) {
        video_pipe->base.destroy(&video_pipe->base);
        return NULL;
    }
    return &video_pipe->base;
}

/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "capture_pipeline_builder.h"
#include "esp_gmf_audio_enc.h"
#include "capture_share_copy_el.h"
#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_audio_element.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_sonic.h"
#include "esp_gmf_alc.h"
#include "esp_gmf_eq.h"
#include "esp_gmf_fade.h"
#include "esp_gmf_pool.h"
#include "capture_audio_src_el.h"
#include "esp_log.h"
#include "capture_pipeline_nego.h"
#include "capture_os.h"
#include "capture_utils.h"

#define TAG "GMF_AUD_PIPE"

typedef struct audio_pipeline_t audio_pipeline_t;

typedef struct {
    uint8_t           path;
    audio_pipeline_t *parent;
} audio_path_ctx_t;

struct audio_pipeline_t {
    esp_capture_pipeline_builder_if_t base;
    esp_gmf_pool_handle_t             pool;
    esp_gmf_pipeline_handle_t         src_pipeline;
    uint8_t                           sink_num;
    esp_gmf_pipeline_handle_t         enc_pipeline[2];
    esp_capture_stream_info_t         sink_cfg[2];
    audio_path_ctx_t                  path_ctx[2];
};

static esp_gmf_err_t buildup_pipelines(audio_pipeline_t *audio_pipe)
{
    const char *copy_elements[] = {"aud_src", "share_copier"};
    esp_gmf_err_t ret = esp_gmf_pool_new_pipeline(audio_pipe->pool, NULL, copy_elements, CAPTURE_ELEMS(copy_elements), NULL, &audio_pipe->src_pipeline);
    CAPTURE_RETURN_ON_FAIL(ret);
    for (int i = 0; i < audio_pipe->sink_num; i++) {
        const char *process_elements[] = {"aud_ch_cvt", "aud_rate_cvt", "aud_bit_cvt", "aud_enc"};
        ret = esp_gmf_pool_new_pipeline(audio_pipe->pool, NULL,
                                        process_elements, CAPTURE_ELEMS(process_elements), NULL, &audio_pipe->enc_pipeline[i]);
        CAPTURE_RETURN_ON_FAIL(ret);
    }
    return ret;
}

static esp_capture_err_t gmf_audio_pipeline_create(esp_capture_pipeline_builder_if_t *pipeline)
{
    audio_pipeline_t *audio_pipe = (audio_pipeline_t *)pipeline;
    esp_gmf_obj_handle_t el = NULL;
    do {
        esp_gmf_pool_init(&audio_pipe->pool);
        if (audio_pipe->pool == NULL) {
            break;
        }
        esp_audio_enc_config_t enc_cfg = DEFAULT_ESP_GMF_AUDIO_ENC_CONFIG();
        esp_gmf_audio_enc_init(&enc_cfg, &el);
        CAPTURE_BREAK_ON_ERR(esp_gmf_pool_register_element(audio_pipe->pool, el, NULL));

        capture_audio_src_el_init(NULL, &el);
        CAPTURE_BREAK_ON_ERR(esp_gmf_pool_register_element(audio_pipe->pool, el, NULL));

        capture_share_copy_el_cfg_t copy_cfg = {};
        capture_share_copy_el_init(&copy_cfg, &el);
        CAPTURE_BREAK_ON_ERR(esp_gmf_pool_register_element(audio_pipe->pool, el, NULL));

        esp_ae_ch_cvt_cfg_t ch_cvt_cfg = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
        esp_gmf_ch_cvt_init(&ch_cvt_cfg, &el);
        CAPTURE_BREAK_ON_ERR(esp_gmf_pool_register_element(audio_pipe->pool, el, NULL));

        esp_ae_bit_cvt_cfg_t bit_cvt_cfg = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
        esp_gmf_bit_cvt_init(&bit_cvt_cfg, &el);
        CAPTURE_BREAK_ON_ERR(esp_gmf_pool_register_element(audio_pipe->pool, el, NULL));

        esp_ae_rate_cvt_cfg_t rate_cvt_cfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
        esp_gmf_rate_cvt_init(&rate_cvt_cfg, &el);
        CAPTURE_BREAK_ON_ERR(esp_gmf_pool_register_element(audio_pipe->pool, el, NULL));

        CAPTURE_BREAK_ON_ERR(buildup_pipelines(audio_pipe));
        return ESP_CAPTURE_ERR_OK;
    } while (0);
    return ESP_CAPTURE_ERR_NO_MEM;
}

static esp_capture_err_t gmf_audio_pipeline_get(esp_capture_pipeline_builder_if_t *builder, esp_capture_gmf_pipeline_t *pipe, uint8_t *pipeline_num)
{
    audio_pipeline_t *audio_pipe = (audio_pipeline_t *)builder;
    if (pipe == NULL) {
        *pipeline_num = audio_pipe->sink_num + 1;
        return ESP_CAPTURE_ERR_OK;
    }
    if (*pipeline_num < audio_pipe->sink_num + 1) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    pipe[0].pipeline = audio_pipe->src_pipeline;
    pipe[0].path_mask = 0x1;
    pipe[0].name = "aud_src";
    pipe[1].pipeline = audio_pipe->enc_pipeline[0];
    pipe[1].path_mask = 0x1;
    pipe[1].name = "aenc_0";
    if (audio_pipe->sink_num > 1) {
        pipe[0].path_mask |= 0x2;
        pipe[2].pipeline = audio_pipe->enc_pipeline[1];
        pipe[2].path_mask = 0x2;
        pipe[2].name = "aenc_1";
    }
    *pipeline_num = audio_pipe->sink_num + 1;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t gmf_audio_pipeline_get_element(esp_capture_pipeline_builder_if_t *builder, uint8_t path_idx, const char *tag, esp_gmf_element_handle_t *element)
{
    audio_pipeline_t *audio_pipe = (audio_pipeline_t *)builder;
    if (path_idx >= audio_pipe->sink_num || audio_pipe->enc_pipeline[path_idx] == NULL) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    esp_gmf_err_t ret = esp_gmf_pipeline_get_el_by_name(audio_pipe->enc_pipeline[path_idx], tag, element);
    return (ret == ESP_GMF_ERR_OK) ? ESP_CAPTURE_ERR_OK : ESP_CAPTURE_ERR_NOT_FOUND;
}

static void gmf_audio_pipeline_destroy(esp_capture_pipeline_builder_if_t *pipeline)
{
    audio_pipeline_t *audio_pipe = (audio_pipeline_t *)pipeline;
    for (int i = 0; i < audio_pipe->sink_num; i++) {
        if (audio_pipe->enc_pipeline[i]) {
            esp_gmf_pipeline_destroy(audio_pipe->enc_pipeline[i]);
            audio_pipe->enc_pipeline[i] = NULL;
        }
    }
    if (audio_pipe->src_pipeline) {
        esp_gmf_pipeline_destroy(audio_pipe->src_pipeline);
        audio_pipe->src_pipeline = NULL;
    }
    if (audio_pipe->pool) {
        esp_gmf_pool_deinit(audio_pipe->pool);
        audio_pipe->pool = NULL;
    }
    capture_free(audio_pipe);
}

static esp_gmf_err_t audio_pipe_prev_stop(void *handle)
{
    audio_path_ctx_t *ctx = (audio_path_ctx_t *)handle;
    if (ctx == NULL || ctx->parent == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    // Disable outport firstly
    audio_pipeline_t *audio_pipe = ctx->parent;
    esp_gmf_element_handle_t cp_element = NULL;
    esp_gmf_pipeline_get_el_by_name(audio_pipe->src_pipeline, "share_copier", &cp_element);
    capture_share_copy_el_enable(cp_element, ctx->path, false);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t audio_pipe_prev_start(void *handle)
{
    audio_path_ctx_t *ctx = (audio_path_ctx_t *)handle;
    if (ctx == NULL || ctx->parent == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    // Disable outport firstly
    audio_pipeline_t *audio_pipe = ctx->parent;
    esp_gmf_element_handle_t cp_element = NULL;
    esp_gmf_pipeline_get_el_by_name(audio_pipe->src_pipeline, "share_copier", &cp_element);
    capture_share_copy_el_enable(cp_element, ctx->path, true);
    return ESP_GMF_ERR_OK;
}

static esp_capture_err_t gmf_audio_pipeline_cfg(audio_pipeline_t *audio_pipe, esp_capture_gmf_audio_pipeline_cfg_t *cfg)
{
    esp_gmf_element_handle_t cp_element = NULL;
    esp_gmf_pipeline_get_el_by_name(audio_pipe->src_pipeline, "share_copier", &cp_element);

    esp_gmf_element_handle_t src_element = NULL;
    esp_gmf_pipeline_get_el_by_name(audio_pipe->src_pipeline, "aud_src", &src_element);

    capture_audio_src_el_set_src_if(src_element, cfg->aud_src[0]);

    // Connect pipelines
    for (int i = 0; i < audio_pipe->sink_num; i++) {
        // Set pre stop callback to avoid read or write blocked when stop
        esp_gmf_element_handle_t element = NULL;
        audio_pipe->path_ctx[i].path = i;
        audio_pipe->path_ctx[i].parent = audio_pipe;
        esp_gmf_pipeline_set_prev_run_cb(audio_pipe->enc_pipeline[i], audio_pipe_prev_start, &audio_pipe->path_ctx[i]);
        esp_gmf_pipeline_set_prev_stop_cb(audio_pipe->enc_pipeline[i], audio_pipe_prev_stop, &audio_pipe->path_ctx[i]);
        // Should manually link pipeline from copier for it have multiple outports
        esp_gmf_port_handle_t port = capture_share_copy_el_new_out_port(cp_element, i);
        esp_gmf_pipeline_get_head_el(audio_pipe->enc_pipeline[i], &element);
        esp_gmf_pipeline_connect_pipe(audio_pipe->src_pipeline, "share_copier", NULL,
                                      audio_pipe->enc_pipeline[i], OBJ_GET_TAG(element), port);
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t gmf_audio_pipeline_set_cfg(esp_capture_pipeline_builder_if_t *builder, uint8_t path_idx, esp_capture_stream_info_t *sink_cfg)
{
    audio_pipeline_t *audio_pipe = (audio_pipeline_t *)builder;
    if (audio_pipe == NULL || path_idx >= audio_pipe->sink_num || sink_cfg == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    audio_pipe->sink_cfg[path_idx] = *sink_cfg;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t gmf_audio_pipeline_get_cfg(esp_capture_pipeline_builder_if_t *builder, uint8_t path_idx,
                                                    esp_capture_stream_info_t *sink_cfg)
{
    audio_pipeline_t *audio_pipe = (audio_pipeline_t *)builder;
    if (audio_pipe == NULL || path_idx >= audio_pipe->sink_num || sink_cfg == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    *sink_cfg = audio_pipe->sink_cfg[path_idx];
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_pipeline_builder_if_t *esp_capture_create_audio_pipeline(esp_capture_gmf_audio_pipeline_cfg_t *cfg)
{
    if (cfg == NULL || cfg->aud_src_num == 0 || cfg->aud_sink_num == 0) {
        return NULL;
    }
    audio_pipeline_t *audio_pipe = (audio_pipeline_t *)calloc(1, sizeof(audio_pipeline_t));
    if (audio_pipe == NULL) {
        return NULL;
    }
    audio_pipe->base.create = gmf_audio_pipeline_create;
    audio_pipe->base.get_pipelines = gmf_audio_pipeline_get;
    audio_pipe->base.get_element = gmf_audio_pipeline_get_element;
    audio_pipe->base.set_sink_cfg = gmf_audio_pipeline_set_cfg;
    audio_pipe->base.get_sink_cfg = gmf_audio_pipeline_get_cfg;
    // Auto negotiate for all paths
    audio_pipe->base.negotiate = esp_capture_audio_pipeline_auto_negotiate;

    audio_pipe->base.destroy = gmf_audio_pipeline_destroy;
    audio_pipe->sink_num = cfg->aud_sink_num;
    if (audio_pipe->sink_num > CAPTURE_ELEMS(audio_pipe->enc_pipeline)) {
        audio_pipe->sink_num = CAPTURE_ELEMS(audio_pipe->enc_pipeline);
    }
    esp_capture_err_t ret = audio_pipe->base.create(&audio_pipe->base);
    if (ret == ESP_CAPTURE_ERR_OK) {
        ret = gmf_audio_pipeline_cfg(audio_pipe, cfg);
    }
    if (ret != ESP_CAPTURE_ERR_OK) {
        audio_pipe->base.destroy(&audio_pipe->base);
        return NULL;
    }
    return &audio_pipe->base;
}

/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>

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
#include "esp_gmf_caps_def.h"
#include "capture_pipeline_builder.h"
#include "capture_audio_src_el.h"
#include "capture_pipeline_nego.h"
#include "capture_os.h"
#include "capture_utils.h"
#include "esp_log.h"

#define TAG "GMF_AUD_PIPE"

#define MAX_SINK_NUM (2)

typedef struct audio_pipeline_t audio_pipeline_t;

typedef enum {
    AUDIO_PATH_OPS_NONE     = 0,
    AUDIO_PATH_OPS_CH_CVT   = 1,
    AUDIO_PATH_OPS_BIT_CVT  = 2,
    AUDIO_PATH_OPS_RATE_CVT = 3,
    AUDIO_PATH_OPS_ENC      = 4,
    AUDIO_PATH_OPS_MAX      = 5,
} audio_path_ops_t;

typedef struct {
    uint8_t            path;
    audio_pipeline_t  *parent;
} audio_path_ctx_t;

struct audio_pipeline_t {
    esp_capture_pipeline_builder_if_t          base;
    esp_capture_gmf_auto_audio_pipeline_cfg_t  cfg;
    esp_gmf_pool_handle_t                      pool;
    bool                                       pipeline_created;
    esp_gmf_pipeline_handle_t                  src_pipeline;
    uint8_t                                    sink_num;
    esp_gmf_pipeline_handle_t                  enc_pipeline[MAX_SINK_NUM];
    bool                                       build_by_user[MAX_SINK_NUM];
    esp_capture_stream_info_t                  sink_cfg[MAX_SINK_NUM];
    audio_path_ctx_t                           path_ctx[MAX_SINK_NUM];
    const char                                *ops_tags[AUDIO_PATH_OPS_MAX];
};

static uint8_t get_sink_num(audio_pipeline_t *audio_pipe)
{
    uint8_t sink_num = 0;
    for (int i = 0; i < MAX_SINK_NUM; i++) {
        if (audio_pipe->sink_cfg[i].audio_info.format_id) {
            sink_num++;
        }
    }
    return sink_num;
}

static void get_max_sink_cfg(audio_pipeline_t *audio_pipe, esp_capture_audio_info_t *max_sink_info)
{
    int8_t sink_num = 0;
    for (int i = 0; i < MAX_SINK_NUM; i++) {
        if (audio_pipe->sink_cfg[i].audio_info.format_id) {
            if (sink_num == 0) {
                *max_sink_info = audio_pipe->sink_cfg[i].audio_info;
            } else {
                MAX_AUD_SINK_CFG((*max_sink_info), audio_pipe->sink_cfg[i]);
            }
            sink_num++;
        }
    }
}

static void sort_path_ops(audio_path_ops_t *ops, esp_capture_audio_info_t *src, esp_capture_audio_info_t *dst)
{
    int i = 0;
    if (src->channel > dst->channel) {
        ops[i++] = AUDIO_PATH_OPS_CH_CVT;
    }
    if (src->bits_per_sample > dst->bits_per_sample) {
        ops[i++] = AUDIO_PATH_OPS_BIT_CVT;
    }
    if (src->sample_rate != dst->sample_rate) {
        ops[i++] = AUDIO_PATH_OPS_RATE_CVT;
    }
    if (src->bits_per_sample < dst->bits_per_sample) {
        ops[i++] = AUDIO_PATH_OPS_BIT_CVT;
    }
    if (src->channel < dst->channel) {
        ops[i++] = AUDIO_PATH_OPS_CH_CVT;
    }
}

static void get_element_tag_by_caps(audio_pipeline_t *audio_pipe)
{
    const void *iter = NULL;
    esp_gmf_element_handle_t element = NULL;
    while (esp_gmf_pool_iterate_element(audio_pipe->pool, &iter, &element) == ESP_GMF_ERR_OK) {
        const esp_gmf_cap_t *caps = NULL;
        esp_gmf_element_get_caps(element, &caps);
        while (caps) {
            audio_path_ops_t ops = AUDIO_PATH_OPS_NONE;
            if (caps->cap_eightcc == ESP_GMF_CAPS_AUDIO_BIT_CONVERT) {
                ops = AUDIO_PATH_OPS_BIT_CVT;
            } else if (caps->cap_eightcc == ESP_GMF_CAPS_AUDIO_CHANNEL_CONVERT) {
                ops = AUDIO_PATH_OPS_CH_CVT;
            } else if (caps->cap_eightcc == ESP_GMF_CAPS_AUDIO_RATE_CONVERT) {
                ops = AUDIO_PATH_OPS_RATE_CVT;
            } else if (caps->cap_eightcc == ESP_GMF_CAPS_AUDIO_ENCODER) {
                ops = AUDIO_PATH_OPS_ENC;
            }
            if (ops && audio_pipe->ops_tags[ops] == NULL) {
                audio_pipe->ops_tags[ops] = OBJ_GET_TAG(element);
            }
            caps = caps->next;
        }
    }
}

static const char *get_ops_element(audio_pipeline_t *audio_pipe, audio_path_ops_t ops)
{
    return audio_pipe->ops_tags[ops];
}

static bool is_dup_element(const char **elements, int n, const char *cur)
{
    for (int i = 0; i < n; i++) {
        if (cur == elements[i]) {
            return true;
        }
    }
    return false;
}

static esp_gmf_err_t audio_pipe_prev_stop(void *handle)
{
    audio_path_ctx_t *ctx = (audio_path_ctx_t *)handle;
    if (ctx == NULL || ctx->parent == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    // Disable outport firstly
    audio_pipeline_t *audio_pipe = ctx->parent;
    esp_gmf_element_handle_t cp_element = NULL;
    esp_gmf_pipeline_get_el_by_name(audio_pipe->src_pipeline, "share_copier", &cp_element);
    capture_share_copy_el_enable(cp_element, ctx->path, false);
    return ESP_CAPTURE_ERR_OK;
}

static esp_gmf_err_t audio_pipe_prev_start(void *handle)
{
    audio_path_ctx_t *ctx = (audio_path_ctx_t *)handle;
    if (ctx == NULL || ctx->parent == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    // Disable outport firstly
    audio_pipeline_t *audio_pipe = ctx->parent;
    esp_gmf_element_handle_t cp_element = NULL;
    esp_gmf_pipeline_get_el_by_name(audio_pipe->src_pipeline, "share_copier", &cp_element);
    capture_share_copy_el_enable(cp_element, ctx->path, true);
    return ESP_CAPTURE_ERR_OK;
}

static esp_gmf_element_handle_t get_src_element(audio_pipeline_t *audio_pipe)
{
    esp_gmf_element_handle_t src_element = NULL;
    if (audio_pipe->src_pipeline) {
        esp_gmf_pipeline_get_el_by_name(audio_pipe->src_pipeline, "aud_src", &src_element);
    } else if (audio_pipe->sink_num == 1) {
        for (int i = 0; i < MAX_SINK_NUM; i++) {
            if (audio_pipe->enc_pipeline[i]) {
                esp_gmf_pipeline_get_el_by_name(audio_pipe->enc_pipeline[i], "aud_src", &src_element);
                break;
            }
        }
    }
    return src_element;
}

static esp_capture_err_t audio_pipe_link(audio_pipeline_t *audio_pipe)
{
    esp_gmf_element_handle_t src_element = get_src_element(audio_pipe);
    if (src_element) {
        capture_audio_src_el_set_src_if(src_element, audio_pipe->cfg.aud_src);
    }
    if (audio_pipe->src_pipeline == NULL) {
        return ESP_CAPTURE_ERR_OK;
    }
    esp_gmf_element_handle_t cp_element = NULL;
    esp_gmf_pipeline_get_el_by_name(audio_pipe->src_pipeline, "share_copier", &cp_element);

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

static bool have_user_pipe(audio_pipeline_t *audio_pipe)
{
    for (int i = 0; i < MAX_SINK_NUM; i++) {
        if (audio_pipe->build_by_user[i]) {
            return true;
        }
    }
    return false;
}

static bool need_auto_build(audio_pipeline_t *audio_pipe)
{
    for (int i = 0; i < MAX_SINK_NUM; i++) {
        if (audio_pipe->sink_cfg[i].audio_info.format_id && audio_pipe->build_by_user[i] == false) {
            return true;
        }
    }
    return false;
}

static esp_capture_err_t buildup_pipelines(audio_pipeline_t *audio_pipe)
{
    if (audio_pipe->pipeline_created) {
        return ESP_CAPTURE_ERR_OK;
    }
    audio_pipe->sink_num = get_sink_num(audio_pipe);
    if (audio_pipe->sink_num == 0) {
        return ESP_CAPTURE_ERR_OK;
    }
    int ret = ESP_GMF_ERR_OK;
    // Create source pipeline
    esp_gmf_pool_handle_t pool = audio_pipe->cfg.element_pool ? (esp_gmf_pool_handle_t)audio_pipe->cfg.element_pool : audio_pipe->pool;
    // Create source pipeline if more than one sink
    if ((audio_pipe->sink_num > 1) || have_user_pipe(audio_pipe)) {
        const char *copy_elements[] = {"aud_src", "share_copier"};
        ret = esp_gmf_pool_new_pipeline(pool, NULL, copy_elements, CAPTURE_ELEMS(copy_elements), NULL, &audio_pipe->src_pipeline);
        if (ret != ESP_GMF_ERR_OK) {
            return ESP_CAPTURE_ERR_NO_RESOURCES;
        }
    }

    if (need_auto_build(audio_pipe)) {
        esp_capture_audio_info_t max_sink_info = {};
        esp_capture_audio_info_t src_info = {};
        get_max_sink_cfg(audio_pipe, &max_sink_info);
        // Open source firstly
        audio_pipe->cfg.aud_src->open(audio_pipe->cfg.aud_src);
        ret = audio_pipe->cfg.aud_src->negotiate_caps(audio_pipe->cfg.aud_src, &max_sink_info, &src_info);
        if (ret != ESP_CAPTURE_ERR_OK && max_sink_info.format_id != ESP_CAPTURE_FMT_ID_PCM) {
            max_sink_info.format_id = ESP_CAPTURE_FMT_ID_PCM;
            ret = audio_pipe->cfg.aud_src->negotiate_caps(audio_pipe->cfg.aud_src, &max_sink_info, &src_info);
            if (ret != ESP_CAPTURE_ERR_OK) {
                ESP_LOGE(TAG, "Fail to negotiate src %d", ret);
                return ret;
            }
        }
        get_element_tag_by_caps(audio_pipe);

        // Build pipelines according negotiate info
        const char *proc_elements[AUDIO_PATH_OPS_MAX] = {NULL};
        int proc_num = 0;
        for (int i = 0; i < MAX_SINK_NUM; i++) {
            if (audio_pipe->sink_cfg[i].audio_info.format_id == ESP_CAPTURE_FMT_ID_NONE) {
                continue;
            }
            // Pipeline already created
            if (audio_pipe->enc_pipeline[i]) {
                continue;
            }
            proc_num = 0;
            // Process pipeline keep user defined not add src into it
            if ((audio_pipe->sink_num == 1) && have_user_pipe(audio_pipe) == false) {
                proc_elements[proc_num++] = "aud_src";
            }
            audio_path_ops_t ops[AUDIO_PATH_OPS_MAX] = {};
            // Sort elements for optimized performance
            sort_path_ops(ops, &src_info, &audio_pipe->sink_cfg[i].audio_info);
            for (int op_idx = 0; op_idx < CAPTURE_ELEMS(ops); op_idx++) {
                if (ops[op_idx] == AUDIO_PATH_OPS_NONE) {
                    break;
                }
                const char *element_name = get_ops_element(audio_pipe, ops[op_idx]);
                if (element_name == NULL) {
                    ESP_LOGE(TAG, "Can not find element for operation %d", ops[op_idx]);
                    return ESP_CAPTURE_ERR_INTERNAL;
                }
                if (is_dup_element(proc_elements, proc_num, element_name) == false) {
                    proc_elements[proc_num++] = element_name;
                }
            }
            if (audio_pipe->sink_cfg[i].audio_info.format_id != src_info.format_id) {
                const char *element_name = get_ops_element(audio_pipe, AUDIO_PATH_OPS_ENC);
                if (element_name == NULL) {
                    ESP_LOGE(TAG, "Can not find element for encoder");
                    return ESP_CAPTURE_ERR_INTERNAL;
                }
                // TODO currently only support encoder
                proc_elements[proc_num++] = element_name;
            }
            if (proc_num == 0) {
                // Add a dummy element in case no element can not form pipeline
                const char *element_name = get_ops_element(audio_pipe, AUDIO_PATH_OPS_BIT_CVT);
                if (element_name == NULL) {
                    ESP_LOGE(TAG, "Can not find element for bit convert");
                    return ESP_CAPTURE_ERR_INTERNAL;
                }
                proc_elements[proc_num++] = element_name;
            }
#if 0
            printf("Audio Pipeline %d: ", i);
            for (int j = 0; j < proc_num; j++) {
                printf("%s -> ", proc_elements[j]);
            }
            printf("\n");
#endif  /* 0 */
            ret = esp_gmf_pool_new_pipeline(pool, NULL, proc_elements, proc_num, NULL, &audio_pipe->enc_pipeline[i]);
            if (ret != ESP_GMF_ERR_OK) {
                return ESP_CAPTURE_ERR_NO_RESOURCES;
            }
        }
    }
    // Link pipelines
    ret = audio_pipe_link(audio_pipe);
    if (ret != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Fail to link pipelines");
        return ESP_CAPTURE_ERR_NO_RESOURCES;
    }
    audio_pipe->pipeline_created = true;
    return ret;
}

static esp_capture_err_t gmf_audio_pool_create(esp_capture_pipeline_builder_if_t *pipeline)
{
    audio_pipeline_t *audio_pipe = (audio_pipeline_t *)pipeline;
    esp_gmf_element_handle_t el = NULL;
    do {
        if (audio_pipe->cfg.element_pool) {
            return ESP_CAPTURE_ERR_OK;
        }
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
        return ESP_CAPTURE_ERR_OK;
    } while (0);
    return ESP_CAPTURE_ERR_NO_MEM;
}

static esp_capture_err_t gmf_audio_pipeline_reg_element(esp_capture_pipeline_builder_if_t *builder, esp_gmf_element_handle_t element)
{
    audio_pipeline_t *audio_pipe = (audio_pipeline_t *)builder;
    if (audio_pipe->pool == NULL) {
        return ESP_CAPTURE_ERR_INTERNAL;
    }
    esp_gmf_err_t ret = esp_gmf_pool_register_element_at_head(audio_pipe->pool, element, NULL);
    if (ret != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Fail to register element");
        return ESP_CAPTURE_ERR_NO_RESOURCES;
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t gmf_audio_pipeline_build(esp_capture_pipeline_builder_if_t *builder, uint8_t path_idx,
                                                  esp_capture_gmf_pipeline_cfg_t *pipe_cfg)
{
    audio_pipeline_t *audio_pipe = (audio_pipeline_t *)builder;
    if (path_idx >= MAX_SINK_NUM) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    if (audio_pipe->enc_pipeline[path_idx]) {
        ESP_LOGW(TAG, "Pipeline for %d already buildup", path_idx);
        return ESP_CAPTURE_ERR_OK;
    }
    esp_gmf_err_t ret = esp_gmf_pool_new_pipeline(audio_pipe->pool, NULL, pipe_cfg->element_tags, pipe_cfg->element_num, NULL, &audio_pipe->enc_pipeline[path_idx]);
    if (ret != ESP_GMF_ERR_OK) {
        return ESP_CAPTURE_ERR_NO_RESOURCES;
    }
    audio_pipe->build_by_user[path_idx] = true;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t gmf_audio_pipeline_get(esp_capture_pipeline_builder_if_t *builder, esp_capture_gmf_pipeline_t *pipe, uint8_t *pipeline_num)
{
    audio_pipeline_t *audio_pipe = (audio_pipeline_t *)builder;
    esp_capture_err_t ret = buildup_pipelines(audio_pipe);
    if (ret != ESP_CAPTURE_ERR_OK) {
        return ret;
    }
    uint8_t actual_pipe_num = audio_pipe->sink_num;
    if (audio_pipe->src_pipeline) {
        actual_pipe_num++;
    }
    if (pipe == NULL) {
        *pipeline_num = actual_pipe_num;
        return ESP_CAPTURE_ERR_OK;
    }
    if (*pipeline_num < actual_pipe_num) {
        return ESP_CAPTURE_ERR_NOT_ENOUGH;
    }
    int fill_pipe = 0;
    if (audio_pipe->src_pipeline) {
        pipe[fill_pipe].pipeline = audio_pipe->src_pipeline;
        pipe[fill_pipe].name = "aud_src";
        pipe[fill_pipe++].path_mask = (1 << audio_pipe->sink_num) - 1;
    }
    for (int i = 0; i < MAX_SINK_NUM; i++) {
        if (audio_pipe->enc_pipeline[i]) {
            pipe[fill_pipe].pipeline = audio_pipe->enc_pipeline[i];
            // TODO support more pipelines ?
            pipe[fill_pipe].name = i > 0 ? "aenc_1" : "aenc_0";
            pipe[fill_pipe++].path_mask = (1 << i);
        }
    }
    *pipeline_num = actual_pipe_num;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t gmf_audio_pipeline_get_element(esp_capture_pipeline_builder_if_t *builder, uint8_t path_idx, const char *tag, esp_gmf_element_handle_t *element)
{
    audio_pipeline_t *audio_pipe = (audio_pipeline_t *)builder;
    if (path_idx > MAX_SINK_NUM || audio_pipe->enc_pipeline[path_idx] == NULL) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    esp_gmf_err_t ret = esp_gmf_pipeline_get_el_by_name(audio_pipe->enc_pipeline[path_idx], tag, element);
    return (ret == ESP_GMF_ERR_OK) ? ESP_CAPTURE_ERR_OK : ESP_CAPTURE_ERR_NOT_FOUND;
}

static esp_capture_err_t gmf_audio_pipeline_release(esp_capture_pipeline_builder_if_t *builder)
{
    audio_pipeline_t *audio_pipe = (audio_pipeline_t *)builder;
    for (int i = 0; i < MAX_SINK_NUM; i++) {
        if (audio_pipe->enc_pipeline[i]) {
            // Not release user setting pipelines
            if (audio_pipe->build_by_user[i] == false) {
                esp_gmf_pipeline_destroy(audio_pipe->enc_pipeline[i]);
                audio_pipe->enc_pipeline[i] = NULL;
            } else {
                // Unregister input port only
                esp_gmf_element_handle_t element = NULL;
                esp_gmf_pipeline_get_head_el(audio_pipe->enc_pipeline[i], &element);
                esp_gmf_element_unregister_in_port(element, NULL);
            }
        }
    }
    if (audio_pipe->src_pipeline) {
        esp_gmf_pipeline_destroy(audio_pipe->src_pipeline);
        audio_pipe->src_pipeline = NULL;
    }
    audio_pipe->pipeline_created = false;
    return ESP_CAPTURE_ERR_OK;
}

static void gmf_audio_pipeline_destroy(esp_capture_pipeline_builder_if_t *builder)
{
    audio_pipeline_t *audio_pipe = (audio_pipeline_t *)builder;
    for (int i = 0; i < MAX_SINK_NUM; i++) {
        audio_pipe->build_by_user[i] = false;
    }
    gmf_audio_pipeline_release(builder);
    if (audio_pipe->pool) {
        esp_gmf_pool_deinit(audio_pipe->pool);
        audio_pipe->pool = NULL;
    }
    capture_free(audio_pipe);
}

static esp_capture_err_t gmf_audio_pipeline_set_cfg(esp_capture_pipeline_builder_if_t *builder, uint8_t path_idx, esp_capture_stream_info_t *sink_cfg)
{
    audio_pipeline_t *audio_pipe = (audio_pipeline_t *)builder;
    if (audio_pipe == NULL || path_idx >= MAX_SINK_NUM || sink_cfg == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    audio_pipe->sink_cfg[path_idx] = *sink_cfg;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t gmf_audio_pipeline_get_cfg(esp_capture_pipeline_builder_if_t *builder, uint8_t path_idx,
                                                    esp_capture_stream_info_t *sink_cfg)
{
    audio_pipeline_t *audio_pipe = (audio_pipeline_t *)builder;
    if (audio_pipe == NULL || path_idx >= MAX_SINK_NUM || sink_cfg == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    *sink_cfg = audio_pipe->sink_cfg[path_idx];
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_pipeline_builder_if_t *esp_capture_create_auto_audio_pipeline(esp_capture_gmf_auto_audio_pipeline_cfg_t *cfg)
{
    if (cfg == NULL || cfg->aud_src == NULL) {
        return NULL;
    }
    audio_pipeline_t *audio_pipe = (audio_pipeline_t *)calloc(1, sizeof(audio_pipeline_t));
    if (audio_pipe == NULL) {
        return NULL;
    }
    audio_pipe->base.create = gmf_audio_pool_create;
    audio_pipe->base.reg_element = gmf_audio_pipeline_reg_element;
    audio_pipe->base.set_sink_cfg = gmf_audio_pipeline_set_cfg;
    audio_pipe->base.get_sink_cfg = gmf_audio_pipeline_get_cfg;
    audio_pipe->base.build_pipeline = gmf_audio_pipeline_build;
    audio_pipe->base.get_pipelines = gmf_audio_pipeline_get;
    audio_pipe->base.get_element = gmf_audio_pipeline_get_element;
    // Auto negotiate for all paths
    audio_pipe->base.negotiate = esp_capture_audio_pipeline_auto_negotiate;
    audio_pipe->base.release_pipelines = gmf_audio_pipeline_release;
    audio_pipe->base.destroy = gmf_audio_pipeline_destroy;

    audio_pipe->cfg = *cfg;
    int ret = audio_pipe->base.create(&audio_pipe->base);
    if (ret != 0) {
        audio_pipe->base.destroy(&audio_pipe->base);
        return NULL;
    }
    return &audio_pipe->base;
}

/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "capture_pipeline_builder.h"
#include "capture_share_copy_el.h"
#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_video_enc.h"
#include "esp_gmf_video_overlay.h"
#include "esp_gmf_video_ppa.h"
#include "esp_gmf_video_fps_cvt.h"
#include "capture_video_src_el.h"
#include "esp_gmf_pool.h"
#include "capture_pipeline_utils.h"
#include "capture_pipeline_nego.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_video_scale.h"
#include "esp_gmf_video_crop.h"
#include "esp_gmf_video_color_convert.h"
#include "capture_utils.h"
#include "esp_log.h"

#define TAG "GMF_VID_PIPE"

#define MAX_SINK_NUM (2)

typedef enum {
    VIDEO_PATH_OPS_NONE        = 0,
    VIDEO_PATH_OPS_FPS_CONVERT = 1,
    VIDEO_PATH_OPS_RESIZE      = 2,
    VIDEO_PATH_OPS_CLR_CVT     = 3,
    VIDEO_PATH_OPS_ENC         = 4,
    VIDEO_PATH_OPS_MAX         = 5,
} video_path_ops_t;

typedef struct video_pipeline_t video_pipeline_t;

typedef struct {
    uint8_t            path;
    video_pipeline_t  *parent;
} video_path_ctx_t;

struct video_pipeline_t {
    esp_capture_pipeline_builder_if_t          base;
    esp_capture_gmf_auto_video_pipeline_cfg_t  cfg;
    esp_gmf_pool_handle_t                      pool;
    uint8_t                                    sink_num;
    bool                                       pipeline_created;
    esp_gmf_pipeline_handle_t                  src_pipeline;
    esp_gmf_pipeline_handle_t                  enc_pipeline[MAX_SINK_NUM];
    bool                                       build_by_user[MAX_SINK_NUM];
    video_path_ctx_t                           path_ctx[MAX_SINK_NUM];
    esp_capture_stream_info_t                  sink_cfg[MAX_SINK_NUM];
    const char                                *ops_tags[VIDEO_PATH_OPS_MAX];
};

extern const char *esp_gmf_video_get_format_string(uint32_t format_id);

static esp_capture_err_t gmf_video_pool_create(esp_capture_pipeline_builder_if_t *pipeline)
{
    video_pipeline_t *video_pipe = (video_pipeline_t *)pipeline;
    esp_gmf_obj_handle_t el = NULL;
    // When use user defined pool return directly
    if (video_pipe->cfg.element_pool) {
        return ESP_CAPTURE_ERR_OK;
    }
    do {
        // Create default pool and default elements
        esp_gmf_pool_init(&video_pipe->pool);
        if (video_pipe->pool == NULL) {
            break;
        }
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
        return ESP_CAPTURE_ERR_OK;
    } while (0);
    return ESP_CAPTURE_ERR_NO_MEM;
}

static uint8_t get_sink_num(video_pipeline_t *video_pipe)
{
    uint8_t sink_num = 0;
    for (int i = 0; i < MAX_SINK_NUM; i++) {
        if (video_pipe->sink_cfg[i].video_info.format_id) {
            sink_num++;
        }
    }
    return sink_num;
}

static bool is_encoded(esp_capture_format_id_t format_id)
{
    if (format_id == ESP_CAPTURE_FMT_ID_H264 || format_id == ESP_CAPTURE_FMT_ID_MJPEG) {
        return true;
    }
    return false;
}

static int resolution_differ(esp_capture_video_info_t *a, esp_capture_video_info_t *b)
{
    return ((int)a->width * a->height) - ((int)b->width * b->height);
}

static void get_max_sink_cfg(video_pipeline_t *video_pipe, esp_capture_video_info_t *max_sink_info)
{
    int8_t sink_num = 0;
    for (int i = 0; i < MAX_SINK_NUM; i++) {
        if (video_pipe->sink_cfg[i].video_info.format_id == ESP_CAPTURE_FMT_ID_NONE) {
            continue;
        }
        if (sink_num == 0) {
            *max_sink_info = video_pipe->sink_cfg[i].video_info;
        } else {
            int res_diff = resolution_differ(max_sink_info, &video_pipe->sink_cfg[i].video_info);
            if (res_diff < 0 || (res_diff == 0 && !is_encoded(video_pipe->sink_cfg[i].video_info.format_id))) {
                // Align with auto negotiate logic none encoder path high priority
                max_sink_info->format_id = video_pipe->sink_cfg[i].video_info.format_id;
            }
            MAX_VID_SINK_CFG((*max_sink_info), video_pipe->sink_cfg[i]);
        }
        sink_num++;
    }
}

static void sort_path_ops(video_path_ops_t *ops, esp_capture_video_info_t *src, esp_capture_video_info_t *dst)
{
    int i = 0;
    if (src->fps > dst->fps) {
        ops[i++] = VIDEO_PATH_OPS_FPS_CONVERT;
    }
    int res_diff = resolution_differ(src, dst);
    if (res_diff > 0) {
        ops[i++] = VIDEO_PATH_OPS_RESIZE;
    }
    if (src->format_id != dst->format_id) {
        ops[i++] = VIDEO_PATH_OPS_CLR_CVT;
    }
    if (res_diff < 0) {
        ops[i++] = VIDEO_PATH_OPS_RESIZE;
    }
    // TODO increase fps not support currently
    if (src->fps < dst->fps) {
        ops[i++] = VIDEO_PATH_OPS_FPS_CONVERT;
    }
}

static void get_element_tag_by_caps(video_pipeline_t *video_pipe)
{
    const void *iter = NULL;
    esp_gmf_element_handle_t element = NULL;
    while (esp_gmf_pool_iterate_element(video_pipe->pool, &iter, &element) == ESP_GMF_ERR_OK) {
        const esp_gmf_cap_t *caps = NULL;
        esp_gmf_element_get_caps(element, &caps);
        while (caps) {
            video_path_ops_t ops = VIDEO_PATH_OPS_NONE;
            if (caps->cap_eightcc == ESP_GMF_CAPS_VIDEO_COLOR_CONVERT) {
                ops = VIDEO_PATH_OPS_CLR_CVT;
            } else if (caps->cap_eightcc == ESP_GMF_CAPS_VIDEO_FPS_CVT) {
                ops = VIDEO_PATH_OPS_FPS_CONVERT;
            } else if (caps->cap_eightcc == ESP_GMF_CAPS_VIDEO_SCALE) {
                ops = VIDEO_PATH_OPS_RESIZE;
            } else if (caps->cap_eightcc == ESP_GMF_CAPS_VIDEO_ENCODER) {
                ops = VIDEO_PATH_OPS_ENC;
            }
            if (ops && video_pipe->ops_tags[ops] == NULL) {
                video_pipe->ops_tags[ops] = OBJ_GET_TAG(element);
            }
            caps = caps->next;
        }
    }
}

static const char *get_ops_element(video_pipeline_t *video_pipe, video_path_ops_t ops)
{
    return video_pipe->ops_tags[ops];
    return NULL;
}

static bool is_dup_element(const char **elements, int n, const char *cur)
{
    for (int i = 0; i < n; i++) {
        // No need strcmp
        if (cur == elements[i]) {
            return true;
        }
    }
    return false;
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
    capture_share_copy_el_enable(cp_element, ctx->path, false);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_element_handle_t get_src_element(video_pipeline_t *video_pipe)
{
    esp_gmf_element_handle_t src_element = NULL;
    if (video_pipe->src_pipeline) {
        esp_gmf_pipeline_get_el_by_name(video_pipe->src_pipeline, "vid_src", &src_element);
    } else if (video_pipe->sink_num == 1) {
        for (int i = 0; i < MAX_SINK_NUM; i++) {
            if (video_pipe->enc_pipeline[i]) {
                esp_gmf_pipeline_get_el_by_name(video_pipe->enc_pipeline[i], "vid_src", &src_element);
                break;
            }
        }
    }
    return src_element;
}

static esp_capture_err_t video_pipeline_link(video_pipeline_t *video_pipe)
{
    // Link video source to source element
    esp_gmf_element_handle_t src_element = get_src_element(video_pipe);
    if (src_element) {
        capture_video_src_el_set_src_if(src_element, video_pipe->cfg.vid_src);
    }
    // Return if only one sink
    if (video_pipe->src_pipeline == NULL) {
        return ESP_CAPTURE_ERR_OK;
    }

    esp_gmf_element_handle_t cp_element = NULL;
    esp_gmf_pipeline_get_el_by_name(video_pipe->src_pipeline, "share_copier", &cp_element);

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
    }
    return ESP_CAPTURE_ERR_OK;
}

static bool have_user_pipe(video_pipeline_t *video_pipe)
{
    for (int i = 0; i < MAX_SINK_NUM; i++) {
        if (video_pipe->build_by_user[i]) {
            return true;
        }
    }
    return false;
}

static bool need_auto_build(video_pipeline_t *video_pipe)
{
    for (int i = 0; i < MAX_SINK_NUM; i++) {
        if (video_pipe->sink_cfg[i].video_info.format_id && video_pipe->build_by_user[i] == false) {
            return true;
        }
    }
    return false;
}

static esp_capture_err_t auto_negotiate(video_pipeline_t *video_pipe, esp_capture_video_info_t *src_info)
{
    esp_capture_video_info_t max_sink_info = {};
    get_max_sink_cfg(video_pipe, &max_sink_info);
    ESP_LOGI(TAG, "Build pipe nego for format %s %dx%d %d fps",
             esp_gmf_video_get_format_string((uint32_t)max_sink_info.format_id),
             max_sink_info.width, max_sink_info.height, max_sink_info.fps);
    // Open source and do negotiate firstly
    esp_capture_err_t ret = video_pipe->cfg.vid_src->open(video_pipe->cfg.vid_src);
    CAPTURE_RETURN_ON_FAIL(ret);
    ret = video_pipe->cfg.vid_src->negotiate_caps(video_pipe->cfg.vid_src, &max_sink_info, src_info);
    if (ret != ESP_CAPTURE_ERR_OK) {
        // Negotiate with any format
        max_sink_info.format_id = ESP_CAPTURE_FMT_ID_ANY;
        ret = video_pipe->cfg.vid_src->negotiate_caps(video_pipe->cfg.vid_src, &max_sink_info, src_info);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to negotiate src %d", ret);
            CAPTURE_RETURN_ON_FAIL(ret);
        }
        // For simple case we add color convert for all paths
        src_info->format_id = ESP_CAPTURE_FMT_ID_ANY;
    }
    return ret;
}

static esp_capture_err_t buildup_pipelines(video_pipeline_t *video_pipe)
{
    // Pipeline already created
    if (video_pipe->pipeline_created) {
        return ESP_CAPTURE_ERR_OK;
    }
    video_pipe->sink_num = get_sink_num(video_pipe);
    if (video_pipe->sink_num == 0) {
        return ESP_CAPTURE_ERR_OK;
    }
    // Build pipelines according negotiate result
    const char *proc_elements[VIDEO_PATH_OPS_MAX] = {NULL};
    int proc_num = 0;
    esp_gmf_pool_handle_t pool = video_pipe->cfg.element_pool ? (esp_gmf_pool_handle_t)video_pipe->cfg.element_pool : video_pipe->pool;
    // Create source pipeline if more than one sink
    if ((video_pipe->sink_num > 1) || have_user_pipe(video_pipe)) {
        const char *copy_elements[] = {"vid_src", "share_copier"};
        int ret = esp_gmf_pool_new_pipeline(pool, NULL, copy_elements, CAPTURE_ELEMS(copy_elements), NULL, &video_pipe->src_pipeline);
        if (ret != ESP_GMF_ERR_OK) {
            return ESP_CAPTURE_ERR_NO_RESOURCES;
        }
    }
    int ret = ESP_CAPTURE_ERR_OK;
    if (need_auto_build(video_pipe)) {
        esp_capture_video_info_t src_info = {};
        ret = auto_negotiate(video_pipe, &src_info);
        if (ret != ESP_CAPTURE_ERR_OK) {
            return ret;
        }
        get_element_tag_by_caps(video_pipe);

        for (int i = 0; i < MAX_SINK_NUM; i++) {
            if (video_pipe->sink_cfg[i].video_info.format_id == ESP_CAPTURE_FMT_ID_NONE) {
                continue;
            }
            // Pipeline already created
            if (video_pipe->enc_pipeline[i]) {
                continue;
            }
            proc_num = 0;
            // Add source to same pipeline if only one sink
            if (video_pipe->sink_num == 1 && (have_user_pipe(video_pipe) == false)) {
                proc_elements[proc_num++] = "vid_src";
            }
#ifdef CONFIG_ESP_CAPTURE_ENABLE_VIDEO_OVERLAY
            proc_elements[proc_num++] = "vid_overlay";
#endif  /* CONFIG_ESP_CAPTURE_ENABLE_VIDEO_OVERLAY */

            video_path_ops_t ops[VIDEO_PATH_OPS_MAX] = {};
            // Sort elements for optimized performance
            sort_path_ops(ops, &src_info, &video_pipe->sink_cfg[i].video_info);
            for (int op_idx = 0; op_idx < CAPTURE_ELEMS(ops); op_idx++) {
                if (ops[op_idx] == VIDEO_PATH_OPS_NONE) {
                    break;
                }
                const char *element_name = get_ops_element(video_pipe, ops[op_idx]);
                if (is_dup_element(proc_elements, proc_num, element_name) == false) {
                    proc_elements[proc_num++] = element_name;
                }
            }
            // Always add video encoder
            if (1 || is_encoded(video_pipe->sink_cfg[i].video_info.format_id)) {
                proc_elements[proc_num++] = "vid_enc";
            }
#if 0
            printf("Video Pipeline %d: ", i);
            for (int j = 0; j < proc_num; j++) {
                printf("%s -> ", proc_elements[j]);
            }
            printf("\n");
#endif  /* 0 */
            ret = esp_gmf_pool_new_pipeline(pool, NULL, (const char **)proc_elements, proc_num, NULL, &video_pipe->enc_pipeline[i]);
            if (ret != ESP_GMF_ERR_OK) {
                ESP_LOGE(TAG, "Fail to create pipeline");
                return ESP_CAPTURE_ERR_NO_RESOURCES;
            }
        }
    }
    // Link pipelines
    ret = video_pipeline_link(video_pipe);
    if (ret != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(TAG, "Fail to link pipelines");
        return ESP_CAPTURE_ERR_NO_RESOURCES;
    }
    video_pipe->pipeline_created = true;
    return ret;
}

static esp_capture_err_t gmf_video_pipeline_reg_element(esp_capture_pipeline_builder_if_t *builder,
                                                        esp_gmf_element_handle_t element)
{
    video_pipeline_t *video_pipe = (video_pipeline_t *)builder;
    if (video_pipe->pool == NULL) {
        return ESP_CAPTURE_ERR_INTERNAL;
    }
    esp_gmf_err_t ret = esp_gmf_pool_register_element_at_head(video_pipe->pool, element, NULL);
    if (ret != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Fail to register element");
        return ESP_CAPTURE_ERR_NO_RESOURCES;
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t gmf_video_pipeline_build(esp_capture_pipeline_builder_if_t *builder, uint8_t path_idx,
                                                  esp_capture_gmf_pipeline_cfg_t *pipe_cfg)
{
    video_pipeline_t *video_pipe = (video_pipeline_t *)builder;
    if (path_idx >= MAX_SINK_NUM) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    if (video_pipe->enc_pipeline[path_idx]) {
        ESP_LOGW(TAG, "Pipeline for %d already buildup", path_idx);
        return ESP_CAPTURE_ERR_OK;
    }
    int ret = esp_gmf_pool_new_pipeline(video_pipe->pool, NULL, pipe_cfg->element_tags, pipe_cfg->element_num, NULL, &video_pipe->enc_pipeline[path_idx]);
    if (ret != ESP_GMF_ERR_OK) {
        return ESP_CAPTURE_ERR_NO_RESOURCES;
    }
    video_pipe->build_by_user[path_idx] = true;
    return ESP_CAPTURE_ERR_OK;
}

static int gmf_video_pipeline_get_element(esp_capture_pipeline_builder_if_t *builder, uint8_t path_idx, const char *tag, esp_gmf_element_handle_t *element)
{
    video_pipeline_t *video_pipe = (video_pipeline_t *)builder;
    if (path_idx > MAX_SINK_NUM || video_pipe->enc_pipeline[path_idx] == NULL) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    int ret = esp_gmf_pipeline_get_el_by_name(video_pipe->enc_pipeline[path_idx], tag, element);
    return (ret == ESP_GMF_ERR_OK) ? ESP_CAPTURE_ERR_OK : ESP_CAPTURE_ERR_NOT_FOUND;
}

static int gmf_video_pipeline_get(esp_capture_pipeline_builder_if_t *builder, esp_capture_gmf_pipeline_t *pipe, uint8_t *pipeline_num)
{
    video_pipeline_t *video_pipe = (video_pipeline_t *)builder;
    int ret = buildup_pipelines(video_pipe);
    if (ret != ESP_CAPTURE_ERR_OK) {
        return ret;
    }
    uint8_t actual_pipe_num = video_pipe->sink_num;
    if (video_pipe->src_pipeline) {
        actual_pipe_num++;
    }
    if (pipe == NULL) {
        *pipeline_num = actual_pipe_num;
        return 0;
    }
    if (*pipeline_num < actual_pipe_num) {
        return -1;
    }
    int fill_pipe = 0;
    if (video_pipe->src_pipeline) {
        pipe[fill_pipe].pipeline = video_pipe->src_pipeline;
        pipe[fill_pipe].name = "vid_src";
        pipe[fill_pipe++].path_mask = (1 << video_pipe->sink_num) - 1;
    }
    for (int i = 0; i < MAX_SINK_NUM; i++) {
        if (video_pipe->enc_pipeline[i]) {
            pipe[fill_pipe].pipeline = video_pipe->enc_pipeline[i];
            // TODO support more pipelines ?
            pipe[fill_pipe].name = i > 0 ? "venc_1" : "venc_0";
            pipe[fill_pipe++].path_mask = (1 << i);
        }
    }
    *pipeline_num = fill_pipe;
    return 0;
}

static int gmf_video_pipeline_set_cfg(esp_capture_pipeline_builder_if_t *builder, uint8_t path_idx, esp_capture_stream_info_t *sink_cfg)
{
    video_pipeline_t *video_pipe = (video_pipeline_t *)builder;
    if (video_pipe == NULL || path_idx >= MAX_SINK_NUM || sink_cfg == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    video_pipe->sink_cfg[path_idx] = *sink_cfg;
    return ESP_CAPTURE_ERR_OK;
}
static int gmf_video_pipeline_get_cfg(esp_capture_pipeline_builder_if_t *builder, uint8_t path_idx,
                                      esp_capture_stream_info_t *sink_cfg)
{
    video_pipeline_t *video_pipe = (video_pipeline_t *)builder;
    if (video_pipe == NULL || path_idx >= MAX_SINK_NUM || sink_cfg == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    *sink_cfg = video_pipe->sink_cfg[path_idx];
    return ESP_CAPTURE_ERR_OK;
}

static int gmf_video_pipeline_release(esp_capture_pipeline_builder_if_t *pipeline)
{
    video_pipeline_t *video_pipe = (video_pipeline_t *)pipeline;
    for (int i = 0; i < MAX_SINK_NUM; i++) {
        if (video_pipe->enc_pipeline[i]) {
            // Do not destroyed user created pipeline
            if (video_pipe->build_by_user[i] == false) {
                esp_gmf_pipeline_destroy(video_pipe->enc_pipeline[i]);
                video_pipe->enc_pipeline[i] = NULL;
            } else {
                // Unregister input port only
                esp_gmf_element_handle_t element = NULL;
                esp_gmf_pipeline_get_head_el(video_pipe->enc_pipeline[i], &element);
                esp_gmf_element_unregister_in_port(element, NULL);
            }
        }
    }
    if (video_pipe->src_pipeline) {
        esp_gmf_pipeline_destroy(video_pipe->src_pipeline);
        video_pipe->src_pipeline = NULL;
    }
    video_pipe->pipeline_created = false;
    return ESP_CAPTURE_ERR_OK;
}

static void gmf_video_pipeline_destroy(esp_capture_pipeline_builder_if_t *builder)
{
    video_pipeline_t *video_pipe = (video_pipeline_t *)builder;
    for (int i = 0; i < MAX_SINK_NUM; i++) {
        video_pipe->build_by_user[i] = false;
    }
    gmf_video_pipeline_release(builder);
    if (video_pipe->pool) {
        esp_gmf_pool_deinit(video_pipe->pool);
        video_pipe->pool = NULL;
    }
    capture_free(video_pipe);
}

esp_capture_pipeline_builder_if_t *esp_capture_create_auto_video_pipeline(esp_capture_gmf_auto_video_pipeline_cfg_t *cfg)
{
    video_pipeline_t *video_pipe = (video_pipeline_t *)capture_calloc(1, sizeof(video_pipeline_t));
    if (video_pipe == NULL) {
        return NULL;
    }
    video_pipe->base.create = gmf_video_pool_create;
    video_pipe->base.reg_element = gmf_video_pipeline_reg_element;
    video_pipe->base.build_pipeline = gmf_video_pipeline_build;
    video_pipe->base.get_pipelines = gmf_video_pipeline_get;
    video_pipe->base.get_element = gmf_video_pipeline_get_element;
    video_pipe->base.set_sink_cfg = gmf_video_pipeline_set_cfg;
    video_pipe->base.get_sink_cfg = gmf_video_pipeline_get_cfg;
    video_pipe->base.negotiate = esp_capture_video_pipeline_auto_negotiate;
    video_pipe->base.release_pipelines = gmf_video_pipeline_release;
    video_pipe->base.destroy = gmf_video_pipeline_destroy;

    video_pipe->cfg = *cfg;
    int ret = video_pipe->base.create(&video_pipe->base);
    if (ret != 0) {
        video_pipe->base.destroy(&video_pipe->base);
        return NULL;
    }
    return &video_pipe->base;
}

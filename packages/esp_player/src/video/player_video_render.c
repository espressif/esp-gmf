/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "esp_log.h"

#include "esp_video_render.h"
#include "esp_gmf_new_databus.h"
#include "esp_gmf_video_element.h"

#include "player_video_render.h"
#include "player_sync.h"

static const char *TAG = "ESP_PLAYER_VIDEO_RENDER";

typedef struct {
    esp_gmf_element_t                 parent;
    esp_video_render_stream_handle_t  stream_hd;
    uint32_t                          frame_size;
    bool                              is_seeking;
} player_video_render_t;

static uint32_t get_frame_data_size(uint16_t width, uint16_t height, uint32_t format)
{
    uint32_t pixels = (uint32_t)width * height;
    switch (format) {
        case ESP_VIDEO_RENDER_FORMAT_YUV420P:
            return pixels * 3 / 2;
        case ESP_VIDEO_RENDER_FORMAT_RGB888:
        case ESP_VIDEO_RENDER_FORMAT_BGR888:
            return pixels * 3;
        case ESP_VIDEO_RENDER_FORMAT_RGB565:
        case ESP_VIDEO_RENDER_FORMAT_RGB565_BE:
        case ESP_VIDEO_RENDER_FORMAT_YUV422:
        case ESP_VIDEO_RENDER_FORMAT_YUV422P:
            return pixels * 2;
        default:
            return pixels * 2;
    }
}

static bool player_video_render_has_visible_rect(const player_video_render_config_t *cfg)
{
    return (cfg && cfg->display_width > 0 && cfg->display_height > 0 &&
            cfg->display_width <= cfg->width && cfg->display_height <= cfg->height &&
            (cfg->display_width != cfg->width || cfg->display_height != cfg->height));
}

static bool player_video_render_should_mask_bottom_padding(const player_video_render_config_t *cfg)
{
    return (player_video_render_has_visible_rect(cfg) &&
            cfg->display_width == cfg->width && cfg->display_height < cfg->height);
}

static void player_video_render_mask_bottom_padding(player_video_render_config_t *cfg,
                                                    uint8_t *buf, uint32_t buf_size)
{
    if (cfg == NULL || buf == NULL || !player_video_render_should_mask_bottom_padding(cfg)) {
        return;
    }
    if (cfg->width == 0 || cfg->height == 0 || cfg->display_height >= cfg->height) {
        return;
    }
    switch ((esp_video_render_format_t)cfg->decoded_format) {
        case ESP_VIDEO_RENDER_FORMAT_YUV420P: {
            uint32_t w = cfg->width;
            uint32_t h = cfg->height;
            uint32_t y_size = w * h;
            uint32_t uv_size = y_size / 4;
            if (buf_size < y_size + uv_size * 2) {
                return;
            }
            uint32_t vis_h = cfg->display_height;
            uint32_t y_tail_off = w * vis_h;
            uint32_t y_tail_len = w * (h - vis_h);
            memset(buf + y_tail_off, 0x00, y_tail_len);
            uint32_t chroma_w = w / 2;
            uint32_t chroma_vis_h = vis_h / 2;
            uint32_t chroma_h = h / 2;
            uint32_t chroma_tail_len = (chroma_h - chroma_vis_h) * chroma_w;
            memset(buf + y_size + chroma_vis_h * chroma_w, 0x80, chroma_tail_len);
            memset(buf + y_size + uv_size + chroma_vis_h * chroma_w, 0x80, chroma_tail_len);
            break;
        }
        case ESP_VIDEO_RENDER_FORMAT_RGB565:
        case ESP_VIDEO_RENDER_FORMAT_RGB565_BE:
        case ESP_VIDEO_RENDER_FORMAT_YUV422:
        case ESP_VIDEO_RENDER_FORMAT_YUV422P: {
            uint32_t line = cfg->width * 2;
            uint32_t off = line * cfg->display_height;
            if (off < buf_size) {
                memset(buf + off, 0x00, buf_size - off);
            }
            break;
        }
        case ESP_VIDEO_RENDER_FORMAT_RGB888:
        case ESP_VIDEO_RENDER_FORMAT_BGR888: {
            uint32_t line = cfg->width * 3;
            uint32_t off = line * cfg->display_height;
            if (off < buf_size) {
                memset(buf + off, 0x00, buf_size - off);
            }
            break;
        }
        default:
            break;
    }
}

static esp_gmf_job_err_t player_video_render_open(esp_gmf_element_handle_t self, void *para)
{
    player_video_render_config_t *cfg = (player_video_render_config_t *)OBJ_GET_CFG(self);
    if (cfg == NULL || cfg->render_handle == NULL) {
        ESP_LOGE(TAG, "Invalid configuration or render handle");
        return ESP_GMF_JOB_ERR_FAIL;
    }
    player_video_render_t *video_render = (player_video_render_t *)self;
    ESP_LOGI(TAG, "Opening: width=%d height=%d fmt=0x%lx fps=%ld",
             (int)cfg->width, (int)cfg->height, cfg->decoded_format, cfg->fps);

    video_render->frame_size = get_frame_data_size(cfg->width, cfg->height, cfg->decoded_format);

    esp_video_render_stream_info_t stream_info = {
        .info = {
            .format = (esp_video_render_format_t)cfg->decoded_format,
            .width = cfg->width,
            .height = cfg->height,
            .fps = (uint8_t)cfg->fps,
        },
        .cached = false,
    };
    esp_video_render_err_t ret = esp_video_render_stream_open(
        cfg->render_handle, &stream_info, &video_render->stream_hd);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to open video render stream, ret=%d", ret);
        return ESP_GMF_JOB_ERR_FAIL;
    }
    if (player_video_render_should_mask_bottom_padding(cfg)) {
        ESP_LOGW(TAG, "Apply bottom padding mask decoded=%ux%u display=%ux%u",
                 (unsigned int)cfg->width, (unsigned int)cfg->height,
                 (unsigned int)cfg->display_width, (unsigned int)cfg->display_height);
    } else if (player_video_render_has_visible_rect(cfg)) {
        esp_video_render_rect_t src_rect = {
            .x = 0,
            .y = 0,
            .width = cfg->display_width,
            .height = cfg->display_height,
        };
        esp_video_render_rect_t disp_rect = src_rect;
        esp_video_render_err_t src_ret = esp_video_render_stream_set_src_rect(video_render->stream_hd, &src_rect);
        esp_video_render_err_t disp_ret = esp_video_render_stream_set_disp_rect(video_render->stream_hd, &disp_rect);
        ESP_LOGW(TAG, "Apply visible crop decoded=%ux%u display=%ux%u src_ret=%d disp_ret=%d",
                 (unsigned int)cfg->width, (unsigned int)cfg->height,
                 (unsigned int)cfg->display_width, (unsigned int)cfg->display_height,
                 src_ret, disp_ret);
    }
    ESP_LOGI(TAG, "Video render stream opened");
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t player_video_render_close(esp_gmf_element_handle_t self, void *para)
{
    player_video_render_t *video_render = (player_video_render_t *)self;
    if (video_render->stream_hd) {
        esp_video_render_stream_close(video_render->stream_hd);
        video_render->stream_hd = NULL;
    }
    ESP_LOGI(TAG, "Video render closed");
    return ESP_GMF_JOB_ERR_OK;
}

static inline bool player_video_render_abort_during_seek(const player_video_render_config_t *cfg, esp_gmf_err_io_t io_ret)
{
    return io_ret == ESP_GMF_IO_ABORT && cfg && cfg->sync_handle && player_sync_get_seek_in_progress(cfg->sync_handle);
}

static esp_gmf_job_err_t player_video_render_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_job_err_t job_ret = ESP_GMF_JOB_ERR_OK;
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_payload_t *in_load = NULL;
    player_video_render_t *video_render = (player_video_render_t *)self;
    if (video_render->stream_hd == NULL) {
        ESP_LOGE(TAG, "process while stream not opened");
        return ESP_GMF_JOB_ERR_FAIL;
    }
    player_video_render_config_t *cfg = (player_video_render_config_t *)OBJ_GET_CFG(self);
    esp_gmf_err_io_t io_ret = esp_gmf_port_acquire_in(in_port, &in_load, video_render->frame_size, ESP_GMF_MAX_DELAY);
    if (player_video_render_abort_during_seek(cfg, io_ret)) {
        vTaskDelay(1);
        return ESP_GMF_JOB_ERR_CONTINUE;
    }
    ESP_GMF_PORT_ACQUIRE_IN_CHECK(TAG, io_ret, job_ret, goto _process_release);

    if (video_render->is_seeking) {
        esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY);
        vTaskDelay(1);
        return ESP_GMF_JOB_ERR_OK;
    }
    if (cfg->sync_handle) {
        player_sync_video_fps_sync(cfg->sync_handle);
        if (!in_load->is_done && !player_sync_video_render_frame(cfg->sync_handle, in_load->pts)) {
            ESP_LOGD(TAG, "Drop frame at render (PTS %llu)", (unsigned long long)in_load->pts);
            goto _process_release;
        }
    }
    if (in_load->is_done == true) {
        if (cfg->sync_handle && player_sync_get_seek_in_progress(cfg->sync_handle)) {
            esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY);
            vTaskDelay(1);
            return ESP_GMF_JOB_ERR_OK;
        }
        job_ret = ESP_GMF_JOB_ERR_DONE;
        if (in_load->valid_size != video_render->frame_size) {
            goto _process_release;
        }
    }

    esp_video_render_frame_t frame = {
        .format = (esp_video_render_format_t)cfg->decoded_format,
        .width = (uint16_t)cfg->width,
        .height = (uint16_t)cfg->height,
        .data = in_load->buf,
        .size = in_load->valid_size,
    };
    player_video_render_mask_bottom_padding(cfg, frame.data, frame.size);
    esp_video_render_stream_write(video_render->stream_hd, &frame);

_process_release:
    if (in_load) {
        io_ret = esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY);
        if (player_video_render_abort_during_seek(cfg, io_ret)) {
            return ESP_GMF_JOB_ERR_CONTINUE;
        }
        ESP_GMF_PORT_RELEASE_IN_CHECK(TAG, io_ret, job_ret, return job_ret);
    }
    return job_ret;
}

static esp_gmf_err_t player_video_render_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return player_video_render_init((player_video_render_config_t *)cfg, handle);
}

static esp_gmf_err_t player_video_render_destroy(esp_gmf_element_handle_t self)
{
    ESP_LOGI(TAG, "Destroyed, %p", self);
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_element_deinit(self);
    esp_gmf_oal_free(self);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t player_video_render_init(player_video_render_config_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    *handle = NULL;

    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    player_video_render_t *video_render = NULL;
    player_video_render_config_t *cfg = NULL;

    video_render = esp_gmf_oal_calloc(1, sizeof(player_video_render_t));
    ESP_GMF_MEM_VERIFY(TAG, video_render, {return ESP_GMF_ERR_MEMORY_LACK;}, "video_render", sizeof(player_video_render_t));
    video_render->is_seeking = false;

    cfg = esp_gmf_oal_calloc(1, sizeof(player_video_render_config_t));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto VIDEO_RENDER_INIT_FAIL;}, "video_render configuration", sizeof(player_video_render_config_t));

    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)video_render;
    obj->new_obj = player_video_render_new;
    obj->del_obj = player_video_render_destroy;
    esp_gmf_obj_set_config(obj, cfg, sizeof(player_video_render_config_t));
    if (config) {
        memcpy(cfg, config, sizeof(player_video_render_config_t));
    } else {
        player_video_render_config_t dcfg = DEFAULT_PLAYER_VIDEO_RENDER_CONFIG();
        memcpy(cfg, &dcfg, sizeof(player_video_render_config_t));
    }
    if (cfg->render_handle != NULL) {
        esp_video_render_clr_t bg = {.r = 0x00, .g = 0x00, .b = 0x00};
        if (esp_video_render_set_bg_color((esp_video_render_handle_t)cfg->render_handle, &bg) != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGW(TAG, "set_bg_color failed (ensure esp_video_render_set_display before starting render pipeline)");
        }
    }
    ret = esp_gmf_obj_set_tag(obj, "vid_render");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto VIDEO_RENDER_INIT_FAIL, "Failed to set obj tag");
    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = false;
    ret = esp_gmf_element_init(video_render, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto VIDEO_RENDER_INIT_FAIL, "Failed to initialize video render element");
    ESP_GMF_ELEMENT_GET(video_render)->ops.open = player_video_render_open;
    ESP_GMF_ELEMENT_GET(video_render)->ops.process = player_video_render_process;
    ESP_GMF_ELEMENT_GET(video_render)->ops.close = player_video_render_close;

    *handle = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;

VIDEO_RENDER_INIT_FAIL:
    player_video_render_destroy((esp_gmf_obj_t *)video_render);
    return ret;
}

esp_gmf_err_t player_video_render_flush_enable(esp_gmf_element_handle_t handle, bool enable)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG;);
    player_video_render_t *video_render = (player_video_render_t *)handle;
    video_render->is_seeking = enable;
    return ESP_GMF_ERR_OK;
}

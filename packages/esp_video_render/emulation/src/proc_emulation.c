/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "video_render_proc.h"
#include "video_render_sys.h"
#include "esp_gmf_video_param.h"

#include <string.h>

/**
 * @brief  This is just the stub implementation of video_render_proc.h
 *         Which used when EMU_WITH_GSTREAMER is OFF
 *         It is a just pass-through implementation without no any processing support
 */

typedef enum {
    // keep file-local enum only for readability; real handle payload uses emu_gmf_video_element_t
    EMU_PROC_UNUSED = 0,
} emu_el_kind_t;

typedef struct {
    esp_video_render_frame_info_t  in;
    esp_video_render_frame_info_t  out;
    bool                           opened;

    esp_video_render_write_cb_t  writer;
    void                        *writer_ctx;
    port_acquire                 out_acquire;
    port_release                 out_release;
    void                        *out_ctx;

    emu_gmf_video_element_t  crop_el;
    emu_gmf_video_element_t  scale_el;
} emu_proc_t;

esp_video_render_err_t video_render_proc_create(esp_gmf_pool_handle_t pool, video_render_proc_handle_t *proc)
{
    (void)pool;
    if (!proc) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    emu_proc_t *p = (emu_proc_t *)video_render_calloc(1, sizeof(*p));
    if (!p) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    p->crop_el.kind = EMU_GMF_VIDEO_EL_CROP;
    p->scale_el.kind = EMU_GMF_VIDEO_EL_SCALE;
    *proc = (video_render_proc_handle_t)p;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_proc_add(video_render_proc_handle_t proc, esp_video_render_proc_type_t *proc_type,
                                             uint8_t proc_num)
{
    (void)proc;
    (void)proc_type;
    (void)proc_num;
    // No-op in emulation (we expose CROP/SCALE elements regardless).
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_proc_open(video_render_proc_handle_t proc,
                                              esp_video_render_frame_info_t *in_frame_info,
                                              esp_video_render_frame_info_t *out_frame_info)
{
    if (!proc || !in_frame_info || !out_frame_info) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    emu_proc_t *p = (emu_proc_t *)proc;
    p->in = *in_frame_info;
    p->out = *out_frame_info;
    p->opened = true;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_gmf_element_handle_t video_render_proc_get_element(video_render_proc_handle_t proc, esp_video_render_proc_type_t type)
{
    if (!proc) {
        return NULL;
    }
    emu_proc_t *p = (emu_proc_t *)proc;
    if (type == ESP_VIDEO_RENDER_PROC_CROP) {
        return (esp_gmf_element_handle_t)&p->crop_el;
    }
    if (type == ESP_VIDEO_RENDER_PROC_SCALE) {
        return (esp_gmf_element_handle_t)&p->scale_el;
    }
    return NULL;
}

esp_video_render_err_t video_render_proc_get_out_info(video_render_proc_handle_t proc, esp_video_render_frame_info_t *info)
{
    if (!proc || !info) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    emu_proc_t *p = (emu_proc_t *)proc;
    *info = p->out;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_proc_set_writer(video_render_proc_handle_t proc, esp_video_render_write_cb_t writer, void *ctx)
{
    if (!proc || !writer) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    emu_proc_t *p = (emu_proc_t *)proc;
    p->writer = writer;
    p->writer_ctx = ctx;
    p->out_acquire = NULL;
    p->out_release = NULL;
    p->out_ctx = NULL;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_proc_set_out_port(video_render_proc_handle_t handle,
                                                      port_acquire acquire,
                                                      port_release release,
                                                      void *ctx)
{
    if (!handle || !acquire || !release) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    emu_proc_t *p = (emu_proc_t *)handle;
    p->out_acquire = acquire;
    p->out_release = release;
    p->out_ctx = ctx;
    p->writer = NULL;
    p->writer_ctx = NULL;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_proc_write(video_render_proc_handle_t proc, esp_video_render_frame_t *frame)
{
    if (!proc || !frame) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    emu_proc_t *p = (emu_proc_t *)proc;
    if (!p->opened || (!p->writer && !(p->out_acquire && p->out_release))) {
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }

    // Pass-through: assume `frame` matches `out` format/size already.
    if (p->writer) {
        (void)p->writer(frame, p->writer_ctx);
        return ESP_VIDEO_RENDER_ERR_OK;
    }

    esp_gmf_payload_t payload = {0};
    if (p->out_acquire(p->out_ctx, &payload, frame->size, 0) != ESP_GMF_IO_OK) {
        return ESP_VIDEO_RENDER_ERR_NO_RESOURCE;
    }
    if (payload.buf == NULL) {
        payload.buf = frame->data;
        payload.buf_length = frame->size;
    }
    if (payload.buf != frame->data) {
        if (payload.buf_length < frame->size) {
            (void)p->out_release(p->out_ctx, &payload, 0);
            return ESP_VIDEO_RENDER_ERR_FAIL;
        }
        memcpy(payload.buf, frame->data, frame->size);
    }
    payload.valid_size = frame->size;
    if (p->out_release(p->out_ctx, &payload, 0) != ESP_GMF_IO_OK) {
        return ESP_VIDEO_RENDER_ERR_FAIL;
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t video_render_proc_close(video_render_proc_handle_t proc)
{
    if (!proc) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    emu_proc_t *p = (emu_proc_t *)proc;
    p->opened = false;
    return ESP_VIDEO_RENDER_ERR_OK;
}

void video_render_proc_destroy(video_render_proc_handle_t proc)
{
    if (!proc) {
        return;
    }
    video_render_free(proc);
}

esp_video_render_err_t video_render_proc_get_out_frame_info(video_render_proc_handle_t proc, esp_video_render_frame_info_t *info)
{
    return video_render_proc_get_out_info(proc, info);
}

int esp_gmf_video_param_set_cropped_region(esp_gmf_element_handle_t element, const esp_gmf_video_rgn_t *region)
{
    if (!element || !region) {
        return -1;
    }
    emu_gmf_video_element_t *el = (emu_gmf_video_element_t *)element;
    if (el->kind != EMU_GMF_VIDEO_EL_CROP) {
        return -1;
    }
    el->crop = *region;
    el->crop_dirty = true;
    return 0;
}

int esp_gmf_video_param_set_dst_resolution(esp_gmf_element_handle_t element, const esp_gmf_video_resolution_t *res)
{
    if (!element || !res) {
        return -1;
    }
    emu_gmf_video_element_t *el = (emu_gmf_video_element_t *)element;
    if (el->kind != EMU_GMF_VIDEO_EL_SCALE) {
        return -1;
    }
    el->dst = *res;
    el->dst_dirty = true;
    return 0;
}

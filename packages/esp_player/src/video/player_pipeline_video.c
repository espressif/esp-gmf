/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "player_pipeline.h"
#include "player_internal.h"

#include "esp_gmf_video_dec.h"
#include "esp_gmf_video_element.h"
#include "esp_video_dec_h264.h"
#include "esp_video_codec_types.h"
#include "esp_video_codec_utils.h"
#include "esp_video_render.h"
#include "esp_gmf_info.h"
#include "esp_gmf_obj.h"
#include "esp_fourcc.h"

#include "player_video_render.h"
#include "player_submit_frame.h"
#include "player_data_bus.h"
#include "player_ports.h"
#include "player_pipe_events.h"

/** Minimum frame buffer estimate (320x240 YUV420) for decoder / render port sizing */
#define VIDEO_MIN_FRAME_SIZE  ((uint32_t)(320 * 240 * 1.5))

/* JPEG/H.264 code in 16x16 macroblocks, so decoders emit padded dimensions. */
#define VIDEO_DIM_ALIGN        (16U)
#define VIDEO_ALIGN_UP_DIM(v)  (((uint32_t)(v) + (VIDEO_DIM_ALIGN - 1U)) & ~(uint32_t)(VIDEO_DIM_ALIGN - 1U))

static const char *TAG = "ESP_PLAYER_PIPELINE";

static bool player_pl_fmt_supported(const uint32_t *fmts, uint8_t n, uint32_t fmt)
{
    for (uint8_t i = 0; i < n; i++) {
        if (fmts[i] == fmt) {
            return true;
        }
    }
    return false;
}

static uint32_t player_pl_pick_video_dst_format(esp_gmf_element_handle_t decoder_el,
                                                uint32_t src_format,
                                                uint32_t preferred)
{
    const uint32_t fallback = ESP_VIDEO_CODEC_PIXEL_FMT_RGB565_LE;
    const uint32_t *fmts = NULL;
    uint8_t n = 0;
    if (esp_gmf_video_dec_get_dst_formats(decoder_el, src_format, &fmts, &n) != ESP_GMF_ERR_OK
        || fmts == NULL || n == 0) {
        return fallback;
    }
    if (preferred != 0 && player_pl_fmt_supported(fmts, n, preferred)) {
        return preferred;
    }
    if (player_pl_fmt_supported(fmts, n, fallback)) {
        return fallback;
    }
    if (player_pl_fmt_supported(fmts, n, ESP_VIDEO_CODEC_PIXEL_FMT_RGB888)) {
        return ESP_VIDEO_CODEC_PIXEL_FMT_RGB888;
    }
    return fmts[0];
}

static esp_gmf_err_t player_video_decoder_report_stream_info(esp_player_stream_t *stream)
{
    const esp_player_video_stream_info_t *src = &stream->video_side->track_info.video_info;
    if (src->format == ESP_PLAYER_FORMAT_NONE) {
        ESP_LOGE(TAG, "Decoder reopen requires source video codec info");
        return ESP_GMF_ERR_FAIL;
    }
    esp_gmf_info_video_t info = {
        .format_id = (uint32_t)src->format,
        .width = src->width,
        .height = src->height,
        .fps = src->fps,
        .bitrate = src->bitrate,
    };
    return esp_gmf_pipeline_report_info(stream->video_side->decoder, ESP_GMF_INFO_VIDEO, &info, (int)sizeof(info));
}

static void player_get_video_visible_size(esp_player_stream_t *stream, uint16_t *out_w, uint16_t *out_h)
{
    uint16_t w = stream->video_side->track_info.video_info.width;
    uint16_t h = stream->video_side->track_info.video_info.height;
    if (stream->extractor) {
        int8_t vid_idx = -1;
        esp_gmf_element_handle_t ext_el = player_extractor_el(stream);
        if (player_extractor_track_active(ext_el, ESP_EXTRACTOR_STREAM_TYPE_VIDEO, &vid_idx) == ESP_GMF_ERR_OK && vid_idx >= 0) {
            esp_extractor_stream_info_t extractor_info = {0};
            if (player_extractor_get_stream_info(ext_el, ESP_EXTRACTOR_STREAM_TYPE_VIDEO, vid_idx, &extractor_info) == ESP_GMF_ERR_OK) {
                w = extractor_info.video_info.width;
                h = extractor_info.video_info.height;
            }
        }
    }
    *out_w = w;
    *out_h = h;
}

esp_player_err_t player_pl_try_custom_decoder_video(esp_player_stream_t *stream,
                                                    esp_gmf_element_handle_t *out_el, bool *is_custom)
{
    esp_player_custom_elements_t *ce = stream->custom_elements;
    if (ce == NULL) {
        return ESP_PLAYER_ERR_OK;
    }
    if (ce->vdec_factory == NULL) {
        return ESP_PLAYER_ERR_OK;
    }
    uint32_t cc = (uint32_t)stream->video_side->track_info.video_info.format;
    const char *expected_tag = VIDEO_DECODER_TAG;
    esp_gmf_element_handle_t el = NULL;
    esp_player_err_t r = ce->vdec_factory(ce->user_ctx, cc, &stream->video_side->track_info.video_info, &el);
    if (r == ESP_PLAYER_ERR_NOT_SUPPORT) {
        ESP_LOGI(TAG, "Custom video decoder declined codec 0x%08x, falling back to built-in", (unsigned)cc);
        return ESP_PLAYER_ERR_OK;
    }
    if (r != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(TAG, "Custom video decoder factory failed for codec 0x%08x (%d)", (unsigned)cc, r);
        return r;
    }
    if (el == NULL) {
        ESP_LOGE(TAG, "Custom video decoder factory returned OK but element is NULL");
        return ESP_PLAYER_ERR_FAIL;
    }
    char *tag = NULL;
    if (esp_gmf_obj_get_tag(el, &tag) != ESP_GMF_ERR_OK || tag == NULL || strcmp(tag, expected_tag) != 0) {
        ESP_LOGE(TAG, "Custom video decoder element tag '%s' mismatch; expected '%s'", tag ? tag : "(null)", expected_tag);
        esp_gmf_obj_delete(el);
        return ESP_PLAYER_ERR_FAIL;
    }
    *out_el = el;
    *is_custom = true;
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_pl_install_builtin_video_decoder(esp_player_stream_t *stream,
                                                         esp_gmf_element_handle_t *decoder_el)
{
    uint32_t src_format = (uint32_t)stream->video_side->track_info.video_info.format;
    esp_gmf_video_dec_cfg_t video_dec_cfg = {0};
    if (src_format == ESP_FOURCC_H264) {
        video_dec_cfg.codec_cc = ESP_VIDEO_DEC_SW_H264_TAG;
    }
    if (esp_gmf_video_dec_init(&video_dec_cfg, decoder_el) != ESP_GMF_ERR_OK) {
        return ESP_PLAYER_ERR_NO_MEM;
    }
    uint32_t preferred = 0;
    if (src_format == ESP_FOURCC_H264) {
        preferred = ESP_VIDEO_CODEC_PIXEL_FMT_YUV420P;
    } else if (stream->video_render_hd != NULL) {
        esp_video_render_disp_info_t disp = {0};
        if (esp_video_render_get_display_info(
                (esp_video_render_handle_t)stream->video_render_hd, &disp) == ESP_VIDEO_RENDER_ERR_OK &&
            disp.format != ESP_VIDEO_RENDER_FORMAT_NONE) {
            preferred = (uint32_t)disp.format;
        }
    }
    uint32_t dst = player_pl_pick_video_dst_format(*decoder_el, src_format, preferred);
    ESP_LOGI(TAG, "Video decode dst format 0x%08" PRIx32, dst);
    esp_gmf_video_dec_set_dst_format(*decoder_el, dst);
    esp_gmf_video_element_t *v_el = (esp_gmf_video_element_t *)*decoder_el;
    v_el->src_info.format_id = src_format;
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_pl_queues_init_video(esp_player_stream_t *stream, uint32_t queue_size)
{
    if (stream->video_side->frame_queue != NULL) {
        player_frame_queue_reset(stream, stream->video_side->frame_queue,
                                 &stream->video_side->read_node);
        player_reset_video_db(stream);
    } else {
        uint32_t data_queue_size = player_frame_queue_size(stream, false, queue_size);
        if (player_frame_queue_create(data_queue_size, &stream->video_side->frame_queue)
            != ESP_PLAYER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to create video frame queue, size: %" PRIu32, data_queue_size);
            return ESP_PLAYER_ERR_FAIL;
        }
    }
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_pl_run_create_video_decoder(esp_player_stream_t *stream)
{
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    esp_gmf_port_handle_t decoder_outport = NULL;
    esp_gmf_port_handle_t decoder_inport = NULL;
    esp_player_err_t ret = ESP_PLAYER_ERR_OK;

    if (stream->video_side->decoder == NULL) {
        if (xSemaphoreTake(stream->lock_resource, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to acquire lock_resource, timeout");
            return ESP_PLAYER_ERR_TIMEOUT;
        }

        bool bus_owned_locally = false;
        ret = ESP_PLAYER_ERR_OK;
        do {
            decoder_inport = NEW_ESP_GMF_PORT_IN_BLOCK(decoder_video_in_acquire, decoder_video_in_release, NULL, stream, 0, ESP_GMF_MAX_DELAY);
            if (decoder_inport == NULL) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_VIDEO_DECODER, "in-port alloc");
                ret = ESP_PLAYER_ERR_NO_MEM;
                break;
            }
            /* Decode-output block is sized from the decoder's first reported frame
             * (see VIDEO_INFO handler / acquire_write), not extractor or set_track_info. */
            player_data_bus_release(&stream->video_side->data_bus);
            stream->video_side->data_bus = player_data_bus_create(NULL, ESP_PLAYER_VIDEO_BLOCK_NUM * 2);
            if (stream->video_side->data_bus == NULL) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_VIDEO_DECODER, "data_bus_create");
                ret = ESP_PLAYER_ERR_NO_MEM;
                break;
            }
            bus_owned_locally = true;
            if (player_data_bus_set_lazy_block(stream->video_side->data_bus, ESP_PLAYER_VIDEO_BLOCK_NUM) != ESP_GMF_ERR_OK) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_VIDEO_DECODER, "data_bus_lazy_block");
                ret = ESP_PLAYER_ERR_NO_MEM;
                break;
            }
            decoder_outport = NEW_ESP_GMF_PORT_OUT_BLOCK(player_data_bus_acquire_write, player_data_bus_release_write, NULL,
                                                         (void *)stream->video_side->data_bus, 0, ESP_GMF_MAX_DELAY);
            if (decoder_outport == NULL) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_VIDEO_DECODER, "out-port alloc");
                ret = ESP_PLAYER_ERR_NO_MEM;
                break;
            }
            player_cfg_fill_video_decoder_task(stream, &cfg);
            esp_gmf_task_handle_t task_hd = NULL;
            ret = player_config_decoder_pipeline(stream, false, &stream->video_side->decoder, decoder_inport, decoder_outport,
                                                 _video_decoder_pipe_event_handler, &cfg, &task_hd);
            if (ret != ESP_PLAYER_ERR_OK) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_VIDEO_DECODER, "config_decoder_pipeline");
                break;
            }
            bus_owned_locally = false;
        } while (0);

        xSemaphoreGive(stream->lock_resource);

        if (ret != ESP_PLAYER_ERR_OK) {
            if (bus_owned_locally) {
                player_data_bus_release(&stream->video_side->data_bus);
            }
            return ret;
        }
        if (!(stream->expected_tasks & TASK_STATUS_EXTRACTOR_RUNNING) &&
            stream->video_side->track_info.video_info.fps > 0) {
            player_sync_set_video_fps(stream->sync_handle, stream->video_side->track_info.video_info.fps);
            player_sync_enable_video_fps_sync(stream->sync_handle, true);
        } else {
            player_sync_enable_video_fps_sync(stream->sync_handle, false);
        }
    } else {
        esp_gmf_pipeline_reset(stream->video_side->decoder);
        if (player_video_decoder_report_stream_info(stream) != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "video_decoder: esp_gmf_pipeline_report_info(VIDEO) failed after reset");
            player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_VIDEO_DECODER, "report_stream_info after reset");
            return ESP_PLAYER_ERR_FAIL;
        }
    }
    esp_gmf_task_handle_t vid_dec_task = player_pipeline_task(stream->video_side->decoder);
    esp_player_err_t run_ret = player_run_pipeline_with_timeout(stream, vid_dec_task, TASK_TIMEOUT_MS, stream->video_side->decoder,
                                                                ESP_PLAYER_ERROR_SOURCE_VIDEO_DECODER);
    if (run_ret != ESP_PLAYER_ERR_OK) {
        return run_ret;
    }
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_pl_create_video_render(esp_player_stream_t *stream)
{
    ESP_LOGI(TAG, "Starting video renderer");
    if (stream->_is_stop) {
        return ESP_PLAYER_ERR_OK;
    }
    if (stream->video_side == NULL) {
        ESP_LOGE(TAG, "Cannot create video renderer: video side not allocated (mask has no VIDEO)");
        return ESP_PLAYER_ERR_FAIL;
    }
    if (stream->video_side->render == NULL) {
        if (xSemaphoreTake(stream->lock_resource, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to acquire lock_resource, timeout");
            return ESP_PLAYER_ERR_TIMEOUT;
        }

        esp_player_err_t ret = ESP_PLAYER_ERR_OK;
        esp_gmf_port_handle_t in_port = NULL;
        bool in_port_registered = false;
        do {
            if (esp_gmf_pipeline_create(&stream->video_side->render) != ESP_GMF_ERR_OK) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_VIDEO_RENDER, "pipeline_create");
                ret = ESP_PLAYER_ERR_NO_MEM;
                break;
            }
            player_video_render_config_t video_render_cfg = DEFAULT_PLAYER_VIDEO_RENDER_CONFIG();
            video_render_cfg.render_handle = stream->video_render_hd;
            video_render_cfg.width = stream->video_side->track_info.video_info.width;
            video_render_cfg.height = stream->video_side->track_info.video_info.height;
            player_get_video_visible_size(stream, &video_render_cfg.display_width, &video_render_cfg.display_height);
            video_render_cfg.fps = stream->video_side->track_info.video_info.fps;
            video_render_cfg.sync_handle = stream->sync_handle;
            video_render_cfg.decoded_format = stream->video_side->decoded_format;
            esp_gmf_element_handle_t video_render_el = NULL;
            if (player_video_render_init(&video_render_cfg, &video_render_el) != ESP_GMF_ERR_OK) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_VIDEO_RENDER, "render_init");
                ret = ESP_PLAYER_ERR_FAIL;
                break;
            }
            esp_gmf_pipeline_register_el(stream->video_side->render, video_render_el);
            uint32_t vid_port_size = 0;
            if (stream->video_side->data_bus) {
                vid_port_size = player_data_bus_block_item_size(stream->video_side->data_bus);
            }
            if (vid_port_size == 0) {
                vid_port_size = player_pl_video_decoded_frame_size(video_render_cfg.width, video_render_cfg.height,
                                                                   video_render_cfg.decoded_format);
            }
            if (vid_port_size == 0) {
                vid_port_size = VIDEO_MIN_FRAME_SIZE;
            }
            if (stream->video_side->data_bus) {
                in_port = NEW_ESP_GMF_PORT_IN_BLOCK(player_data_bus_acquire_read, player_data_bus_release_read, NULL,
                                                    (void *)stream->video_side->data_bus, vid_port_size, ESP_GMF_MAX_DELAY);
            } else {
                in_port = NEW_ESP_GMF_PORT_IN_BLOCK(esp_gmf_db_acquire_read, esp_gmf_db_release_read, NULL,
                                                    (void *)player_video_db(stream), vid_port_size, ESP_GMF_MAX_DELAY);
            }
            if (in_port == NULL) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_VIDEO_RENDER, "in-port alloc");
                ret = ESP_PLAYER_ERR_NO_MEM;
                break;
            }
            if (esp_gmf_element_register_in_port(video_render_el, in_port) != ESP_GMF_ERR_OK) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_VIDEO_RENDER, "register in-port");
                ret = ESP_PLAYER_ERR_FAIL;
                break;
            }
            in_port_registered = true;
            esp_gmf_pipeline_set_event(stream->video_side->render, _video_render_pipe_event_handler, stream);
            esp_gmf_task_cfg_t task_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
            player_cfg_fill_video_render_task(stream, &task_cfg);
            esp_gmf_task_handle_t task_hd = NULL;
            ret = player_pl_task_create(&task_cfg, stream->video_side->render, &task_hd);
            if (ret != ESP_PLAYER_ERR_OK) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_VIDEO_RENDER, "task_create");
                break;
            }
        } while (0);

        xSemaphoreGive(stream->lock_resource);

        if (ret != ESP_PLAYER_ERR_OK) {
            if (in_port && !in_port_registered) {
                esp_gmf_port_deinit(in_port);
            }
            if (stream->video_side->render) {
                esp_gmf_pipeline_destroy(stream->video_side->render);
                stream->video_side->render = NULL;
            }
            return ret;
        }
    } else {
        esp_gmf_pipeline_reset(stream->video_side->render);
    }
    return player_run_pipeline_with_timeout(stream, player_pipeline_task(stream->video_side->render), TASK_TIMEOUT_MS,
                                            stream->video_side->render, ESP_PLAYER_ERROR_SOURCE_VIDEO_RENDER);
}

uint32_t player_pl_video_decoded_frame_size(uint32_t width, uint32_t height, uint32_t format)
{
    if (width == 0 || height == 0) {
        return 0;
    }
    esp_video_codec_resolution_t res = {
        .width = VIDEO_ALIGN_UP_DIM(width),
        .height = VIDEO_ALIGN_UP_DIM(height),
    };
    return esp_video_codec_get_image_size((esp_video_codec_pixel_fmt_t)format, &res);
}

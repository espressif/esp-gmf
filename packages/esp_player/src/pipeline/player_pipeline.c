/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "sdkconfig.h"

#include "player_pipeline.h"
#include "player_internal.h"
#include "player_ports.h"
#include "player_pipe_events.h"
#include "player_extractor_io.h"
#include "player_submit_frame.h"

static const char *TAG = "ESP_PLAYER_PIPELINE";

#define EXTRACTOR_OUT_ALIGN  16

static esp_player_err_t player_try_custom_decoder(esp_player_stream_t *stream, bool is_audio,
                                                  esp_gmf_element_handle_t *out_el, bool *is_custom)
{
    if (stream->custom_elements == NULL) {
        return ESP_PLAYER_ERR_OK;
    }
    if (is_audio) {
        return player_pl_try_custom_decoder_audio(stream, out_el, is_custom);
    }
    return player_pl_try_custom_decoder_video(stream, out_el, is_custom);
}

esp_player_err_t player_config_decoder_pipeline(esp_player_stream_t *stream, bool is_audio,
                                                esp_gmf_pipeline_handle_t *decoder_pipe,
                                                esp_gmf_port_handle_t decoder_inport,
                                                esp_gmf_port_handle_t decoder_outport,
                                                player_pipe_event_handler_t event_cb,
                                                esp_gmf_task_cfg_t *cfg,
                                                esp_gmf_task_handle_t *task_hd)
{
    esp_player_err_t ret = ESP_PLAYER_ERR_OK;
    esp_gmf_element_handle_t decoder_el = NULL;
    bool is_custom = false;
    bool inport_registered = false;
    bool outport_registered = false;
    if (esp_gmf_pipeline_create(decoder_pipe) != ESP_GMF_ERR_OK) {
        ret = ESP_PLAYER_ERR_NO_MEM;
        goto fail;
    }
    esp_player_err_t cr = player_try_custom_decoder(stream, is_audio, &decoder_el, &is_custom);
    if (cr != ESP_PLAYER_ERR_OK) {
        ret = cr;
        goto fail;
    }
    if (is_audio) {
        if (!is_custom) {
            ret = player_pl_install_builtin_audio_decoder(stream, &decoder_el);
            if (ret != ESP_PLAYER_ERR_OK) {
                goto fail;
            }
        }
    } else {
        if (!is_custom) {
            ret = player_pl_install_builtin_video_decoder(stream, &decoder_el);
            if (ret != ESP_PLAYER_ERR_OK) {
                goto fail;
            }
        }
    }
    esp_gmf_pipeline_register_el(*decoder_pipe, decoder_el);
    if (stream->dec_frame_mode != ESP_PLAYER_DEC_FRAME_MODE_EXTRACTOR) {
        if (queues_init(stream, is_audio) != ESP_PLAYER_ERR_OK) {
            ret = ESP_PLAYER_ERR_FAIL;
            goto fail;
        }
    }
    if (esp_gmf_element_register_in_port(decoder_el, decoder_inport) != ESP_GMF_ERR_OK) {
        ret = ESP_PLAYER_ERR_FAIL;
        goto fail;
    }
    inport_registered = true;
    if (decoder_outport) {
        ESP_LOGD(TAG, "decoder_outport: %p\n", decoder_outport);
        if (esp_gmf_element_register_out_port(decoder_el, decoder_outport) != ESP_GMF_ERR_OK) {
            ret = ESP_PLAYER_ERR_FAIL;
            goto fail;
        }
        outport_registered = true;
    }
    esp_gmf_pipeline_set_event(*decoder_pipe, event_cb, stream);
    esp_gmf_element_set_state(decoder_el, ESP_GMF_EVENT_STATE_INITIALIZED);
    ret = player_pl_task_create(cfg, *decoder_pipe, task_hd);
    if (ret != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to create decoder task");
        goto fail;
    }
    return ret;

fail:
    if (*decoder_pipe) {
        esp_gmf_pipeline_destroy(*decoder_pipe);
        *decoder_pipe = NULL;
    }
    if (!inport_registered && decoder_inport) {
        esp_gmf_port_deinit(decoder_inport);
    }
    if (!outport_registered && decoder_outport) {
        esp_gmf_port_deinit(decoder_outport);
    }
    return ret;
}

esp_player_err_t queues_init(esp_player_stream_t *stream, bool is_audio)
{
    const uint32_t queue_size =
        (stream->dec_frame_mode == ESP_PLAYER_DEC_FRAME_MODE_BLOCK)  ? 1
        : (stream->dec_frame_mode == ESP_PLAYER_DEC_FRAME_MODE_FILL) ? ESP_PLAYER_FILL_POOL_SIZE
                                                                     : (is_audio ? ESP_PLAYER_AUDIO_QUEUE_SIZE
                                                                                 : ESP_PLAYER_VIDEO_QUEUE_SIZE);
    if (is_audio) {
        return player_pl_queues_init_audio(stream, queue_size);
    }
    return player_pl_queues_init_video(stream, queue_size);
}

esp_player_err_t player_create_extractor_pipeline(esp_player_stream_t *stream)
{
    if (stream->_is_stop) {
        return ESP_PLAYER_ERR_OK;
    }
    if (stream->extractor == NULL) {
        if (xSemaphoreTake(stream->lock_resource, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to acquire lock_resource");
            return ESP_PLAYER_ERR_TIMEOUT;
        }

        esp_player_err_t ret = ESP_PLAYER_ERR_OK;
        enum {
            EXTRACTOR_PORT_AUDIO = 0,
            EXTRACTOR_PORT_VIDEO = 1,
            EXTRACTOR_PORT_NUM = 2,
        };
        esp_gmf_port_handle_t extractor_ports[EXTRACTOR_PORT_NUM] = {NULL};
        bool extractor_port_registered[EXTRACTOR_PORT_NUM] = {false};
        do {
            if (esp_gmf_pipeline_create(&stream->extractor) != ESP_GMF_ERR_OK) {
                ESP_LOGE(TAG, "Failed to create extractor pipeline");
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_EXTRACTOR, "pipeline_create");
                ret = ESP_PLAYER_ERR_NO_MEM;
                break;
            }
            esp_extractor_config_t extractor_cfg = {
                .in_read_cb = _extractor_read,
                .in_seek_cb = _extractor_seek,
                .in_size_cb = _extractor_total_size,
                .in_ctx = stream,
                .out_pool_size = player_cfg_extractor_pool_size(stream, false),
                .out_align = EXTRACTOR_OUT_ALIGN,
                .type = player_current_format(stream),
                .extract_mask = stream->av_mask,
            };
            ESP_LOGD(TAG, "extractor_cfg.avmask %d", (int)stream->av_mask);
            if (stream->av_mask & ESP_PLAYER_MASK_VIDEO) {
                extractor_cfg.out_pool_size = player_cfg_extractor_pool_size(stream, true);
            }
            esp_gmf_element_handle_t extractor_el = NULL;
            if (player_extractor_init(&extractor_cfg, &extractor_el) != ESP_GMF_ERR_OK) {
                esp_gmf_pipeline_destroy(stream->extractor);
                stream->extractor = NULL;
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_EXTRACTOR, "extractor_init");
                ret = ESP_PLAYER_ERR_NO_MEM;
                break;
            }
            esp_gmf_pipeline_register_el(stream->extractor, extractor_el);
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
            /* Headerless raw source (e.g. PCM): hand URL-supplied parameters to the RAW extractor. */
            if (extractor_cfg.type == ESP_EXTRACTOR_TYPE_RAW && stream->audio_side) {
                esp_player_audio_stream_info_t *ai = &stream->audio_side->track_info.audio_info;
                player_extractor_set_raw_pcm_info(extractor_el, ai->sample_rate, ai->channels, ai->bits_per_sample);
            }
            extractor_ports[EXTRACTOR_PORT_AUDIO] = NEW_ESP_GMF_PORT_OUT_BLOCK(NULL, extractor_audio_out_release, NULL, stream, 0, ESP_GMF_MAX_DELAY);
            if (extractor_ports[EXTRACTOR_PORT_AUDIO] == NULL) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_EXTRACTOR, "audio out-port alloc");
                ret = ESP_PLAYER_ERR_NO_MEM;
                break;
            }
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */
#if CONFIG_ESP_PLAYER_ENABLE_VIDEO
            extractor_ports[EXTRACTOR_PORT_VIDEO] = NEW_ESP_GMF_PORT_OUT_BLOCK(NULL, extractor_video_out_release, NULL, stream, 0, ESP_GMF_MAX_DELAY);
            if (extractor_ports[EXTRACTOR_PORT_VIDEO] == NULL) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_EXTRACTOR, "video out-port alloc");
                ret = ESP_PLAYER_ERR_NO_MEM;
                break;
            }
#endif  /* CONFIG_ESP_PLAYER_ENABLE_VIDEO */
            int register_order[EXTRACTOR_PORT_NUM];
            int register_count = 0;
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO && CONFIG_ESP_PLAYER_ENABLE_VIDEO
            /* Dual-port: video-only playback needs video at out head; AV uses audio then video (see player_extractor.c). */
            if (stream->av_mask == ESP_PLAYER_MASK_VIDEO) {
                register_order[register_count++] = EXTRACTOR_PORT_VIDEO;
                register_order[register_count++] = EXTRACTOR_PORT_AUDIO;
            } else {
                register_order[register_count++] = EXTRACTOR_PORT_AUDIO;
                register_order[register_count++] = EXTRACTOR_PORT_VIDEO;
            }
#elif CONFIG_ESP_PLAYER_ENABLE_AUDIO
            register_order[register_count++] = EXTRACTOR_PORT_AUDIO;
#elif CONFIG_ESP_PLAYER_ENABLE_VIDEO
            register_order[register_count++] = EXTRACTOR_PORT_VIDEO;
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO && CONFIG_ESP_PLAYER_ENABLE_VIDEO */
            for (int i = 0; i < register_count; i++) {
                int port_idx = register_order[i];
                if (extractor_ports[port_idx] == NULL) {
                    continue;
                }
                if (esp_gmf_element_register_out_port(extractor_el, extractor_ports[port_idx]) != ESP_GMF_ERR_OK) {
                    ESP_LOGE(TAG, "Failed to register extractor %s output port",
                             (port_idx == EXTRACTOR_PORT_AUDIO) ? "audio" : "video");
                    player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_EXTRACTOR,
                                              (port_idx == EXTRACTOR_PORT_AUDIO) ? "register audio out-port" : "register video out-port");
                    ret = ESP_PLAYER_ERR_FAIL;
                    break;
                }
                extractor_port_registered[port_idx] = true;
            }
            if (ret != ESP_PLAYER_ERR_OK) {
                break;
            }
            esp_gmf_pipeline_set_event(stream->extractor, _extractor_pipe_event_handler, stream);
            esp_gmf_task_cfg_t task_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
            player_cfg_fill_extractor_task(stream, &task_cfg);
            esp_gmf_task_handle_t task_hd = NULL;
            ret = player_pl_task_create(&task_cfg, stream->extractor, &task_hd);
            if (ret != ESP_PLAYER_ERR_OK) {
                ESP_LOGE(TAG, "Failed to create extractor task");
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_EXTRACTOR, "task_create");
                break;
            }
        } while (0);

        xSemaphoreGive(stream->lock_resource);

        if (ret != ESP_PLAYER_ERR_OK) {
            for (int i = 0; i < EXTRACTOR_PORT_NUM; i++) {
                if (extractor_ports[i] && !extractor_port_registered[i]) {
                    esp_gmf_port_deinit(extractor_ports[i]);
                }
            }
            if (stream->extractor) {
                esp_gmf_pipeline_destroy(stream->extractor);
                stream->extractor = NULL;
            }
            return ret;
        }
    } else {
        esp_gmf_pipeline_reset(stream->extractor);
        player_extractor_seek(player_extractor_el(stream), player_sync_get_seek_target(stream->sync_handle));
    }
    esp_gmf_task_handle_t ext_task = player_pipeline_task(stream->extractor);
    return player_run_pipeline_with_timeout(stream, ext_task, TASK_TIMEOUT_MS, stream->extractor, ESP_PLAYER_ERROR_SOURCE_EXTRACTOR);
}

esp_player_err_t player_create_decoder_pipeline(esp_player_stream_t *stream, bool is_audio)
{
    if (stream->_is_stop) {
        return ESP_PLAYER_ERR_OK;
    }
    if (is_audio && stream->audio_side == NULL) {
        ESP_LOGE(TAG, "Cannot create audio decoder: audio side not allocated");
        return ESP_PLAYER_ERR_FAIL;
    }
    if (!is_audio && stream->video_side == NULL) {
        ESP_LOGE(TAG, "Cannot create video decoder: video side not allocated");
        return ESP_PLAYER_ERR_FAIL;
    }
    if (is_audio) {
        return player_pl_run_create_audio_decoder(stream);
    }
    return player_pl_run_create_video_decoder(stream);
}

esp_player_err_t player_create_render_pipeline(esp_player_stream_t *stream, bool is_audio)
{
    if (is_audio) {
        return player_pl_create_audio_render(stream);
    }
    return player_pl_create_video_render(stream);
}

esp_player_err_t player_pl_task_create(esp_gmf_task_cfg_t *cfg, esp_gmf_pipeline_handle_t pipe,
                                       esp_gmf_task_handle_t *task_hd)
{
    if (cfg == NULL || pipe == NULL || task_hd == NULL) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    *task_hd = NULL;
    if (esp_gmf_task_init(cfg, task_hd) != ESP_GMF_ERR_OK) {
        return ESP_PLAYER_ERR_FAIL;
    }
    if (esp_gmf_pipeline_bind_task(pipe, *task_hd) != ESP_GMF_ERR_OK) {
        esp_gmf_task_deinit(*task_hd);
        *task_hd = NULL;
        return ESP_PLAYER_ERR_FAIL;
    }
    return ESP_PLAYER_ERR_OK;
}

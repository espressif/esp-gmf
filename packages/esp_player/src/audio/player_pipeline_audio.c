/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "player_pipeline.h"
#include "player_internal.h"

#include "esp_gmf_audio_dec.h"
#include "esp_gmf_obj.h"

#include "player_audio_render.h"
#include "player_data_bus.h"
#include "player_ports.h"
#include "player_pipe_events.h"

static const char *TAG = "ESP_PLAYER_PIPELINE";

#define AUDIO_RB_FLAC_MAX_SIZE  (40 * 1024)
#define AUDIO_RINGBUF_SIZE_MIN  (2048)
#define AUDIO_RINGBUF_SIZE_MAX  (8192)

static inline uint32_t player_audio_ringbuf_size(uint32_t sample_rate, uint8_t channels,
                                                 uint8_t bits_per_sample, esp_player_format_t fmt)
{
    uint32_t sz = (uint32_t)((uint64_t)sample_rate * channels * (bits_per_sample / 8U)
                             * ESP_PLAYER_AUDIO_RB_TIME_MS / 1000ULL);
    if (sz < AUDIO_RINGBUF_SIZE_MIN) {
        sz = AUDIO_RINGBUF_SIZE_MIN;
    }
    if (sz > AUDIO_RINGBUF_SIZE_MAX) {
        sz = AUDIO_RINGBUF_SIZE_MAX;
    }
    if (fmt == ESP_FOURCC_FLAC && sz < AUDIO_RB_FLAC_MAX_SIZE) {
        sz = AUDIO_RB_FLAC_MAX_SIZE;
    }
    return sz;
}

static esp_player_err_t player_get_audio_dec_cfg(esp_player_stream_t *stream)
{
    esp_audio_simple_dec_type_t dec_type = (esp_audio_simple_dec_type_t)stream->audio_side->track_info.audio_info.format;
    esp_player_format_t fmt_fcc = (esp_player_format_t)(uint32_t)stream->audio_side->track_info.audio_info.format;

    bool need_prepare = (stream->dec_cfg.dec_type != dec_type);
    if (!need_prepare && !is_simple_format_type(fmt_fcc) && stream->dec_cfg.dec_cfg == NULL) {
        need_prepare = true;
    }
    if (need_prepare) {
        if (player_prepare_dec_cfg(stream, fmt_fcc) != ESP_PLAYER_ERR_OK) {
            return ESP_PLAYER_ERR_FAIL;
        }
    }
    switch (dec_type) {
        case ESP_AUDIO_SIMPLE_DEC_TYPE_MP3:
        case ESP_AUDIO_SIMPLE_DEC_TYPE_AMRWB:
        case ESP_AUDIO_SIMPLE_DEC_TYPE_AMRNB:
        case ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC:
        case ESP_AUDIO_SIMPLE_DEC_TYPE_WAV:
        case ESP_AUDIO_SIMPLE_DEC_TYPE_M4A:
        case ESP_AUDIO_SIMPLE_DEC_TYPE_TS:
            break;
        case ESP_AUDIO_SIMPLE_DEC_TYPE_AAC: {
            esp_aac_dec_cfg_t *aac_cfg = (esp_aac_dec_cfg_t *)stream->dec_cfg.dec_cfg;
            aac_cfg->sample_rate = stream->audio_side->track_info.audio_info.sample_rate;
            aac_cfg->channel = stream->audio_side->track_info.audio_info.channels;
            aac_cfg->bits_per_sample = stream->audio_side->track_info.audio_info.bits_per_sample;
        } break;
        case ESP_AUDIO_SIMPLE_DEC_TYPE_RAW_OPUS: {
            esp_opus_dec_cfg_t *opus_cfg = (esp_opus_dec_cfg_t *)stream->dec_cfg.dec_cfg;
            opus_cfg->channel = stream->audio_side->track_info.audio_info.channels;
            opus_cfg->sample_rate = stream->audio_side->track_info.audio_info.sample_rate;
        } break;
        case ESP_AUDIO_SIMPLE_DEC_TYPE_PCM: {
            esp_pcm_dec_cfg_t *pcm_cfg = (esp_pcm_dec_cfg_t *)stream->dec_cfg.dec_cfg;
            pcm_cfg->sample_rate = stream->audio_side->track_info.audio_info.sample_rate;
            pcm_cfg->channel = stream->audio_side->track_info.audio_info.channels;
            pcm_cfg->bits_per_sample = stream->audio_side->track_info.audio_info.bits_per_sample;
        } break;
        case ESP_AUDIO_SIMPLE_DEC_TYPE_G711A:
        case ESP_AUDIO_SIMPLE_DEC_TYPE_G711U: {
            esp_g711_dec_cfg_t *g711_cfg = (esp_g711_dec_cfg_t *)stream->dec_cfg.dec_cfg;
            g711_cfg->channel = stream->audio_side->track_info.audio_info.channels;
        } break;
        case ESP_AUDIO_SIMPLE_DEC_TYPE_ADPCM: {
            esp_adpcm_dec_cfg_t *adpcm_cfg = (esp_adpcm_dec_cfg_t *)stream->dec_cfg.dec_cfg;
            adpcm_cfg->channel = stream->audio_side->track_info.audio_info.channels;
            adpcm_cfg->sample_rate = stream->audio_side->track_info.audio_info.sample_rate;
        } break;
        case ESP_AUDIO_SIMPLE_DEC_TYPE_SBC: {
            esp_sbc_dec_cfg_t *sbc_cfg = (esp_sbc_dec_cfg_t *)stream->dec_cfg.dec_cfg;
            sbc_cfg->ch_num = stream->audio_side->track_info.audio_info.channels;
        } break;
        case ESP_AUDIO_SIMPLE_DEC_TYPE_LC3: {
            esp_lc3_dec_cfg_t *lc3_cfg = (esp_lc3_dec_cfg_t *)stream->dec_cfg.dec_cfg;
            lc3_cfg->channel = stream->audio_side->track_info.audio_info.channels;
            lc3_cfg->sample_rate = stream->audio_side->track_info.audio_info.sample_rate;
            lc3_cfg->bits_per_sample = stream->audio_side->track_info.audio_info.bits_per_sample;
        } break;
        case ESP_AUDIO_SIMPLE_DEC_TYPE_ALAC: {
            esp_alac_dec_cfg_t *alac_cfg = (esp_alac_dec_cfg_t *)stream->dec_cfg.dec_cfg;
            alac_cfg->codec_spec_info = stream->audio_side->track_info.audio_info.spec_info;
            alac_cfg->spec_info_len = stream->audio_side->track_info.audio_info.spec_info_len;
            if (alac_cfg->codec_spec_info == NULL || alac_cfg->spec_info_len == 0) {
                ESP_LOGE(TAG, "ALAC spec info is invalid, spec_info=%p, spec_info_len=%u",
                         alac_cfg->codec_spec_info, (unsigned int)alac_cfg->spec_info_len);
                return ESP_PLAYER_ERR_FAIL;
            }
        } break;
        case ESP_AUDIO_SIMPLE_DEC_TYPE_VORBIS: {
            esp_vorbis_dec_cfg_t *vorbis_cfg = (esp_vorbis_dec_cfg_t *)stream->dec_cfg.dec_cfg;
            esp_vorbis_dec_cfg_t *vorbis_cfg_spec = (esp_vorbis_dec_cfg_t *)stream->audio_side->track_info.audio_info.spec_info;
            uint32_t spec_info_len = vorbis_cfg_spec->info_size + vorbis_cfg_spec->setup_size + sizeof(esp_vorbis_dec_cfg_t);
            if (stream->audio_side->track_info.audio_info.spec_info_len != spec_info_len) {
                ESP_LOGE(TAG, "VORBIS spec info len error, expected: %u, got: %u", (unsigned int)spec_info_len,
                         (unsigned int)stream->audio_side->track_info.audio_info.spec_info_len);
                return ESP_PLAYER_ERR_FAIL;
            }
            memcpy(vorbis_cfg, vorbis_cfg_spec, sizeof(esp_vorbis_dec_cfg_t));
        } break;
        default:
            break;
    }
    stream->dec_cfg.use_frame_dec = true;
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_pl_try_custom_decoder_audio(esp_player_stream_t *stream,
                                                    esp_gmf_element_handle_t *out_el, bool *is_custom)
{
    esp_player_custom_elements_t *ce = stream->custom_elements;
    if (ce == NULL) {
        return ESP_PLAYER_ERR_OK;
    }
    if (ce->adec_factory == NULL) {
        return ESP_PLAYER_ERR_OK;
    }
    uint32_t cc = (uint32_t)stream->audio_side->track_info.audio_info.format;
    const char *expected_tag = AUDIO_DECODER_TAG;
    esp_gmf_element_handle_t el = NULL;
    esp_player_err_t r = ce->adec_factory(ce->user_ctx, cc, &stream->audio_side->track_info.audio_info, &el);
    if (r == ESP_PLAYER_ERR_NOT_SUPPORT) {
        ESP_LOGI(TAG, "Custom audio decoder declined codec 0x%08x, falling back to built-in", (unsigned)cc);
        return ESP_PLAYER_ERR_OK;
    }
    if (r != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(TAG, "Custom audio decoder factory failed for codec 0x%08x (%d)", (unsigned)cc, r);
        return r;
    }
    if (el == NULL) {
        ESP_LOGE(TAG, "Custom audio decoder factory returned OK but element is NULL");
        return ESP_PLAYER_ERR_FAIL;
    }
    char *tag = NULL;
    if (esp_gmf_obj_get_tag(el, &tag) != ESP_GMF_ERR_OK || tag == NULL || strcmp(tag, expected_tag) != 0) {
        ESP_LOGE(TAG, "Custom audio decoder element tag '%s' mismatch; expected '%s'", tag ? tag : "(null)", expected_tag);
        esp_gmf_obj_delete(el);
        return ESP_PLAYER_ERR_FAIL;
    }
    *out_el = el;
    *is_custom = true;
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_pl_install_builtin_audio_decoder(esp_player_stream_t *stream,
                                                         esp_gmf_element_handle_t *decoder_el)
{
    if (player_get_audio_dec_cfg(stream) != ESP_PLAYER_ERR_OK) {
        return ESP_PLAYER_ERR_FAIL;
    }
    if (esp_gmf_audio_dec_init(&stream->dec_cfg, decoder_el) != ESP_GMF_ERR_OK) {
        return ESP_PLAYER_ERR_NO_MEM;
    }
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_pl_queues_init_audio(esp_player_stream_t *stream, uint32_t queue_size)
{
    if (stream->audio_side->extractor_queue != NULL) {
        player_drop_single_queue(stream, stream->audio_side->extractor_queue);
        player_reset_audio_db(stream);
    } else {
        stream->audio_side->extractor_queue = xQueueCreate(queue_size, sizeof(esp_gmf_payload_t));
        if (stream->audio_side->extractor_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create extractor audio queue");
            return ESP_PLAYER_ERR_FAIL;
        }
    }
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_pl_run_create_audio_decoder(esp_player_stream_t *stream)
{
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    esp_gmf_port_handle_t decoder_outport = NULL;
    esp_gmf_port_handle_t decoder_inport = NULL;
    esp_player_err_t ret = ESP_PLAYER_ERR_OK;

    if (stream->audio_side->decoder == NULL) {
        if (xSemaphoreTake(stream->lock_resource, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to acquire lock_resource, timeout");
            return ESP_PLAYER_ERR_TIMEOUT;
        }

        esp_gmf_db_handle_t aud_db = NULL;
        bool db_owned_locally = false;
        ret = ESP_PLAYER_ERR_OK;
        do {
            decoder_inport = NEW_ESP_GMF_PORT_IN_BLOCK(decoder_audio_in_acquire, decoder_audio_in_release, NULL, stream, 0, ESP_GMF_MAX_DELAY);
            if (decoder_inport == NULL) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_AUDIO_DECODER, "in-port alloc");
                ret = ESP_PLAYER_ERR_NO_MEM;
                break;
            }
            uint32_t aud_rb_size = player_audio_ringbuf_size(
                stream->audio_side->track_info.audio_info.sample_rate,
                stream->audio_side->track_info.audio_info.channels,
                stream->audio_side->track_info.audio_info.bits_per_sample,
                stream->audio_side->track_info.audio_info.format);
            if (esp_gmf_db_new_ringbuf(aud_rb_size, 1, &aud_db) != ESP_GMF_ERR_OK) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_AUDIO_DECODER, "db_new_ringbuf");
                ret = ESP_PLAYER_ERR_NO_MEM;
                break;
            }
            db_owned_locally = true;
            if (stream->audio_side->data_bus) {
                player_data_bus_destroy(stream->audio_side->data_bus);
                stream->audio_side->data_bus = NULL;
            }
            stream->audio_side->data_bus = player_data_bus_create(aud_db, 512);
            if (stream->audio_side->data_bus) {
                decoder_outport = NEW_ESP_GMF_PORT_OUT_BYTE(player_data_bus_acquire_write, player_data_bus_release_write, NULL,
                                                            (void *)stream->audio_side->data_bus, 0, ESP_GMF_MAX_DELAY);
            } else {
                decoder_outport = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, NULL,
                                                            (void *)aud_db, 0, ESP_GMF_MAX_DELAY);
            }
            if (decoder_outport == NULL) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_AUDIO_DECODER, "out-port alloc");
                ret = ESP_PLAYER_ERR_NO_MEM;
                break;
            }
            player_cfg_fill_audio_decoder_task(stream, &cfg);
            if (stream->audio_side->track_info.audio_info.format == ESP_FOURCC_OPUS ||
                stream->audio_side->track_info.audio_info.format == ESP_FOURCC_FLAC) {
                if (cfg.thread.stack <= 30 * 1024) {
                    cfg.thread.stack = 30 * 1024;
                }
            }
            esp_gmf_task_handle_t task_hd = NULL;
            ret = player_config_decoder_pipeline(stream, true, &stream->audio_side->decoder, decoder_inport, decoder_outport,
                                                 _audio_decoder_pipe_event_handler, &cfg, &task_hd);
            if (ret != ESP_PLAYER_ERR_OK) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_AUDIO_DECODER, "config_decoder_pipeline");
                break;
            }
            db_owned_locally = false;
        } while (0);

        xSemaphoreGive(stream->lock_resource);

        if (ret != ESP_PLAYER_ERR_OK) {
            if (db_owned_locally && aud_db) {
                esp_gmf_db_deinit(aud_db);
            }
            if (db_owned_locally && stream->audio_side->data_bus && player_data_bus_inner(stream->audio_side->data_bus) == aud_db) {
                player_data_bus_destroy(stream->audio_side->data_bus);
                stream->audio_side->data_bus = NULL;
            }
            return ret;
        }
    } else {
        esp_gmf_pipeline_reset(stream->audio_side->decoder);
    }
    esp_gmf_task_handle_t aud_dec_task = player_pipeline_task(stream->audio_side->decoder);
    esp_player_err_t run_ret = player_run_pipeline_with_timeout(stream, aud_dec_task, TASK_TIMEOUT_MS, stream->audio_side->decoder,
                                                                ESP_PLAYER_ERROR_SOURCE_AUDIO_DECODER);
    if (run_ret != ESP_PLAYER_ERR_OK) {
        return run_ret;
    }
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_pl_create_audio_render(esp_player_stream_t *stream)
{
    if (stream->_is_stop) {
        return ESP_PLAYER_ERR_OK;
    }
    if (stream->audio_side == NULL) {
        ESP_LOGE(TAG, "Cannot create audio renderer: audio side not allocated (mask has no AUDIO)");
        return ESP_PLAYER_ERR_FAIL;
    }
    if (stream->audio_side->render == NULL) {
        if (xSemaphoreTake(stream->lock_resource, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to acquire lock_resource, timeout");
            return ESP_PLAYER_ERR_TIMEOUT;
        }

        esp_player_err_t ret = ESP_PLAYER_ERR_OK;
        esp_gmf_port_handle_t in_port = NULL;
        bool in_port_registered = false;
        do {
            if (esp_gmf_pipeline_create(&stream->audio_side->render) != ESP_GMF_ERR_OK) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_AUDIO_RENDER, "pipeline_create");
                ret = ESP_PLAYER_ERR_NO_MEM;
                break;
            }
            player_audio_render_config_t audio_render_cfg = DEFAULT_PLAYER_AUDIO_RENDER_CONFIG();
            audio_render_cfg.sample_info.sample_rate = stream->audio_side->track_info.audio_info.sample_rate;
            audio_render_cfg.sample_info.bits_per_sample = stream->audio_side->track_info.audio_info.bits_per_sample;
            audio_render_cfg.sample_info.channel = stream->audio_side->track_info.audio_info.channels;
            audio_render_cfg.stream_handle = stream->audio_render_hd;
            audio_render_cfg.sync_handle = stream->sync_handle;
            esp_gmf_element_handle_t audio_render_el = NULL;
            if (player_audio_render_init(&audio_render_cfg, &audio_render_el) != ESP_GMF_ERR_OK) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_AUDIO_RENDER, "render_init");
                ret = ESP_PLAYER_ERR_FAIL;
                break;
            }
            if (player_audio_render_set_frame_duration(audio_render_el, ESP_PLAYER_AUDIO_RENDER_FRAME_MS) != ESP_GMF_ERR_OK) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_AUDIO_RENDER, "render_set_frame_duration");
                ret = ESP_PLAYER_ERR_FAIL;
                break;
            }
            esp_gmf_pipeline_register_el(stream->audio_side->render, audio_render_el);
            uint32_t aud_port_size = player_audio_ringbuf_size(
                stream->audio_side->track_info.audio_info.sample_rate,
                stream->audio_side->track_info.audio_info.channels,
                stream->audio_side->track_info.audio_info.bits_per_sample,
                stream->audio_side->track_info.audio_info.format);
            if (stream->audio_side->data_bus) {
                in_port = NEW_ESP_GMF_PORT_IN_BYTE(player_data_bus_acquire_read, player_data_bus_release_read, NULL,
                                                   (void *)stream->audio_side->data_bus, aud_port_size, ESP_GMF_MAX_DELAY);
            } else {
                in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read, NULL,
                                                   (void *)player_audio_db(stream), aud_port_size, ESP_GMF_MAX_DELAY);
            }
            if (in_port == NULL) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_AUDIO_RENDER, "in-port alloc");
                ret = ESP_PLAYER_ERR_NO_MEM;
                break;
            }
            if (esp_gmf_element_register_in_port(audio_render_el, in_port) != ESP_GMF_ERR_OK) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_AUDIO_RENDER, "register in-port");
                ret = ESP_PLAYER_ERR_FAIL;
                break;
            }
            in_port_registered = true;
            esp_gmf_pipeline_set_event(stream->audio_side->render, _audio_render_pipe_event_handler, stream);
            esp_gmf_task_cfg_t task_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
            player_cfg_fill_audio_render_task(stream, &task_cfg);
            esp_gmf_task_handle_t task_hd = NULL;
            ret = player_pl_task_create(&task_cfg, stream->audio_side->render, &task_hd);
            if (ret != ESP_PLAYER_ERR_OK) {
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_AUDIO_RENDER, "task_create");
                break;
            }
        } while (0);

        xSemaphoreGive(stream->lock_resource);

        if (ret != ESP_PLAYER_ERR_OK) {
            if (in_port && !in_port_registered) {
                esp_gmf_port_deinit(in_port);
            }
            if (stream->audio_side->render) {
                esp_gmf_pipeline_destroy(stream->audio_side->render);
                stream->audio_side->render = NULL;
            }
            return ret;
        }
    } else {
        esp_gmf_pipeline_reset(stream->audio_side->render);
    }
    return player_run_pipeline_with_timeout(stream, player_pipeline_task(stream->audio_side->render), TASK_TIMEOUT_MS,
                                            stream->audio_side->render, ESP_PLAYER_ERROR_SOURCE_AUDIO_RENDER);
}

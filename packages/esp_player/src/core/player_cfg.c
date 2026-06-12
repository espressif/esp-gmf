/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"

#include "player_internal.h"
#include "player_defaults_cfg.h"
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
#include "player_adec_defaults_cfg.h"
#include "esp_gmf_audio_dec.h"
#include "esp_audio_render.h"
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */

esp_player_err_t player_validate_sync_mode(esp_player_sync_mode_t sync_mode)
{
    if (sync_mode >= ESP_PLAYER_SYNC_MODE_MAX) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid sync_mode: %d", (int)sync_mode);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_validate_init_config(const esp_player_config_t *config)
{
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO && CONFIG_ESP_PLAYER_ENABLE_VIDEO
    if (config->audio_render_hd == NULL && config->video_render_hd == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, audio_render_hd: %p, video_render_hd: %p", config->audio_render_hd,
                 config->video_render_hd);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
#elif CONFIG_ESP_PLAYER_ENABLE_AUDIO
    if (config->audio_render_hd == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, audio_render_hd is NULL (audio path required)");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
#elif CONFIG_ESP_PLAYER_ENABLE_VIDEO
    if (config->video_render_hd == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, video_render_hd is NULL (video path required)");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO && CONFIG_ESP_PLAYER_ENABLE_VIDEO */
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_side_reconcile(esp_player_stream_t *stream, uint8_t new_mask)
{
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
    const bool want_audio = ((new_mask & ESP_PLAYER_MASK_AUDIO) != 0);
    if (want_audio && stream->audio_side == NULL) {
        stream->audio_side = (player_audio_side_t *)calloc(1, sizeof(player_audio_side_t));
        if (stream->audio_side == NULL) {
            ESP_LOGE(ESP_PLAYER_TAG, "Failed to allocate audio side");
            return ESP_PLAYER_ERR_NO_MEM;
        }
        stream->audio_side->track_info.track_type = ESP_PLAYER_TRACK_TYPE_AUDIO;
    } else if (!want_audio && stream->audio_side != NULL) {
        free(stream->audio_side);
        stream->audio_side = NULL;
    }
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */
#if CONFIG_ESP_PLAYER_ENABLE_VIDEO
    const bool want_video = ((new_mask & ESP_PLAYER_MASK_VIDEO) != 0);
    if (want_video && stream->video_side == NULL) {
        stream->video_side = (player_video_side_t *)calloc(1, sizeof(player_video_side_t));
        if (stream->video_side == NULL) {
            ESP_LOGE(ESP_PLAYER_TAG, "Failed to allocate video side");
            return ESP_PLAYER_ERR_NO_MEM;
        }
        stream->video_side->track_info.track_type = ESP_PLAYER_TRACK_TYPE_VIDEO;
    } else if (!want_video && stream->video_side != NULL) {
        free(stream->video_side);
        stream->video_side = NULL;
    }
#endif  /* CONFIG_ESP_PLAYER_ENABLE_VIDEO */
    return ESP_PLAYER_ERR_OK;
}

uint8_t player_build_time_av_mask(void)
{
    return (uint8_t)(
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
        ESP_PLAYER_MASK_AUDIO |
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */
#if CONFIG_ESP_PLAYER_ENABLE_VIDEO
        ESP_PLAYER_MASK_VIDEO |
#endif  /* CONFIG_ESP_PLAYER_ENABLE_VIDEO */
        0);
}

bool player_build_time_has_full_av(void)
{
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO && CONFIG_ESP_PLAYER_ENABLE_VIDEO
    return true;
#else
    return false;
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO && CONFIG_ESP_PLAYER_ENABLE_VIDEO */
}

void player_deinit_decoder_subcfg(esp_player_stream_t *stream)
{
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
    player_free_dec_subcfg_heap(stream);
#else
    (void)stream;
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */
}

esp_player_err_t player_set_dec_cfg_impl(esp_player_stream_t *stream, esp_player_format_t type, void *cfg,
                                         uint32_t cfg_sz)
{
#if !CONFIG_ESP_PLAYER_ENABLE_AUDIO
    (void)stream;
    (void)type;
    (void)cfg;
    (void)cfg_sz;
    return ESP_PLAYER_ERR_NOT_SUPPORT;
#else
    if (cfg == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, cfg: NULL");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    ESP_LOGI(ESP_PLAYER_TAG, "set dec cfg, type: %d, line: %d", type, __LINE__);

    bool simple = is_simple_format_type(type);
    uint32_t expected_sz = 0;
    esp_audio_simple_dec_type_t dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
    if (!simple) {
        if (player_dec_cfg_resolve(type, &expected_sz, &dec_type, NULL) != ESP_PLAYER_ERR_OK) {
            ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, type: 0x%08x", type);
            return ESP_PLAYER_ERR_NOT_SUPPORT;
        }
        if (cfg_sz != expected_sz) {
            ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, cfg_sz: %u, expected: %u, line: %d", (unsigned)cfg_sz,
                     (unsigned)expected_sz, __LINE__);
            return ESP_PLAYER_ERR_FAIL;
        }
    }

    if (xSemaphoreTake(stream->lock_resource, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(ESP_PLAYER_TAG, "lock_resource timeout in set_dec_cfg");
        return ESP_PLAYER_ERR_TIMEOUT;
    }

    esp_player_err_t ret = ESP_PLAYER_ERR_OK;
    if (simple) {
        if (cfg_sz != 0) {
            ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, cfg_sz: %u for simple dec cfg", (unsigned)cfg_sz);
            ret = ESP_PLAYER_ERR_FAIL;
            goto out;
        }
        player_free_dec_subcfg_heap(stream);
        stream->dec_cfg.dec_type = (type == ESP_FOURCC_MP4) ? ESP_AUDIO_SIMPLE_DEC_TYPE_M4A : (esp_audio_simple_dec_type_t)type;
    } else {
        if (stream->dec_cfg.dec_cfg == NULL || stream->dec_cfg.dec_type != dec_type || (uint32_t)stream->dec_cfg.cfg_size != expected_sz) {
            player_free_dec_subcfg_heap(stream);
            void *buf = calloc(1, expected_sz);
            if (buf == NULL) {
                ESP_LOGE(ESP_PLAYER_TAG, "dec subcfg alloc failed");
                ret = ESP_PLAYER_ERR_NO_MEM;
                goto out;
            }
            stream->dec_cfg.dec_cfg = buf;
        }
        size_t copy_size = (cfg_sz < expected_sz) ? cfg_sz : expected_sz;
        memcpy(stream->dec_cfg.dec_cfg, cfg, copy_size);
        stream->dec_cfg.dec_type = dec_type;
        stream->dec_cfg.cfg_size = (int)expected_sz;
    }

    if (stream->audio_side && stream->audio_side->decoder != NULL && (stream->av_mask & ESP_PLAYER_MASK_AUDIO) != 0) {
        esp_gmf_element_handle_t audio_dec_el = NULL;
        if (esp_gmf_pipeline_get_el_by_name(stream->audio_side->decoder, AUDIO_DECODER_TAG, &audio_dec_el) == ESP_GMF_ERR_OK &&
            audio_dec_el) {
            esp_gmf_err_t gmf_ret = esp_gmf_audio_dec_reconfig(audio_dec_el, &stream->dec_cfg);
            if (gmf_ret != ESP_GMF_ERR_OK) {
                ESP_LOGE(ESP_PLAYER_TAG, "esp_gmf_audio_dec_reconfig failed (%d)", gmf_ret);
                ret = ESP_PLAYER_ERR_FAIL;
            }
        }
    }
out:
    xSemaphoreGive(stream->lock_resource);
    return ret;
#endif  /* !CONFIG_ESP_PLAYER_ENABLE_AUDIO */
}

void player_set_speed_impl(esp_player_stream_t *stream, float speed, esp_player_err_t *out_ret)
{
    if (stream->sync_handle) {
        player_sync_set_speed(stream->sync_handle, speed);
    }
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
    if (stream->audio_render_hd) {
        if (esp_audio_render_stream_set_speed(stream->audio_render_hd, speed) != ESP_AUDIO_RENDER_ERR_OK) {
            ESP_LOGE(ESP_PLAYER_TAG, "Failed to set audio render speed");
            *out_ret = ESP_PLAYER_ERR_FAIL;
        }
    }
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */
}

void player_free_runtime_config(esp_player_stream_t *stream)
{
    if (stream == NULL) {
        return;
    }
    free(stream->task_cfg);
    stream->task_cfg = NULL;
    free(stream->buffer_cfg);
    stream->buffer_cfg = NULL;
}

esp_player_err_t player_set_task_config_impl(esp_player_stream_t *stream, const esp_player_task_config_t *config)
{
    if (config == NULL) {
        free(stream->task_cfg);
        stream->task_cfg = NULL;
        return ESP_PLAYER_ERR_OK;
    }
    if (stream->task_cfg == NULL) {
        stream->task_cfg = (esp_player_task_config_t *)calloc(1, sizeof(esp_player_task_config_t));
        if (stream->task_cfg == NULL) {
            return ESP_PLAYER_ERR_NO_MEM;
        }
    }
    *stream->task_cfg = *config;
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_set_buffer_config_impl(esp_player_stream_t *stream, const esp_player_buffer_config_t *config)
{
    if (config == NULL) {
        free(stream->buffer_cfg);
        stream->buffer_cfg = NULL;
        return ESP_PLAYER_ERR_OK;
    }
    if (stream->buffer_cfg == NULL) {
        stream->buffer_cfg = (esp_player_buffer_config_t *)calloc(1, sizeof(esp_player_buffer_config_t));
        if (stream->buffer_cfg == NULL) {
            return ESP_PLAYER_ERR_NO_MEM;
        }
    }
    *stream->buffer_cfg = *config;
    return ESP_PLAYER_ERR_OK;
}

static void player_cfg_fill_gmf_task(const esp_gmf_task_config_t *thread,
                                     uint16_t default_stack, int8_t default_core, uint8_t default_prio,
                                     const char *name, esp_gmf_task_cfg_t *out)
{
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.name = name;
    if (thread != NULL) {
        cfg.thread = *thread;
    } else {
        cfg.thread.stack = default_stack;
        cfg.thread.core = default_core;
        cfg.thread.prio = default_prio;
    }
    *out = cfg;
}

void player_cfg_fill_extractor_task(const esp_player_stream_t *stream, esp_gmf_task_cfg_t *out)
{
    static const esp_gmf_task_config_t default_cfg = ESP_PLAYER_DEFAULT_EXTRACTOR_TASK();
    const esp_gmf_task_config_t *thread = stream->task_cfg ? &stream->task_cfg->extractor : &default_cfg;
    player_cfg_fill_gmf_task(thread,
                             (uint16_t)default_cfg.stack,
                             (int8_t)default_cfg.core,
                             (uint8_t)default_cfg.prio,
                             EXTRACTOR_TAG, out);
}

void player_cfg_fill_audio_decoder_task(const esp_player_stream_t *stream, esp_gmf_task_cfg_t *out)
{
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
    static const esp_gmf_task_config_t default_cfg = ESP_PLAYER_DEFAULT_AUDIO_DECODER_TASK();
    const esp_gmf_task_config_t *thread = stream->task_cfg ? &stream->task_cfg->audio_decoder : &default_cfg;
    player_cfg_fill_gmf_task(thread,
                             (uint16_t)default_cfg.stack,
                             (int8_t)default_cfg.core,
                             (uint8_t)default_cfg.prio,
                             AUDIO_DECODER_TAG, out);
#else
    (void)stream;
    (void)out;
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */
}

void player_cfg_fill_audio_render_task(const esp_player_stream_t *stream, esp_gmf_task_cfg_t *out)
{
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
    static const esp_gmf_task_config_t default_cfg = ESP_PLAYER_DEFAULT_AUDIO_RENDER_TASK();
    const esp_gmf_task_config_t *thread = stream->task_cfg ? &stream->task_cfg->audio_render : &default_cfg;
    player_cfg_fill_gmf_task(thread,
                             (uint16_t)default_cfg.stack,
                             (int8_t)default_cfg.core,
                             (uint8_t)default_cfg.prio,
                             AUDIO_RENDER_TAG, out);
#else
    (void)stream;
    (void)out;
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */
}

void player_cfg_fill_video_decoder_task(const esp_player_stream_t *stream, esp_gmf_task_cfg_t *out)
{
#if CONFIG_ESP_PLAYER_ENABLE_VIDEO
    static const esp_gmf_task_config_t default_cfg = ESP_PLAYER_DEFAULT_VIDEO_DECODER_TASK();
    const esp_gmf_task_config_t *thread = stream->task_cfg ? &stream->task_cfg->video_decoder : &default_cfg;
    player_cfg_fill_gmf_task(thread,
                             (uint16_t)default_cfg.stack,
                             (int8_t)default_cfg.core,
                             (uint8_t)default_cfg.prio,
                             VIDEO_DECODER_TAG, out);
#else
    (void)stream;
    (void)out;
#endif  /* CONFIG_ESP_PLAYER_ENABLE_VIDEO */
}

void player_cfg_fill_video_render_task(const esp_player_stream_t *stream, esp_gmf_task_cfg_t *out)
{
#if CONFIG_ESP_PLAYER_ENABLE_VIDEO
    static const esp_gmf_task_config_t default_cfg = ESP_PLAYER_DEFAULT_VIDEO_RENDER_TASK();
    const esp_gmf_task_config_t *thread = stream->task_cfg ? &stream->task_cfg->video_render : &default_cfg;
    player_cfg_fill_gmf_task(thread,
                             (uint16_t)default_cfg.stack,
                             (int8_t)default_cfg.core,
                             (uint8_t)default_cfg.prio,
                             VIDEO_RENDER_TAG, out);
#else
    (void)stream;
    (void)out;
#endif  /* CONFIG_ESP_PLAYER_ENABLE_VIDEO */
}

uint32_t player_cfg_extractor_pool_size(const esp_player_stream_t *stream, bool for_video)
{
    if (stream->buffer_cfg != NULL && stream->buffer_cfg->extractor_pool_size != 0) {
        return stream->buffer_cfg->extractor_pool_size;
    }
    return for_video ? DEFAULT_EXTRACTOR_VIDEO_POOL_SIZE : DEFAULT_EXTRACTOR_AUDIO_POOL_SIZE;
}

uint32_t player_cfg_http_read_buf_size(const esp_player_stream_t *stream)
{
    if (stream->buffer_cfg != NULL && stream->buffer_cfg->http_read_buf_size != 0) {
        return stream->buffer_cfg->http_read_buf_size;
    }
    return ESP_PLAYER_DEFAULT_HTTP_READ_BUF_SIZE;
}

uint32_t player_cfg_prebuffer_resume_ms(const esp_player_stream_t *stream)
{
    if (stream->buffer_cfg != NULL && stream->buffer_cfg->prebuffer_resume_ms != 0) {
        return stream->buffer_cfg->prebuffer_resume_ms;
    }
    return ESP_PLAYER_DEFAULT_PREBUFFER_RESUME_MS;
}

uint32_t player_cfg_rebuffer_enter_ms(const esp_player_stream_t *stream)
{
    if (stream->buffer_cfg != NULL && stream->buffer_cfg->rebuffer_enter_ms != 0) {
        return stream->buffer_cfg->rebuffer_enter_ms;
    }
    return ESP_PLAYER_DEFAULT_REBUFFER_ENTER_MS;
}

uint32_t player_cfg_rebuffer_resume_ms(const esp_player_stream_t *stream)
{
    if (stream->buffer_cfg != NULL && stream->buffer_cfg->rebuffer_resume_ms != 0) {
        return stream->buffer_cfg->rebuffer_resume_ms;
    }
    return ESP_PLAYER_DEFAULT_REBUFFER_RESUME_MS;
}

uint32_t player_cfg_rebuffer_grace_ms(const esp_player_stream_t *stream)
{
    if (stream->buffer_cfg != NULL && stream->buffer_cfg->rebuffer_grace_ms != 0) {
        return stream->buffer_cfg->rebuffer_grace_ms;
    }
    return ESP_PLAYER_DEFAULT_REBUFFER_GRACE_MS;
}

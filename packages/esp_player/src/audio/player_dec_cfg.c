/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>

#include "esp_log.h"

#include "player_stream.h"
#include "player_adec_defaults_cfg.h"
#include "player_helper.h"

void player_free_dec_subcfg_heap(esp_player_stream_t *stream)
{
    if (stream->dec_cfg.dec_cfg) {
        free(stream->dec_cfg.dec_cfg);
        stream->dec_cfg.dec_cfg = NULL;
    }
    stream->dec_cfg.cfg_size = 0;
}

esp_player_err_t player_prepare_dec_cfg(esp_player_stream_t *stream, esp_player_format_t format)
{
    if (stream == NULL) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    if (is_simple_format_type(format)) {
        player_free_dec_subcfg_heap(stream);
        if (format == ESP_FOURCC_MP4) {
            stream->dec_cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_M4A;
        } else {
            stream->dec_cfg.dec_type = (esp_audio_simple_dec_type_t)format;
        }
        return ESP_PLAYER_ERR_OK;
    }

    uint32_t sz = 0;
    esp_audio_simple_dec_type_t dt = ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
    if (player_dec_cfg_resolve(format, &sz, &dt, NULL) != ESP_PLAYER_ERR_OK) {
        return ESP_PLAYER_ERR_NOT_SUPPORT;
    }

    if (stream->dec_cfg.dec_type == dt && stream->dec_cfg.dec_cfg != NULL && (uint32_t)stream->dec_cfg.cfg_size == sz) {
        return ESP_PLAYER_ERR_OK;
    }

    player_free_dec_subcfg_heap(stream);

    void *buf = calloc(1, sz);
    if (buf == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "dec subcfg alloc %u failed", (unsigned)sz);
        return ESP_PLAYER_ERR_NO_MEM;
    }
    player_dec_cfg_resolve(format, &sz, &dt, buf);
    stream->dec_cfg.dec_cfg = buf;
    stream->dec_cfg.cfg_size = (int)sz;
    stream->dec_cfg.dec_type = dt;
    return ESP_PLAYER_ERR_OK;
}

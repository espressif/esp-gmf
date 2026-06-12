/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "player_stream.h"

esp_gmf_db_handle_t player_video_db(esp_player_stream_t *stream)
{
    if (stream->video_side == NULL) {
        return NULL;
    }
    esp_gmf_db_handle_t db = player_get_db(stream->video_side->decoder, true);
    return db ? db : player_get_db(stream->video_side->render, false);
}

int8_t player_video_track_idx(esp_player_stream_t *stream)
{
    int8_t idx = -1;
    esp_gmf_element_handle_t el = player_extractor_el(stream);
    if (el) {
        player_extractor_track_active(el, ESP_EXTRACTOR_STREAM_TYPE_VIDEO, &idx);
    }
    return idx;
}

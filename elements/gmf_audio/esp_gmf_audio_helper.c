/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_gmf_audio_helper.h"
#include "esp_fourcc.h"

static const char *TAG = "ESP_GMF_AUDIO_HELPER";

esp_gmf_err_t esp_gmf_audio_helper_get_audio_type_by_uri(const char *uri, uint32_t *format_id)
{
    const char *ext = strrchr(uri, '.');
    if (ext == NULL) {
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    ext++;
    if (strncasecmp(ext, "aac", 3) == 0) {
        *format_id = ESP_FOURCC_AAC;
    } else if (strncasecmp(ext, "g711a", 5) == 0) {
        *format_id = ESP_FOURCC_ALAW;
    } else if (strncasecmp(ext, "g711u", 5) == 0) {
        *format_id = ESP_FOURCC_ULAW;
    } else if (strncasecmp(ext, "amr", 3) == 0) {
        *format_id = ESP_FOURCC_AMRNB;
    } else if (strncasecmp(ext, "awb", 3) == 0) {
        *format_id = ESP_FOURCC_AMRWB;
    } else if (strncasecmp(ext, "alac", 4) == 0) {
        *format_id = ESP_FOURCC_ALAC;
    } else if (strncasecmp(ext, "pcm", 3) == 0) {
        *format_id = ESP_FOURCC_PCM;
    } else if (strncasecmp(ext, "opus", 4) == 0) {
        *format_id = ESP_FOURCC_OPUS;
    } else if (strncasecmp(ext, "adpcm", 5) == 0) {
        *format_id = ESP_FOURCC_ADPCM;
    } else if (strncasecmp(ext, "sbc", 3) == 0) {
        *format_id = ESP_FOURCC_SBC;
    } else if (strncasecmp(ext, "lc3", 3) == 0) {
        *format_id = ESP_FOURCC_LC3;
    } else if (strncasecmp(ext, "mp3", 3) == 0) {
        *format_id = ESP_FOURCC_MP3;
    } else if (strncasecmp(ext, "m4a", 3) == 0) {
        *format_id = ESP_FOURCC_M4A;
    } else if (strncasecmp(ext, "wav", 3) == 0) {
        *format_id = ESP_FOURCC_WAV;
    } else if (strncasecmp(ext, "ts", 2) == 0) {
        *format_id = ESP_FOURCC_M2TS;
    } else if (strncasecmp(ext, "flac", 4) == 0) {
        *format_id = ESP_FOURCC_FLAC;
    } else {
        *format_id = 0;
        ESP_LOGE(TAG, "Not support audio codec type, %s", uri);
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    return ESP_GMF_ERR_OK;
}

bool esp_gmf_audio_helper_is_frame_dec(uint32_t dec_type)
{
    for (int i = 0; i < sizeof(esp_gmf_audio_containers) / sizeof(esp_gmf_audio_containers[0]); i++) {
        if (dec_type == esp_gmf_audio_containers[i]) {
            return false;
        }
    }
    return true;
}

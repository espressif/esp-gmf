/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include "esp_gmf_err.h"
#include "esp_fourcc.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  List of audio container types supported by decoder detection
 */
static const int esp_gmf_audio_containers[] = {
    ESP_FOURCC_AAC,
    ESP_FOURCC_MP3,
    ESP_FOURCC_FLAC,
    ESP_FOURCC_AMRNB,
    ESP_FOURCC_AMRWB,
    ESP_FOURCC_WAV,
    ESP_FOURCC_M4A,
    ESP_FOURCC_M2TS,
};

/**
 * @brief  Get audio codec type by uri
 *
 * @param[in]   uri        URI of audio codec
 * @param[out]  format_id  Type of audio codec(ESP_FourCC type)
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_NOT_SUPPORT  Not supported audio codec type
 */
esp_gmf_err_t esp_gmf_audio_helper_get_audio_type_by_uri(const char *uri, uint32_t *format_id);

/**
 * @brief  Check if the given decoder type is a frame-based decoder
 *
 * @param[in]  dec_type  The decoder type to check
 *
 * @return
 *       - true   If the decoder is frame-based
 *       - false  otherwise
 */
bool esp_gmf_audio_helper_is_frame_dec(uint32_t dec_type);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

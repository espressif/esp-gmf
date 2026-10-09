/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stddef.h>

#include "esp_gmf_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/* Helpers shared by GMF AI audio elements. */

/**
 * Maximum channel count in one ADC channel label list.
 */
#define ESP_GMF_AI_AUDIO_SR_FORMAT_MAX_CH  (8)

/**
 * @brief  Convert an ADC channel label list into an esp-sr input format
 *
 *         `label` is a comma-separated list of logical channel names in
 *         codec_dev 2.0 channel order (LSB to MSB). Spaces around tokens
 *         are ignored. Duplicate names are allowed.
 *
 *         Only the following tokens are supported:
 *         - FC: Front Center
 *         - RE: Reference signal
 *         - FL / FR: Front Left / Right
 *         - SL / SR: Side Left / Right
 *         - BL / BR: Back Left / Right
 *         - NA: Not available / not enabled
 *
 *         Any other token is invalid, including lowercase names (`na`, `fl`)
 *         and near-NA spellings such as `N/A` or `NF`. Matching is exact and
 *         case-sensitive. Empty tokens such as "FL,,RE" are invalid.
 *         At most `ESP_GMF_AI_AUDIO_SR_FORMAT_MAX_CH` channels are accepted.
 *         A NULL or empty label is rejected.
 *
 *         Each token maps to one character of the esp-sr input format (`afe_config_init()`,
 *         `esp_gmf_aec_cfg_t.input_format`, `esp_gmf_wn_cfg_t.input_format`,
 *         `esp_gmf_doa_cfg_t.input_format`). Channel order is preserved:
 *         - FC, FL, FR, SL, SR, BL, BR  -> `M` (microphone)
 *         - RE                          -> `R` (reference)
 *         - NA                          -> `N` (unused)
 *
 *         For example, "FL,RE,FR,NA" becomes "MRMN".
 *         On failure, `sr_format` is left unchanged.
 *
 * @param[in]   ch_layout       ADC channel layout label list such as "FL,NA,RE"
 * @param[out]  sr_format       Output buffer for the NUL-terminated esp-sr format
 * @param[in]   sr_format_size  Size of `sr_format` in bytes, including the NUL terminator.
 *                              Use at least `ESP_GMF_AI_AUDIO_SR_FORMAT_MAX_CH + 1`
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  NULL argument, or the label is empty or malformed
 *       - ESP_GMF_ERR_NOT_ENOUGH   `sr_format` is too small for the converted string
 */
esp_gmf_err_t esp_gmf_ai_audio_ch_layout_to_sr_format(const char *ch_layout, char *sr_format, size_t sr_format_size);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

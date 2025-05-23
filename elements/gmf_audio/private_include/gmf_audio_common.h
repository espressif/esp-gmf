/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_err.h"
#include "esp_gmf_info.h"
#include "esp_gmf_audio_element.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define GMF_AUDIO_UPDATE_SND_INFO(self, sample_rate, bits, channel) gmf_audio_update_snd_info(self, sample_rate, bits, channel)
#define GMF_AUDIO_INPUT_SAMPLE_NUM                                  (256)
#define GMF_AUDIO_CALC_PTS(out_len, sample_rate, ch, bits)          ((out_len) * 8000 / ((sample_rate) * (ch) * (bits)))

static inline void gmf_audio_update_snd_info(void *self, uint32_t sample_rate, uint8_t bits, uint8_t channel)
{
    esp_gmf_info_sound_t snd_info = {0};
    esp_gmf_audio_el_get_snd_info(self, &snd_info);
    snd_info.sample_rates = sample_rate;
    snd_info.channels = channel;
    snd_info.bits = bits;
    esp_gmf_audio_el_set_snd_info(self, &snd_info);
    esp_gmf_element_notify_snd_info(self, &snd_info);
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */

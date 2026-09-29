/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_ble_audio_bap_lc3_preset_defs.h"

/*
 * The public DEFINE macros size each preset's codec and metadata buffers to
 * CONFIG_BT_AUDIO_CODEC_CFG_MAX_*_SIZE and leave them writable, so the objects
 * land in DIRAM .data. These templates are copied before any
 * esp_ble_audio_codec_cfg_*_set_* call, so exact-size const storage is enough
 * and the linker can place them in .rodata.
 *
 * codec_cfg.data and codec_cfg.meta are uint8_t *. The cast documents that the
 * bytes stay read-only; callers must copy before writing.
 */
#undef ESP_BLE_AUDIO_BAP_LC3_PRESET_DEFINE_QOS
#define ESP_BLE_AUDIO_BAP_LC3_PRESET_DEFINE_QOS(_name, _freq, _duration, _loc, _len, \
                                                _frames_per_sdu, _stream_context, _qos) \
    static const uint8_t codec_cfg_data_##_name[] = \
        ESP_BLE_AUDIO_CODEC_CFG_LC3_DATA(_freq, _duration, _loc, _len, _frames_per_sdu); \
    static const uint8_t codec_cfg_meta_##_name[] = \
        ESP_BLE_AUDIO_CODEC_CFG_LC3_META(_stream_context); \
    static const esp_ble_audio_bap_lc3_preset_t _name = \
        ESP_BLE_AUDIO_BAP_LC3_PRESET( \
            ESP_BLE_AUDIO_CODEC_CFG_LC3_LEN( \
                (uint8_t *)codec_cfg_data_##_name, sizeof(codec_cfg_data_##_name), \
                (uint8_t *)codec_cfg_meta_##_name, sizeof(codec_cfg_meta_##_name)), \
            _qos)

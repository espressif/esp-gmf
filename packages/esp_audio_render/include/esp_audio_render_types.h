/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Audio render error code
 */
typedef enum {
    ESP_AUDIO_RENDER_ERR_OK            = 0,   /*!< No error */
    ESP_AUDIO_RENDER_ERR_FAIL          = -1,  /*!< General failure error */
    ESP_AUDIO_RENDER_ERR_INVALID_ARG   = -2,  /*!< Invalid argument error */
    ESP_AUDIO_RENDER_ERR_NO_MEM        = -3,  /*!< Not enough memory error */
    ESP_AUDIO_RENDER_ERR_NOT_SUPPORTED = -4,  /*!< Not supported error */
    ESP_AUDIO_RENDER_ERR_NOT_FOUND     = -5,  /*!< Not found error */
    ESP_AUDIO_RENDER_ERR_TIMEOUT       = -6,  /*!< Run timeout error */
    ESP_AUDIO_RENDER_ERR_INVALID_STATE = -7,  /*!< Invalid state error */
    ESP_AUDIO_RENDER_ERR_NO_RESOURCE   = -8,  /*!< No resource error */
} esp_audio_render_err_t;

/**
 * @brief  Audio render process type
 *
 * @note  Users can add customized process elements into audio render process
 *        Then can get element handle through `esp_audio_render_stream_get/mixed_element` and do extra settings
 *        Definition of process types are aligned with https://github.com/espressif/esp-gmf/blob/main/gmf_core/helpers/include/esp_gmf_caps_def.h
 */
typedef enum {
    ESP_AUDIO_RENDER_PROC_NONE  = 0,
    ESP_AUDIO_RENDER_PROC_ALC   = 0x0000434C41445541ULL,  /*!< ALC ("AUDALC": Automatic Level Control) audio processor */
    ESP_AUDIO_RENDER_PROC_SONIC = 0x43494E4F53445541ULL,  /*!< Sonic ("AUDSONIC": speed and pitch control) audio processor */
    ESP_AUDIO_RENDER_PROC_EQ    = 0x0000005145445541ULL,  /*!< EQ ("AUDEQ": Equalizer) audio processor */
    ESP_AUDIO_RENDER_PROC_FADE  = 0x0045444146445541ULL,  /*!< Fade ("AUDFADE": volume fade) audio processor */
    ESP_AUDIO_RENDER_PROC_ENC   = 0x0000434E45445541ULL,  /*!< Audio encode ("AUDENC": audio encoder) audio processor
                                                               (only allowed to place on mixed processor) */
} esp_audio_render_proc_type_t;

/**
 * @brief  Audio render sample information
 */
typedef struct {
    uint32_t  sample_rate;      /*!< Audio sample rate */
    uint8_t   bits_per_sample;  /*!< Bit depth per sample */
    uint8_t   channel;          /*!< Audio channel */
} esp_audio_render_sample_info_t;

/**
 * @brief  Audio render mixer gain
 */
typedef struct {
    float     initial_gain;     /*!< Initial gain for mixer stream */
    float     target_gain;      /*!< Target gain for mixer stream */
    uint32_t  transition_time;  /*!< Transition time to smooth from initial to target or versa (unit ms) */
} esp_audio_render_mixer_gain_t;

/**
 * @brief  Audio render writer callback
 *
 * @param[in]  pcm_data  PCM data to be written
 * @param[in]  pcm_size  PCM data size
 * @param[in]  ctx       Writer context
 *
 * @return
 *       - 0       On success
 *       - Others  Failed to write
 */
typedef int (*esp_audio_render_write_cb_t)(uint8_t *pcm_data, uint32_t pcm_size, void *ctx);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

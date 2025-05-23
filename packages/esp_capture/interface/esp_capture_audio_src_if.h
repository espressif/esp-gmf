/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_capture_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Capture audio source interface (typedef alias)
 */
typedef struct esp_capture_audio_src_if_t esp_capture_audio_src_if_t;

/**
 * @brief  Capture audio source interface
 */
struct esp_capture_audio_src_if_t {
    /**
     * @brief  Open the audio source for capturing
     *
     * @param[in]  src  Pointer to the audio source interface
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to open audio source
     */
    esp_capture_err_t (*open)(esp_capture_audio_src_if_t *src);

    /**
     * @brief  Get the supported audio codecs
     *
     * @param[in]   src     Pointer to the audio source interface
     * @param[out]  codecs  Pointer to an array of supported codecs
     * @param[out]  num     Pointer codec numbers to be filled
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to get support codecs
     */
    esp_capture_err_t (*get_support_codecs)(esp_capture_audio_src_if_t *src, const esp_capture_format_id_t **codecs, uint8_t *num);

    /**
     * @brief  Set fixed capability for audio source
     *
     * @note  If fixed capability is set, `negotiate_caps` returns it directly if the format matches
     *
     * @param[in]   src       Pointer to the audio source interface
     * @param[in]   aud_caps  Pointer to fixed audio capability to set
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to set fixed capability
     */
    esp_capture_err_t (*set_fixed_caps)(esp_capture_audio_src_if_t *src, const esp_capture_audio_info_t *fixed_caps);

    /**
     * @brief Negotiate capabilities between the source and the sink
     *
     * @param[in]   src       Pointer to the audio source interface
     * @param[in]   in_caps   Pointer to capabilities to be negotiated
     * @param[out]  out_caps  Pointer to negotiated results
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to negotiate capabilities
     */
    esp_capture_err_t (*negotiate_caps)(esp_capture_audio_src_if_t *src, esp_capture_audio_info_t *in_caps, esp_capture_audio_info_t *out_caps);

    /**
     * @brief  Start capturing audio from the source
     *
     * @param[in]  src  Pointer to the audio source interface
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to start audio source
     */
    esp_capture_err_t (*start)(esp_capture_audio_src_if_t *src);

    /**
     * @brief Read a frame of audio data from the source
     *
     * @note  This API will read `frame->size` bytes and fill them into the given `frame->data`
     *        The frame size should be set before calling this function
     *        The frame data buffer should be large enough to hold the requested size
     *
     * @param[in]   src    Pointer to the audio source interface
     * @param[out]  frame  Pointer to the output audio frame
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to read audio frame
     */
    esp_capture_err_t (*read_frame)(esp_capture_audio_src_if_t *src, esp_capture_stream_frame_t *frame);

    /**
     * @brief  Stop capturing audio from the source
     *
     * @param[in]  src  Pointer to the audio source interface
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to stop audio source
     */
    esp_capture_err_t (*stop)(esp_capture_audio_src_if_t *src);

    /**
     * @brief  Close the audio source and release resources
     *
     * @param[in]  src  Pointer to the audio source interface
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to close audio source
     */
    esp_capture_err_t (*close)(esp_capture_audio_src_if_t *src);
};

#ifdef __cplusplus
}
#endif  /* __cplusplus */

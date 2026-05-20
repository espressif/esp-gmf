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
 * @brief  Capture overlay interface (typedef alias)
 */
typedef struct esp_capture_overlay_if_t esp_capture_overlay_if_t;

/**
 * @brief  Capture overlay interface
 */
struct esp_capture_overlay_if_t {
    /**
     * @brief  Open the overlay interface
     *
     * @param[in]  src  Pointer to the overlay interface instance
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to open overlay
     */
    esp_capture_err_t (*open)(esp_capture_overlay_if_t *src);

    /**
     * @brief  Get the overlay region and codec type
     *
     * @param[in]   src    Pointer to the overlay interface instance
     * @param[out]  codec  Pointer to store the codec type
     * @param[out]  rgn    Pointer to store the region information
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to get overlay region
     */
    esp_capture_err_t (*get_overlay_region)(esp_capture_overlay_if_t *src, esp_capture_format_id_t *codec, esp_capture_rgn_t *rgn);

    /**
     * @brief  Set the alpha value for the overlay
     *
     * @note  Alpha is the overlay blend strength (same as GMF video overlay):
     *        - 0: Fully transparent (overlay invisible)
     *        - 255: Fully opaque (overlay fully visible)
     *        - Values in between: Partial alpha blend
     *
     * @param[in]  src    Pointer to the overlay interface instance
     * @param[in]  alpha  Alpha value (0-255)
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to set alpha
     */
    esp_capture_err_t (*set_alpha)(esp_capture_overlay_if_t *src, uint8_t alpha);

    /**
     * @brief Get the current alpha value of the overlay
     *
     * @param[in]   src    Pointer to the overlay interface instance
     * @param[out]  alpha  Pointer to store the current alpha value
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to get alpha
     */
    esp_capture_err_t (*get_alpha)(esp_capture_overlay_if_t *src, uint8_t *alpha);

    /**
     * @brief  Set the trans-color for the overlay
     *
     * @param[in]  src  Pointer to the overlay interface instance
     * @param[in]  rgb  Pointer to the trans-color value
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to set trans-color
     */
    esp_capture_err_t (*set_trans_color)(esp_capture_overlay_if_t *src, const uint8_t rgb[3]);

    /**
     * @brief  Get the trans-color for the overlay
     *
     * @param[in]   src              Pointer to the overlay interface instance
     * @param[out]  has_trans_color  Pointer to store the trans-color flag
     * @param[out]  rgb              Pointer to store the trans-color value
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to get trans-color
     */
    esp_capture_err_t (*get_trans_color)(esp_capture_overlay_if_t *src, bool *has_trans_color, uint8_t rgb[3]);

    /**
     * @brief Acquire a frame for the overlay
     *
     * @note  The acquired frame should be released using release_frame when no longer needed
     *        Multiple frames can be acquired before releasing them
     *
     * @param[in]   src    Pointer to the overlay interface instance
     * @param[out]  frame  Pointer to store the frame information
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to acquire overlay frame
     */
    esp_capture_err_t (*acquire_frame)(esp_capture_overlay_if_t *src, esp_capture_stream_frame_t *frame);

    /**
     * @brief  Release a previously acquired frame
     *
     * @param[in]  src    Pointer to the overlay interface instance
     * @param[in]  frame  Pointer to the frame to be released
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to release overlay frame
     */
    esp_capture_err_t (*release_frame)(esp_capture_overlay_if_t *src, esp_capture_stream_frame_t *frame);

    /**
     * @brief Close the overlay interface
     *
     * @param[in]  src  Pointer to the overlay interface instance
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to close overlay
     */
    esp_capture_err_t (*close)(esp_capture_overlay_if_t *src);

    /**
     * @brief  Link to the next overlay region
     *
     * @note  Each region is a separate overlay interface. Build a list with
     *        `ESP_CAPTURE_APPEND_OVERLAY` and pass only the list head to
     *        `esp_capture_sink_add_overlay()` once.
     */
    esp_capture_overlay_if_t *next;
};

/**
 * @brief  Append an overlay region to the tail of a linked list
 *
 * @param  head  List head (not modified)
 * @param  tail  Overlay to append; its `next` is cleared
 */
#define ESP_CAPTURE_APPEND_OVERLAY(head, tail)  do {  \
    esp_capture_overlay_if_t *_ov_append = (head);    \
    (tail)->next = NULL;                              \
    while (_ov_append->next != NULL) {                \
        _ov_append = _ov_append->next;                \
    }                                                 \
    _ov_append->next = (tail);                        \
} while (0)

#ifdef __cplusplus
}
#endif  /* __cplusplus */

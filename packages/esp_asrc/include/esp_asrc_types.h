/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Handle for the ASRC instance
 */
typedef void *esp_asrc_handle_t;

/**
 * @brief  ASRC error code
 */
typedef enum {
    ESP_ASRC_ERR_OK                = 0,   /*!< Operation succeeded */
    ESP_ASRC_ERR_FAIL              = -1,  /*!< Operation failed */
    ESP_ASRC_ERR_MEM_LACK          = -2,  /*!< Memory allocation failure */
    ESP_ASRC_ERR_INVALID_PARAMETER = -3,  /*!< Invalid parameter */
    ESP_ASRC_ERR_NOT_SUPPORT       = -4,  /*!< Unsupported type */
    ESP_ASRC_ERR_TIMEOUT           = -5,  /*!< Hardware asrc process timeout */
    ESP_ASRC_ERR_INVALID_ALIGNMENT = -6,  /*!< Invalid buffer address or size alignment */
} esp_asrc_err_t;

/**
 * @brief  ASRC performance preference type
 */
typedef enum {
    ESP_ASRC_PERF_TYPE_AUTO      = 0,  /*!< Automatically select optimal performance mode */
    ESP_ASRC_PERF_TYPE_HW_ONLY   = 1,  /*!< Use hardware ASRC only (no fallback to software) */
    ESP_ASRC_PERF_TYPE_SW_MEMORY = 2,  /*!< Use software ASRC optimized for memory usage */
    ESP_ASRC_PERF_TYPE_SW_SPEED  = 3,  /*!< Use software ASRC optimized for speed */
} esp_asrc_perf_type_t;

/**
 * @brief  Audio information structure
 */
typedef struct {
    uint32_t  sample_rate;      /*!< The audio sample rate */
    uint8_t   channel;          /*!< The audio channel number */
    uint8_t   bits_per_sample;  /*!< The audio bits per sample */
} esp_asrc_aud_info_t;

/**
 * @brief  Buffer alignment requirements for ASRC frames
 */
typedef struct {
    uint32_t inbuf_addr_align;   /*!< Required address alignment for input buffer */
    uint32_t inbuf_size_align;   /*!< Required size alignment for input buffer length */
    uint32_t outbuf_addr_align;  /*!< Required address alignment for output buffer */
    uint32_t outbuf_size_align;  /*!< Required size alignment for output buffer length */
} esp_asrc_buffer_alignment_t;

/**
 * @brief  Configuration structure for initializing the ASRC
 */
typedef struct {
    esp_asrc_aud_info_t   src_info;    /*!< Source audio format information */
    esp_asrc_aud_info_t   dest_info;   /*!< Target audio format information */
    float                *weight;      /*!< Optional ASRC weight coefficients
                                            Note:
                                             1. If NULL, weights will default to 1 / src_ch
                                                For example, in 2 to 3 channel case, it will internally use:
                                                {0.5, 0.5, 0.5, 0.5, 0.5, 0.5}
                                             2. If non-NULL, weight must be an array of length `src_ch * dest_ch`
                                                For example, 2 to 3 channel conversion with weights:
                                                  B1 = W1 * A1 + W2 * A2;
                                                  B2 = W3 * A1 + W4 * A2;
                                                  B3 = W5 * A1 + W6 * A2;
                                                Use the weight array: {W1, W2, W3, W4, W5, W6}
                                             3. Only valid when channel configuration changes. Equal src_ch and dest_ch implies bypass-style channel conversion */
    uint32_t              weight_len;  /*!< Number of elements in the `weight` array
                                            - Should be set to `src_ch * dest_ch` when `weight` is not NULL
                                            - If `weight` is NULL, `weight_len` is ignored */
    esp_asrc_perf_type_t  perf_type;   /*!< Desired performance mode */
    uint8_t               complexity;  /*!< Resampling algorithm complexity level (1–3)
                                            Notes:
                                            - 1: Lowest quality, highest speed
                                            - 3: Highest quality, lowest speed
                                            - Ignored when `perf_type` is set to `ESP_ASRC_PERF_TYPE_HW_ONLY` */
    int32_t               timeout_ms;  /*!< ASRC timeout threshold for handling one audio frame, -1 means wait max delay
                                            Notes: Ignored when `perf_type` is set to `ESP_ASRC_PERF_TYPE_SW_MEMORY` or `ESP_ASRC_PERF_TYPE_SW_SPEED` */
} esp_asrc_cfg_t;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

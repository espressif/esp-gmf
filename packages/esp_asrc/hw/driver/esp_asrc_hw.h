/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_asrc_types.h"
#include "asrc_utils.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  The ASRC hardware driver leverages on-chip ASRC peripheral resources to
 *         perform real-time audio sample-rate conversion with low latency and minimal
 *         CPU overhead. It supports simultaneous conversion of sample rate, channel
 *         count, and bit depth as specified in `esp_asrc_hw_cfg_t`.
 *
 *         The hardware engine processes audio in units of sample frames. The byte
 *         size of one sample frame is:
 *         frame_bytes = channel * (bits_per_sample >> 3).
 *         For a given processing call, the total input byte count is:
 *         in_bytes = in_samples_num * frame_bytes.
 *
 *         When the conversion ratio is not an integer, the actual output sample
 *         count per call may vary slightly due to internal fractional accumulation.
 *         Use `esp_asrc_hw_get_out_sample_num` to obtain the expected output count
 *         for a given input count so that a sufficiently sized output buffer can be
 *         allocated in advance.
 *
 *         Input and output buffers must satisfy the alignment and cache-coherency
 *         constraints of the target platform. This is especially important when
 *         buffers reside in external RAM (PSRAM), where DMA transfers require
 *         cache-line-aligned addresses and sizes. Use `esp_asrc_get_buffer_alignment`
 *         together with `esp_asrc_align_alloc` to allocate compliant buffers.
 */

/**
 * @brief  ASRC hardware configuration
 */
typedef esp_asrc_cfg_t esp_asrc_hw_cfg_t;

/**
 * @brief  ASRC hardware handle
 */
typedef esp_asrc_handle_t esp_asrc_hw_handle_t;

/**
 * @brief  Open ASRC hardware instance
 *
 * @param[in]   cfg     Pointer to ASRC configuration
 * @param[out]  handle  ASRC hardware handle
 *
 * @return
 *       - ESP_ASRC_ERR_OK           Success
 *       - ESP_ASRC_ERR_FAIL         Failed
 *       - ESP_ASRC_ERR_MEM_LACK     Memory allocation failed
 *       - ESP_ASRC_ERR_NOT_SUPPORT  Not supported
 */
esp_asrc_err_t esp_asrc_hw_open(esp_asrc_hw_cfg_t *cfg, esp_asrc_hw_handle_t *handle);

/**
 * @brief  Calculate expected output sample count
 *
 * @param[in]   handle           ASRC hardware handle
 * @param[in]   in_samples_cnt   Input sample count
 * @param[out]  out_samples_cnt  Pointer to receive output sample count
 *
 * @return
 *       - ESP_ASRC_ERR_OK    Success
 *       - ESP_ASRC_ERR_FAIL  Failed
 */
esp_asrc_err_t esp_asrc_hw_get_out_sample_num(esp_asrc_hw_handle_t handle, uint32_t in_samples_cnt, uint32_t *out_samples_cnt);

/**
 * @brief  Process audio samples through ASRC
 *
 * @param[in]   handle          ASRC hardware handle
 * @param[in]   in_samples      Input sample buffer
 * @param[in]   in_samples_num  Number of input samples
 * @param[out]  out_samples     Output sample buffer
 * @param[out]  out_sample_num  Pointer to receive actual output sample count
 *
 * @return
 *       - ESP_ASRC_ERR_OK                 Success
 *       - ESP_ASRC_ERR_FAIL               Failed
 *       - ESP_ASRC_ERR_TIMEOUT            Processing timeout
 *       - ESP_ASRC_ERR_INVALID_PARAMETER  Invalid argument
 *       - ESP_ASRC_ERR_INVALID_ALIGNMENT  Invalid buffer address or size alignment
 */
esp_asrc_err_t esp_asrc_hw_process(esp_asrc_hw_handle_t handle, uint8_t *in_samples, uint32_t in_samples_num,
                                   uint8_t *out_samples, uint32_t *out_sample_num);

/**
 * @brief  Close ASRC hardware instance
 *
 * @param[in]  handle  ASRC hardware handle
 */
void esp_asrc_hw_close(esp_asrc_hw_handle_t handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

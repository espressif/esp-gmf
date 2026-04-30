/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_asrc_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  ESP-ASRC (Audio Sample Rate Converter) is a high-performance audio format
 *         conversion module that supports sample rate, bit depth, and channel count
 *         conversion between different audio streams
 *
 *         The module adopts a hardware–software cooperative architecture. On chips
 *         equipped with an ASRC peripheral, it can automatically schedule hardware
 *         resources for low-latency, high-efficiency real-time conversion. When no
 *         hardware is available, an optimized software path is used instead. The
 *         performance mode is selected through `esp_asrc_perf_type_t` in the
 *         configuration structure
 *
 *         ASRC processing is based on sample frames. One sample frame contains all
 *         channels of a single sampling instant, and its byte size is:
 *         frame_bytes = channel * (bits_per_sample >> 3)
 *         The relationship between processing data length and sample count is:
 *         sample_num = data_length / frame_bytes
 *
 *         When the conversion ratio between source and destination sample rates is
 *         not an integer, the actual output sample count per call may vary slightly
 *         due to internal fractional accumulation. Use `esp_asrc_get_out_sample_num`
 *         to estimate the output count for proper buffer sizing
 *
 *         For hardware-accelerated paths with PSRAM buffers, input and output buffers
 *         must satisfy platform-specific alignment and cache-coherency constraints.
 *         Call `esp_asrc_get_buffer_alignment` to query alignment requirements and
 *         `esp_asrc_align_alloc` to allocate compliant buffers
 */

/**
 * @brief  Create an ASRC handle based on the provided configuration
 *
 * @param[in]   cfg     Pointer to the ASRC configuration
 * @param[out]  handle  The ASRC handle. If an error occurs, the result will be a NULL pointer
 *
 * @return
 *       - ESP_ASRC_ERR_OK                 Operation succeeded
 *       - ESP_ASRC_ERR_MEM_LACK           Fail to allocate memory
 *       - ESP_ASRC_ERR_INVALID_PARAMETER  Invalid input parameter
 *       - ESP_ASRC_ERR_NOT_SUPPORT        Unsupported configuration
 */
esp_asrc_err_t esp_asrc_open(esp_asrc_cfg_t *cfg, esp_asrc_handle_t *handle);

/**
 * @brief  Calculate the number of output samples required for a given number of input samples
 *
 * @param[in]   handle           The ASRC handle
 * @param[in]   in_samples_cnt   Number of input samples
 * @param[out]  out_samples_cnt  Pointer to store the number of output samples
 *
 * @return
 *       - ESP_ASRC_ERR_OK                 Operation succeeded
 *       - ESP_ASRC_ERR_INVALID_PARAMETER  Invalid input parameter
 */
esp_asrc_err_t esp_asrc_get_out_sample_num(esp_asrc_handle_t handle, uint32_t in_samples_cnt, uint32_t *out_samples_cnt);

/**
 * @brief  Get the bytes per sample for input and output audio formats
 *
 * @note  The `in_sample_bytes` and `out_sample_bytes` can be NULL
 *        When NULL is passed, the corresponding value will not be assigned
 *
 * @param[in]   handle            The ASRC handle
 * @param[out]  in_sample_bytes   Pointer to store input sample bytes
 * @param[out]  out_sample_bytes  Pointer to store output sample bytes
 *
 * @return
 *       - ESP_ASRC_ERR_OK                 Operation succeeded
 *       - ESP_ASRC_ERR_INVALID_PARAMETER  Invalid input parameter
 */
esp_asrc_err_t esp_asrc_get_bytes_per_sample(esp_asrc_handle_t handle, uint16_t *in_sample_bytes, uint16_t *out_sample_bytes);

/**
 * @brief  Query buffer alignment requirements for ASRC frame buffers
 *
 * @note  1. Call this API before `esp_asrc_align_alloc()` and pass the returned
 *        alignment fields to ensure the allocated buffers meet ASRC requirements
 *        2. Only user want to use the ASRC hardware and use the PSRAM, should call this API to get the alignment requirements
 *
 * @param[out]  buffer_alignment  Pointer to the structure used to store alignment requirements
 *
 * @return
 *       - ESP_ASRC_ERR_OK                 Success
 *       - ESP_ASRC_ERR_INVALID_PARAMETER  The `buffer_alignment` is NULL
 */
esp_asrc_err_t esp_asrc_get_buffer_alignment(esp_asrc_buffer_alignment_t *buffer_alignment);

/**
 * @brief  Allocate memory for ASRC
 *
 * @note  1. Hardware ASRC with buffers in PSRAM must meet platform address and size alignment (cache/DMA).
 *           Call `esp_asrc_get_buffer_alignment()` and pass the returned `inbuf_*` / `outbuf_*` values here so
 *           allocation matches the active path without duplicating alignment math
 *        2. Lifetime is owned by the caller: free the returned pointer when the buffer is no longer used
 *           (e.g. `free()`, or `heap_caps_free()` if your project uses the same heap helpers
 *           as this component)
 *
 * @param[in]   size            Requested buffer size in bytes
 * @param[in]   addr_align      Required address alignment in bytes
 * @param[in]   size_align      Size alignment granularity in bytes
 * @param[out]  allocated_size  Actual allocated size in bytes
 *                              May be larger than the requested `size` due to alignment adjustment
 *
 * @return
 *       - Pointer  Allocated memory pointer
 *       - NULL     Allocation failed
 */
void *esp_asrc_align_alloc(uint32_t size, uint32_t addr_align, uint32_t size_align, uint32_t *allocated_size);

/**
 * @brief  Process audio data through the ASRC
 *
 * @param[in]      handle          The ASRC handle
 * @param[in]      in_samples      Pointer to input audio samples
 *                                 Note: In hardware mode with PSRAM allocation, use cache-line-aligned buffers
 * @param[in]      in_samples_num  Number of input samples
 * @param[out]     out_samples     Pointer to output audio samples buffer
 *                                 Note: In hardware mode with PSRAM allocation, use cache-line-aligned buffers
 * @param[in,out]  out_sample_num  Input: maximum number of output samples that can be stored.
 *                                 Output: actual number of output samples generated
 *                                 When the sample rate ratio is not an integer, fractional residual samples are cached internally and flushed in subsequent calls
 *                                 The actual output count may vary slightly between calls
 *
 * @return
 *       - ESP_ASRC_ERR_OK                 Operation succeeded
 *       - ESP_ASRC_ERR_MEM_LACK           Fail to allocate memory
 *       - ESP_ASRC_ERR_TIMEOUT            Hardware ASRC process timeout
 *       - ESP_ASRC_ERR_INVALID_PARAMETER  Invalid input parameter
 *       - ESP_ASRC_ERR_INVALID_ALIGNMENT  Invalid buffer address or size alignment
 */
esp_asrc_err_t esp_asrc_process(esp_asrc_handle_t handle, uint8_t *in_samples, uint32_t in_samples_num,
                                uint8_t *out_samples, uint32_t *out_sample_num);

/**
 * @brief  Close the ASRC handle and free associated resources
 *
 * @param[in]  handle  The ASRC handle to close
 */
void esp_asrc_close(esp_asrc_handle_t handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

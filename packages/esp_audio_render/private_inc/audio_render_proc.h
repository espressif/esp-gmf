/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_audio_render_types.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define SAME_SAMPLE_INFO(from, to) \
    ((from).sample_rate == (to).sample_rate && (from).bits_per_sample == (to).bits_per_sample && (from).channel == (to).channel)

/**
 * @brief  Audio render processor handle
 */
typedef void *audio_render_proc_handle_t;

/**
 * @brief  Create audio render processor
 *
 * @param[in]   pool  GMF pool
 * @param[out]  proc  Audio render processor handle to store
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK           On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_AUDIO_RENDER_ERR_NO_MEM       Not enough memory
 */
esp_audio_render_err_t audio_render_proc_create(esp_gmf_pool_handle_t pool, audio_render_proc_handle_t *proc);

/**
 * @brief  Set buffer alignment for audio render processor
 *
 * @param[in]  proc       Audio processor handle
 * @param[in]  buf_align  Audio render processor handle to store
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK           On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_AUDIO_RENDER_ERR_NO_MEM       Not enough memory
 */
esp_audio_render_err_t audio_render_proc_set_buf_align(audio_render_proc_handle_t proc, uint8_t buf_align);

/**
 * @brief  Add audio processor
 *
 * @param[in]  proc       Audio processor handle
 * @param[in]  proc_type  Array of processor type
 * @param[in]  proc_num   Processor number
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK             On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_AUDIO_RENDER_ERR_NO_MEM         Not enough memory
 *       - ESP_AUDIO_RENDER_ERR_INVALID_STATE  Not allowed to add when running
 */
esp_audio_render_err_t audio_render_proc_add(audio_render_proc_handle_t proc, esp_audio_render_proc_type_t *proc_type,
                                             uint8_t proc_num);

/**
 * @brief  Open audio processor
 *
 * @param[in]  proc             Audio processor handle
 * @param[in]  in_sample_info   Input sample information
 * @param[in]  out_sample_info  Output sample information
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK             On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_AUDIO_RENDER_ERR_NO_RESOURCE    Not enough resource
 *       - ESP_AUDIO_RENDER_ERR_INVALID_STATE  Not allowed to add when running
 */
esp_audio_render_err_t audio_render_proc_open(audio_render_proc_handle_t proc,
                                              esp_audio_render_sample_info_t *in_sample_info,
                                              esp_audio_render_sample_info_t *out_sample_info);

/**
 * @brief  Get element handle from audio processor by processor type
 *
 * @param[in]  proc   Audio processor handle
 * @param[in]  type   Audio processor type
 *
 * @return
 *       - NULL    Element not existed
 *       - Others  Audio processor element handle
 */
esp_gmf_element_handle_t audio_render_proc_get_element(audio_render_proc_handle_t proc, esp_audio_render_proc_type_t type);

/**
 * @brief  Set output writer for audio processor
 *
 * @param[in]  proc    Audio processor handle
 * @param[in]  writer  Output writer
 * @param[in]  ctx     Writer context
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK           On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_audio_render_err_t audio_render_proc_set_writer(audio_render_proc_handle_t proc, esp_audio_render_write_cb_t writer, void *ctx);

/**
 * @brief  Write PCM data into audio processor
 *
 * @note  Currently all processor runs in input context
 *
 * @param[in]  proc      Audio processor handle
 * @param[in]  pcm_data  PCM data to write
 * @param[in]  pcm_size  PCM data size
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK             On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_AUDIO_RENDER_ERR_INVALID_STATE  Not allowed to add when running
 *       - ESP_AUDIO_RENDER_ERR_FAIL           Failed to process data
 */
esp_audio_render_err_t audio_render_proc_write(audio_render_proc_handle_t proc, uint8_t *pcm_data, uint32_t pcm_size);

/**
 * @brief  Close audio processor
 *
 * @param[in]  proc  Audio processor handle
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK           On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_audio_render_err_t audio_render_proc_close(audio_render_proc_handle_t proc);

/**
 * @brief  Destroy audio processor
 *
 * @param[in]  proc  Audio processor handle
 */
void audio_render_proc_destroy(audio_render_proc_handle_t proc);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

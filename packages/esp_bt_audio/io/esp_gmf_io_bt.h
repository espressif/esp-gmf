/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_gmf_io.h"
#include "esp_bt_audio_stream.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Bluetooth I/O configuration structure
 */
typedef struct {
    esp_gmf_io_dir_t              dir;     /*!< I/O direction */
    esp_bt_audio_stream_handle_t  stream;  /*!< Bluetooth stream handle */
} bt_io_cfg_t;

/**
 * @brief  Default Bluetooth I/O configuration
 */
#define ESP_GMF_BT_IO_CFG_DEFAULT()  {  \
    .dir    = ESP_GMF_IO_DIR_NONE,      \
    .stream = NULL,                     \
}

/**
 * @brief  Initialize Bluetooth I/O
 *
 * @param[in]   config  Pointer to I/O configuration
 * @param[out]  io      Pointer to store the initialized I/O handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_MEMORY_LACK  Memory allocation failure
 *       - ESP_GMF_ERR_NOT_SUPPORT  Not supported
 */
esp_gmf_err_t esp_gmf_io_bt_init(bt_io_cfg_t *config, esp_gmf_io_handle_t *io);

/**
 * @brief  Set the Bluetooth stream for the I/O
 *
 * @param[in]  io      I/O handle
 * @param[in]  stream  Bluetooth stream handle
 *
 * @return
 *       - ESP_GMF_ERR_OK             On success
 *       - ESP_GMF_ERR_FAIL           On failure
 *       - ESP_GMF_ERR_INVALID_ARG    Invalid argument
 *       - ESP_GMF_ERR_INVALID_STATE  Invalid state
 */
esp_gmf_err_t esp_gmf_io_bt_set_stream(esp_gmf_io_handle_t io, esp_bt_audio_stream_handle_t stream);

/**
 * @brief  Discard writer payloads instead of sending them to a Bluetooth stream
 *
 * @note  Enable discard before opening a writer that has no stream, and to keep a running
 *        writer alive once its stream is gone. To attach a live stream while the pipeline
 *        is running, call esp_gmf_io_bt_set_stream first, then disable discard.
 *
 * @param[in]  io      I/O handle
 * @param[in]  enable  true to discard writes, false to use the bound stream
 *
 * @return
 *       - ESP_GMF_ERR_OK             On success
 *       - ESP_GMF_ERR_INVALID_ARG    Invalid argument
 *       - ESP_GMF_ERR_INVALID_STATE  Invalid state
 */
esp_gmf_err_t esp_gmf_io_bt_set_discard(esp_gmf_io_handle_t io, bool enable);

/**
 * @brief  Pace discarded writer payloads at one frame per frame period
 *
 * @note  A discarding writer otherwise consumes its input as fast as it can, which puts its
 *        PCM position ahead of the writers that a live stream throttles. Set the audio frame
 *        duration on every writer of a set that must stay in sync, so a writer without a
 *        stream still consumes in real time.
 *
 * @param[in]  io        I/O handle
 * @param[in]  frame_us  Frame duration in us, 0 to consume as fast as possible
 *
 * @return
 *       - ESP_GMF_ERR_OK             On success
 *       - ESP_GMF_ERR_INVALID_ARG    Invalid argument
 *       - ESP_GMF_ERR_INVALID_STATE  Invalid state
 */
esp_gmf_err_t esp_gmf_io_bt_set_discard_pace(esp_gmf_io_handle_t io, uint32_t frame_us);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

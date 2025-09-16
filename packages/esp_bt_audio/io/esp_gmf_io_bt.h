/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

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

#ifdef __cplusplus
}
#endif  /* __cplusplus */

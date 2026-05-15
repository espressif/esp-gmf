/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_err.h"
#include "esp_bt_audio_stream.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Structure for Classic Bluetooth stream node
 */
typedef struct _classic_stream {
    esp_bt_audio_stream_base_t  base;         /*!< Base stream information */
    uint32_t                    conn_handle;  /*!< Connection handle */
} bt_audio_classic_stream_t;

/**
 * @brief  Create a Classic Bluetooth stream node
 *
 * @param[out]  stream  Returned stream node handle
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid parameter
 *       - ESP_ERR_NO_MEM       No memory to create stream
 */
esp_err_t bt_audio_classic_stream_create(bt_audio_classic_stream_t **stream);

/**
 * @brief  Destroy a Classic Bluetooth stream node
 *
 * @param[in]  stream  Pointer to the stream node to destroy
 */
void bt_audio_classic_stream_destroy(bt_audio_classic_stream_t *stream);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

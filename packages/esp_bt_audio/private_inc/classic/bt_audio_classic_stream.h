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
 * @brief  Classic Bluetooth stream data buffer operations
 */
typedef struct {
    void      *(*audio_buff_alloc)(uint32_t size);                       /*!< Allocate an audio buffer */
    void       (*audio_buff_free)(void *audio_buf);                      /*!< Free an audio buffer */
    uint8_t   *(*audio_buff_get_data)(void *audio_buf);                  /*!< Get payload pointer from an audio buffer */
    void       (*audio_buff_set_data_len)(void *audio_buf, uint32_t len); /*!< Set payload length */
    esp_err_t  (*audio_data_send)(uint32_t conn_handle, void *audio_buf); /*!< Send an audio buffer */
} bt_audio_classic_stream_data_ops_t;

#if CONFIG_BT_HFP_CLIENT_ENABLE
extern const bt_audio_classic_stream_data_ops_t bt_audio_hfp_client_stream_data_ops;
#endif  /* CONFIG_BT_HFP_CLIENT_ENABLE */

#if CONFIG_BT_HFP_AG_ENABLE
extern const bt_audio_classic_stream_data_ops_t bt_audio_hfp_ag_stream_data_ops;
#endif  /* CONFIG_BT_HFP_AG_ENABLE */

/**
 * @brief  Structure for Classic Bluetooth stream node
 */
typedef struct _classic_stream {
    esp_bt_audio_stream_base_t                  base;         /*!< Base stream information */
    uint32_t                                    conn_handle;  /*!< Connection handle */
    const bt_audio_classic_stream_data_ops_t   *data_ops;    /*!< Profile-specific data buffer operations */
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

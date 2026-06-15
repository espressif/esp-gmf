/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_bt_audio_stream.h"
#include "esp_ble_audio_bap_api.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  LE Audio stream base structure.
 */
typedef struct {
    esp_bt_audio_stream_base_t  base;                /*!< Public stream base */
    esp_ble_audio_bap_stream_t  bap_stream;          /*!< BAP stream object */
    uint32_t                    presentation_delay;  /*!< Presentation delay in us */
    uint16_t                    seq;                 /*!< ISO packet sequence */
    uint16_t                    max_sdu;             /*!< Max SDU size from QoS */
    bool                        first_packet;        /*!< First RX packet flag */
    volatile bool               started;             /*!< Stream has entered started state */
    uint16_t                    iso_interval;        /*!< ISO interval in 125 us units, 0 if unavailable */
} bt_audio_le_stream_t;

/**
 * @brief  Allocate an LE stream object and register BAP stream callbacks.
 *
 * @param[out]  stream  Receives the new stream pointer; set to NULL on error.
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  If stream is NULL
 *       - ESP_ERR_NO_MEM       If allocation fails
 *       - Other                non-zero codes from esp_ble_audio_bap_stream_cb_register
 */
esp_err_t bt_audio_le_stream_create(bt_audio_le_stream_t **stream);

/**
 * @brief  Destroy a stream, drain its queue, and free codec configuration memory.
 *
 * @param[in]  stream  Stream returned by @ref bt_audio_le_stream_create (may be NULL).
 */
void bt_audio_le_stream_destroy(bt_audio_le_stream_t *stream);

/**
 * @brief  Locate the LE stream wrapper for a BAP stream pointer.
 *
 * @param[in]   bap_stream  Registered BAP stream.
 * @param[out]  stream      Receives the LE stream wrapper.
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  If a pointer argument is NULL
 */
esp_err_t bt_audio_le_stream_find_by_bap_stream(esp_ble_audio_bap_stream_t *bap_stream, bt_audio_le_stream_t **stream);

/**
 * @brief  Notify the user pipeline that stream resources are allocated (codec filled).
 *
 * @param[in]  stream  Target stream.
 */
void bt_audio_le_stream_dispatch_allocated(bt_audio_le_stream_t *stream);

/**
 * @brief  Dispatch stream state change to the event hub.
 *
 * @param[in]  stream  Target stream.
 * @param[in]  state   New @c esp_bt_audio_stream_state_t value.
 */
void bt_audio_le_stream_dispatch_state(bt_audio_le_stream_t *stream, esp_bt_audio_stream_state_t state);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

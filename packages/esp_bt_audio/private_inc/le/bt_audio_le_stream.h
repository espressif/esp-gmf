/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_ble_audio_bap_api.h"
#if CONFIG_BT_CAP
#include "esp_ble_audio_cap_api.h"
#endif  /* CONFIG_BT_CAP */

#include "esp_bt_audio_stream.h"

#if CONFIG_BT_CAP
_Static_assert(offsetof(esp_ble_audio_cap_stream_t, bap_stream) == 0,
               "CAP stream must embed BAP stream as its first member");
_Static_assert(sizeof(esp_ble_audio_cap_stream_t) >= sizeof(esp_ble_audio_bap_stream_t),
               "CAP stream must contain a complete BAP stream");
#endif  /* CONFIG_BT_CAP */

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define BT_AUDIO_LE_TX_TASK_STACK_SIZE_DEFAULT  4096
#define BT_AUDIO_LE_TX_TASK_PRIO_DEFAULT        16
#define BT_AUDIO_LE_TX_TASK_CORE_ID_DEFAULT     0

/*!< Transmit group that paces all source streams of one CIG or BIG together */
typedef struct bt_audio_le_tx_group bt_audio_le_tx_group_t;

typedef struct bt_audio_le_stream bt_audio_le_stream_t;

/**
 * @brief  Callback when a BAP stream has completed the Release operation.
 *
 * @param[in]  stream    Released stream.
 * @param[in]  user_ctx  Context supplied when the callback was registered.
 */
typedef void (*bt_audio_le_stream_released_cb_t)(bt_audio_le_stream_t *stream, void *user_ctx);

/**
 * @brief  LE Audio stream base structure.
 */
struct bt_audio_le_stream {
    esp_bt_audio_stream_base_t  base;               /*!< Public stream base */
    union {
        esp_ble_audio_bap_stream_t  bap_stream;     /*!< BAP stream object */
#if CONFIG_BT_CAP
        esp_ble_audio_cap_stream_t  cap_stream;     /*!< CAP stream object; overlays bap_stream */
#endif  /* CONFIG_BT_CAP */
    };
    uint32_t                    presentation_delay; /*!< Presentation delay in us */
    bool                        first_packet;       /*!< First RX packet flag */
    uint32_t                    iso_interval;       /*!< Controller ISO interval in us */
    uint32_t                    sdu_interval_us;    /*!< QoS SDU interval in us */
    bt_audio_le_tx_group_t      *tx_group;          /*!< Group that paces this source stream */
    volatile uint8_t            tx_state;           /*!< Member state, a @c bt_audio_le_tx_state_t value */
    uint16_t                    tx_last_seq;        /*!< Last seq successfully submitted to the controller */
    bool                        tx_need_burst;      /*!< Controller likely has no queued SDU; send two */
    SemaphoreHandle_t           tx_credits;         /*!< Free slots left in the TX queue */
    uint8_t                     tx_task_core_id;    /*!< Source pacing task core ID */
    uint8_t                     tx_task_prio;       /*!< Source pacing task priority */
    uint32_t                    tx_task_stack_size; /*!< Source pacing task stack size in bytes */
    char                        tx_name[configMAX_TASK_NAME_LEN]; /*!< Source pacing task and timer name */
    bt_audio_le_stream_released_cb_t released_cb;   /*!< Optional release-complete observer */
    void                            *released_ctx;  /*!< Observer context */
};

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

#if CONFIG_BT_CAP
/**
 * @brief  Register the common LE stream callbacks through a CAP stream.
 *
 * @param[in]  stream  Stream returned by @ref bt_audio_le_stream_create.
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  If stream is NULL
 *       - Others               Failure codes from the CAP stream registration API
 */
esp_err_t bt_audio_le_stream_register_cap_ops(bt_audio_le_stream_t *stream);
#endif  /* CONFIG_BT_CAP */

/**
 * @brief  Register an observer for completion of the BAP Release operation.
 *
 * @param[in]  stream    Target stream.
 * @param[in]  callback  Observer callback, or NULL to unregister.
 * @param[in]  user_ctx  Context passed to @p callback.
 */
void bt_audio_le_stream_set_released_cb(bt_audio_le_stream_t *stream,
                                        bt_audio_le_stream_released_cb_t callback,
                                        void *user_ctx);

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

/**
 * @brief  Configure the LE source send task for one stream.
 *
 * @param[in]  stream      Stream returned by @ref bt_audio_le_stream_create
 * @param[in]  core_id     Core ID, 0 or 1
 * @param[in]  prio        Task priority, less than 24
 * @param[in]  stack_size  Stack size in bytes, must be greater than 0
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  If stream is NULL or a parameter is out of range
 */
esp_err_t bt_audio_le_stream_set_tx_task_cfg(bt_audio_le_stream_t *stream, uint8_t core_id, uint8_t prio, uint32_t stack_size);

/**
 * @brief  Free the payload of a stream packet and clear its descriptor.
 *
 * @param[in,out]  packet  Packet to release.
 */
void bt_audio_le_stream_release_packet(esp_bt_audio_stream_packet_t *packet);

/**
 * @brief  Drop every packet still sitting in the stream queue.
 *
 * @param[in]  stream  Target stream (may be NULL).
 */
void bt_audio_le_stream_flush_queue(bt_audio_le_stream_t *stream);

/**
 * @brief  Submit one SDU on a source stream and record its sequence number.
 *
 * @param[in]  stream  Source stream.
 * @param[in]  data    SDU payload.
 * @param[in]  size    SDU size in bytes.
 * @param[in]  seq     Packet sequence number assigned by the TX group.
 * @param[in]  use_ts  True to send with an explicit CIG/BIG timestamp.
 * @param[in]  ts      Timestamp used when @p use_ts is true.
 *
 * @return
 *       - ESP_OK               The SDU was submitted and @c tx_last_seq was updated
 *       - ESP_ERR_INVALID_ARG  If stream or data is NULL
 *       - Others               Failure codes from the BAP send API
 */
esp_err_t bt_audio_le_stream_tx_send(bt_audio_le_stream_t *stream, const uint8_t *data,
                                     uint16_t size, uint16_t seq, bool use_ts, uint32_t ts);

/**
 * @brief  Clear the last submitted sequence number before a stream starts or stops.
 *
 * @param[in]  stream  Target stream (may be NULL).
 */
void bt_audio_le_stream_tx_reset(bt_audio_le_stream_t *stream);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

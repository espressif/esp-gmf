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

#include "bt_audio_le_stream.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/*!< Maximum ISO channels allowed by the BAP broadcast-source configuration */
#define BT_AUDIO_LE_TX_GROUP_MAX_MEMBERS  31

/**
 * @brief  Transmit state of one group member.
 *
 *         Each tick uses one shared sequence number. A member that has an SDU sends it with
 *         that sequence number; a member that does not is skipped. Late or restarted members
 *         drop queued SDUs from before the current sequence number and wait for a fresh write.
 */
typedef enum {
    BT_AUDIO_LE_TX_STATE_IDLE = 0,  /*!< Not paced, outgoing SDUs are dropped on the spot */
    BT_AUDIO_LE_TX_STATE_SHADOW,    /*!< Paced but muted, the dequeued SDU is dropped instead of sent */
    BT_AUDIO_LE_TX_STATE_ACTIVE,    /*!< Transmitting one SDU per tick when the queue has one */
} bt_audio_le_tx_state_t;

/**
 * @brief  Create a transmit group for the streams of one CIG or BIG.
 *
 * @param[in]   name       Name used for the pacing task and timer (may be NULL).
 * @param[out]  out_group  Receives the new group; set to NULL on error.
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  If out_group is NULL
 *       - ESP_ERR_NO_MEM       If allocation fails
 */
esp_err_t bt_audio_le_tx_group_create(const char *name, bt_audio_le_tx_group_t **out_group);

/**
 * @brief  Stop pacing, release the group resources, and detach all members.
 *
 * @param[in]  group  Group to destroy (may be NULL).
 */
void bt_audio_le_tx_group_destroy(bt_audio_le_tx_group_t *group);

/**
 * @brief  Add a source stream to a group.
 *
 * @param[in]  group   Target group.
 * @param[in]  stream  Stream to pace with the rest of the group.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    If a pointer argument is NULL
 *       - ESP_ERR_INVALID_STATE  If the stream already belongs to a group
 *       - ESP_ERR_NO_MEM         If the group is full or the credit counter cannot be created
 */
esp_err_t bt_audio_le_tx_group_add(bt_audio_le_tx_group_t *group, bt_audio_le_stream_t *stream);

/**
 * @brief  Configure the group pacing task.
 *
 * @param[in]  group       Target group.
 * @param[in]  core_id     Core ID, 0 or 1.
 * @param[in]  prio        Task priority, less than 24.
 * @param[in]  stack_size  Stack size in bytes, must be greater than 0.
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  If group is NULL or a parameter is out of range
 */
esp_err_t bt_audio_le_tx_group_set_task_cfg(bt_audio_le_tx_group_t *group, uint8_t core_id,
                                            uint8_t prio, uint32_t stack_size);

/**
 * @brief  Report that a member CIS became live.
 *
 *         The member starts the group clock when it is the first one, otherwise it joins
 *         immediately as ACTIVE and follows the current sequence number. The caller must
 *         have emptied the member queue first so leftover SDUs from before this sequence
 *         number are not transmitted.
 *
 * @param[in]  stream  Member stream whose CIS is live.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    If stream is NULL
 *       - ESP_ERR_INVALID_STATE  If no SDU interval is known yet
 *       - Others                 Failure codes from task, timer, or event group creation
 */
esp_err_t bt_audio_le_tx_group_member_started(bt_audio_le_stream_t *stream);

/**
 * @brief  Report that a member CIS is gone.
 *
 *         The member keeps being paced (and muted) while others still transmit, so its
 *         producer stays at the SDU rate. The next start flushes leftover SDUs and
 *         follows the current sequence number.
 *
 * @param[in]  stream  Member stream whose CIS is gone.
 *
 * @return
 *       - true   The member is still paced and its queue must be kept
 *       - false  The member went idle and its queue should be flushed
 */
bool bt_audio_le_tx_group_member_stopped(bt_audio_le_stream_t *stream);

/**
 * @brief  Remove a stream from its group and free its credit counter.
 *
 * @param[in]  stream  Member stream (may be NULL or groupless).
 */
void bt_audio_le_tx_group_member_detach(bt_audio_le_stream_t *stream);

/**
 * @brief  Reserve room for one SDU in a member queue.
 *
 *         Credits cap the queued SDUs of every member to the same target depth, which is
 *         what gives all members of a group the same transmit latency.
 *
 * @param[in]  stream   Member stream.
 * @param[in]  wait_ms  Time to wait for a free slot.
 *
 * @return
 *       - ESP_OK           A slot was reserved
 *       - ESP_ERR_TIMEOUT  No slot became free in time
 */
esp_err_t bt_audio_le_tx_group_credit_take(bt_audio_le_stream_t *stream, uint32_t wait_ms);

/**
 * @brief  Return a credit that was reserved but not used.
 *
 * @param[in]  stream  Member stream.
 */
void bt_audio_le_tx_group_credit_give(bt_audio_le_stream_t *stream);

/**
 * @brief  Restore all credits of a member, to be called after its queue was flushed.
 *
 * @param[in]  stream  Member stream.
 */
void bt_audio_le_tx_group_credit_reset(bt_audio_le_stream_t *stream);

/**
 * @brief  Run the pacing tick early while the group is still prefilling.
 *
 * @param[in]  stream  Member stream that just queued an SDU.
 */
void bt_audio_le_tx_group_kick(bt_audio_le_stream_t *stream);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

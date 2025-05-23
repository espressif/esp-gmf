/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Message queue handle
 */
typedef struct msg_q_t *msg_q_handle_t;

/**
 * @brief  Create message queue
 *
 * @param[in]   msg_number  The maximum number of messages in the queue
 * @param[out]  msg_size    Message size
 *
 * @return
 *       - NULL    No resource to create message queue
 *       - Others  Message queue handle
 */
msg_q_handle_t msg_q_create(int msg_number, int msg_size);

/**
 * @brief  Send message to queue
 *
 * @param[in]  q     Message queue handle
 * @param[in]  msg   Message to be inserted into queue
 * @param[in]  size  Message size, need not larger than msg_size when created
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int msg_q_send(msg_q_handle_t q, void *msg, int size);

/**
 * @brief  Receive message from queue
 *
 * @param[in]   q        Message queue handle
 * @param[out]  msg      Message to be inserted into queue
 * @param[in]   size     Message size, need not larger than msg_size when created
 * @param[in]   no_wait  If true, return immediately if no message in queue
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 *       - 1   If no message in queue and no_wait is true
 */
int msg_q_recv(msg_q_handle_t q, void *msg, int size, bool no_wait);

/**
 * @brief  Get items number in message queue
 *
 * @param[in]  q  Message queue handle
 *
 * @return
 *       - 0       No message in queue
 *       - Others  Current queued items number
 */
int msg_q_number(msg_q_handle_t q);

/**
 * @brief  Wakeup message queue
 *
 * @param[in]  q  Message queue handle
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int msg_q_wakeup(msg_q_handle_t q);

/**
 * @brief  Destroy message queue
 *
 * @param[in]  q  Message queue handle
 */
void msg_q_destroy(msg_q_handle_t q);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "msg_q.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Shared queue get frame data callback
 */
typedef void *(*q_get_frame_data_cb)(void *item);

/**
 * @brief  Shared queue item release callback
 */
typedef int (*q_release_frame_cb)(void *item, void *ctx);

/**
 * @brief  Shared queue configuration
 */
typedef struct {
    uint8_t              user_count;      /*!< Output user counts */
    uint8_t              q_count;         /*!< Maximum queue count for output user */
    int                  item_size;       /*!< Item size to fill into queue */
    q_get_frame_data_cb  get_frame_data;  /*!< Function to get frame data (used to distinguish frame) */
    q_release_frame_cb   release_frame;   /*!< Function to release frame */
    void                *ctx;             /*!< Input context for release frame */
    bool                 use_external_q;  /*!< Whether use external queue */
} share_q_cfg_t;

/**
 * @brief  Shared queue handle
 *
 * @note  This shared queue is designed for distributing frame data. It has one input
 *        and multiple output consumers. The data is shared by reference and is only
 *        released when all consumers have finished using the frame. When input data
 *        arrives, the frame is pushed to all active output queues. Each consumer retrieves
 *        frame data from the queue and releases it when done. The shared queue tracks
 *        the release actions of consumers and uses a reference count to determine when
 *        to release the actual frame data
 */
typedef struct share_q_t *share_q_handle_t;

/**
 * @brief  Create share queue
 *
 * @param[in]  cfg  Share queue configuration
 *
 * @return
 *       - NULL    No resources for share queue
 *       - Others  Share queue handle
 */
share_q_handle_t share_q_create(share_q_cfg_t *cfg);

/**
 * @brief  Set external queue for share queue by index
 *
 * @param[in]  q       Share queue handle
 * @param[in]  index   Index of the queue
 * @param[in]  handle  Message queue handle
 *
 * @return
 *       - 0   On success
 *       - -1  Invalid input arguments
 */
int share_q_set_external(share_q_handle_t q, uint8_t index, msg_q_handle_t handle);

/**
 * @brief  Set release API for certain index
 *
 * @note  Specially used by one queue used for multiple work
 *
 * @param[in]  q       Shared queue handle
 * @param[in]  index   Output index
 * @param[in]  enable  Enable or disable
 *
 * @return
 *       - 0   On success
 *       - -1  Invalid input arguments
 */
int share_q_set_user_release(share_q_handle_t q, uint8_t index, q_release_frame_cb release_cb, void *ctx);

/**
 * @brief  Receive frame from share queue by index
 *
 * @param[in]   q      Share queue handle
 * @param[in]   index  Index of the queue
 * @param[out]  frame  Frame information to be filled
 *
 * @return
 *       - 0   On success
 *       - -1  Invalid input arguments
 */
int share_q_recv(share_q_handle_t q, uint8_t index, void *frame);

/**
 * @brief  Receive all frames from share queue and release input frame accordingly
 *
 * @param[in]   q      Share queue handle
 * @param[out]  frame  Frame information to be filled
 *
 * @return
 *       - 0   On success
 *       - -1  Invalid input arguments
 */
int share_q_recv_all(share_q_handle_t q, void *frame);

/**
 * @brief  Enable or disable shared queue output by index
 *
 * @note  Enabling or disabling can happen at any time. When disabled, input frames
 *        will not be inserted into the queue of the specified output index
 *
 * @param[in]  q       Shared queue handle
 * @param[in]  index   Output index
 * @param[in]  enable  Enable or disable
 *
 * @return
 *       - 0   On success
 *       - -1  Invalid input arguments
 */
int share_q_enable(share_q_handle_t q, uint8_t index, bool enable);

/**
 * @brief  Enable or disable shared queue output once by index
 *
 * @note  When enable once is set, after add one frame to the port , it won't add more frames into the port anymore
 *        Once disabled, it runs in normal and output data to port in continuous mode
 *
 * @param[in]  q       Shared queue handle
 * @param[in]  index   Output index
 * @param[in]  enable  Whether enable fetch once
 *
 * @return
 *       - 0   On success
 *       - -1  Invalid input arguments
 */
int share_q_enable_once(share_q_handle_t q, uint8_t index, bool enable);

/**
 * @brief  Check whether output queue of the specified index is enabled
 *
 * @param[in]  q      Shared queue handle
 * @param[in]  index  Output index
 *
 * @return
 *       - true   Queue of the specified index is enabled
 *       - false  Queue of the specified index is disabled
 */
bool share_q_is_enabled(share_q_handle_t q, uint8_t index);

/**
 * @brief  Add frame into share queue
 *
 * @param[in]  q     Shared queue handle
 * @param[in]  item  Frame to be inserted into queues
 *
 * @return
 *       - 0   On success
 *       - -1  Fail to add frame
 */
int share_q_add(share_q_handle_t q, void *item);

/**
 * @brief  Release frame
 *
 * @param[in]  q     Shared queue handle
 * @param[in]  item  Frame to be released
 *
 * @return
 *       - 0   On success
 *       - -1  Fail to release frame
 */
int share_q_release(share_q_handle_t q, void *item);

/**
 * @brief  Wait share queue to be empty
 *
 * @param[in]  q  Shared queue handle
 *
 * @return
 *       - 0   On success
 *       - -1  Fail to wait
 */

int share_q_wait_empty(share_q_handle_t q);

/**
 * @brief  Destroy share queue
 *
 * @param[in]  q  Shared queue handle
 */
void share_q_destroy(share_q_handle_t q);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

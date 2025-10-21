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
 * @brief  Struct for data queue
 *         Data queue works like a queue, you can receive the exact size of data as you send previously
 *         It allows you to get continuous buffer so that no need to care ring back issue
 *         It adds a fill_end member to record fifo write end position before ring back
 */
typedef struct {
    void  *buffer;        /*!< Buffer for queue */
    int    size;          /*!< Buffer size */
    int    fill_end;      /*!< Buffer write position before ring back */
    int    last_fill_end; /*!< Memorized last fill end for rewind */
    int    fixed_wr_wize; /*!< Fixed write size */
    int    wp;            /*!< Write pointer */
    int    rp;            /*!< Read pointer */
    int    filled;        /*!< Buffer filled size */
    int    user;          /*!< Buffer reference by reader or writer */
    int    quit;          /*!< Buffer quit flag */
    void  *lock;          /*!< Protect lock */
    void  *write_lock;    /*!< Write lock to let only one writer at same time */
    void  *event;         /*!< Event group to wake up reader or writer */
} data_q_t;

/**
 * @brief  Initialize data queue
 *
 * @param[in]  size  Buffer size for data queue
 *
 * @return
 *       - NULL    Fail to initialize queue
 *       - Others  Data queue instance
 */
data_q_t *data_q_init(int size);

/**
 * @brief  Wakeup thread which wait on queue data
 *
 * @param[in]  q  Data queue instance
 */
void data_q_wakeup(data_q_t *q);

/**
 * @brief  Deinitialize data queue
 *
 * @param[in]  q  Data queue instance
 */
void data_q_deinit(data_q_t *q);

/**
 * @brief  Get continuous buffer from data queue
 *
 * @param[in]  q     Data queue instance
 * @param[in]  size  Buffer size want to get
 *
 * @return
 *       - NULL    Fail to get buffer
 *       - Others  Pointer to got buffer data
 */
void *data_q_get_buffer(data_q_t *q, int size);

/**
 * @brief  Get data pointer being written but not send yet
 *
 * @param[in]  q  Data queue instance
 *
 * @return
 *       - NULL    Fail to get write buffer
 *       - Others  Write data pointer
 */
void *data_q_get_write_data(data_q_t *q);

/**
 * @brief  Send data into data queue
 *
 * @param[in]  q     Data queue instance
 * @param[in]  size  Buffer size want to get
 * @return
 *       - 0       On success
 *       - Others  Fail to send buffer
 */
int data_q_send_buffer(data_q_t *q, int size);

/**
 * @brief  Read data from data queue, and add reference count
 *
 * @param[in]   q       Data queue instance
 * @param[out]  buffer  Buffer in front of queue, this buffer is always valid before call `data_q_read_unlock`
 * @param[out]  size    Buffer size in front of queue
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to lock reader
 */
int data_q_read_lock(data_q_t *q, void **buffer, int *size);

/**
 * @brief  Release data be read and decrease reference count
 *
 * @param[in]   q       Data queue instance
 * @param[out]  buffer  Buffer in front of queue
 * @param[out]  size    Buffer size in front of queue
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to unlock reader
 */
int data_q_read_unlock(data_q_t *q);

/**
 * @brief  Rewind and send from old position
 *
 * @note  Only support when write in fixed size
 *        It will move read pointer to previous blocks, if write blocks less than setting blocks will rewind to write head
 *
 * @param[in]  q       Data queue instance
 * @param[in]  blocks  Rewind blocks
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to rewind
 */
int data_q_rewind(data_q_t *q, int blocks);

/**
 * @brief  Peak data unlock, call `data_q_read_lock` to read data with block
 *         After peek data, not consume the data and release the lock
 *
 * @param[in]  q  Data queue instance
 * @return
 *       - 0       On success
 *       - Others  Fail to peek data
 */
int data_q_peek_unlock(data_q_t *q);

/**
 * @brief  Consume all data in queue
 *
 * @param[in]  q  Data queue instance
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to consume
 */
int data_q_consume_all(data_q_t *q);

/**
 * @brief  Check whether there are filled data in queue
 *
 * @param[in]  q  Data queue instance
 *
 * @return
 *       - true   Have data in queue
 *       - false  Empty, no buffer filled
 */
bool data_q_have_data(data_q_t *q);

/**
 * @brief  Query data queue information
 *
 * @param[in]   q       Data queue instance
 * @param[out]  q_num   Data block number in queue
 * @param[out]  q_size  Total data size kept in queue
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to query
 */
int data_q_query(data_q_t *q, int *q_num, int *q_size);

/**
 * @brief  Query available data size
 *
 * @param[in]  q  Data queue instance
 *
 * @return
 *       - Available  size for write queue
 */
int data_q_get_avail(data_q_t *q);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

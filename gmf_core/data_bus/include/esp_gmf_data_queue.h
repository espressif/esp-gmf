/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_gmf_data_bus.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define ESP_GMF_DATA_QUEUE_NO_WAIT       (0)
#define ESP_GMF_DATA_QUEUE_WAIT_FOREVER  (UINT32_MAX)

/**
 * @brief  GMF data queue is a FIFO queue for frame-based, copy-free transfer between tasks.
 *         One push is one frame. The reader always receives the exact frame that was pushed,
 *         in first-in-first-out order.
 *
 *         The queue behaves like a classic queue: frames are queued on write and dequeued
 *         on read. It is thread-safe and supports multiple writer tasks and one reader task.
 *         Acquire can block, time out, or be aborted by `esp_gmf_data_queue_wakeup`.
 *         `esp_gmf_data_queue_rewind` can replay fixed-size frames
 *         `esp_gmf_data_queue_peek_release` can peek data but not consume
 *
 *         It acts like a queue, which suitable to store metadata only or payload together with metadata
 *
 *         Two usage styles are provided.
 *         - The direct APIs let the application define the frame layout in the acquired buffer
 *         - The `esp_gmf_data_queue_db_*` APIs provides unified interface for GMF pipeline
 *
 *         Typical use:
 *           Passing video or audio variable-sized frames between capture, encode, mux, or render
 */

/**
 * @brief  Data queue handle
 */
typedef struct esp_gmf_data_queue esp_gmf_data_queue_t;

/**
 * @brief  Data queue bus handle
 */
typedef struct esp_gmf_data_queue_bus esp_gmf_data_queue_bus_t;

/**
 * @brief  Initialize data queue
 *
 * @param[in]  size  Buffer size for data queue
 *
 * @return
 *       - NULL    Fail to initialize queue
 *       - Others  Data queue instance
 */
esp_gmf_data_queue_t *esp_gmf_data_queue_create(int size);

/**
 * @brief  Wakeup thread which wait on queue data
 *
 * @param[in]  queue  Data queue instance
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to wakeup
 */
int esp_gmf_data_queue_wakeup(esp_gmf_data_queue_t *queue);

/**
 * @brief  Reset data queue, clear aborted flag by `esp_gmf_data_queue_wakeup`
 *
 * @param[in]  queue  Data queue instance
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to reset
 */
int esp_gmf_data_queue_reset(esp_gmf_data_queue_t *queue);

/**
 * @brief  Deinitialize data queue
 *
 * @param[in]  queue  Data queue instance
 */
void esp_gmf_data_queue_destroy(esp_gmf_data_queue_t *queue);

/**
 * @brief  Get continuous buffer from data queue for writing
 *
 * @param[in]   queue    Data queue instance
 * @param[out]  buffer   Pointer to store the buffer pointer
 * @param[in]   size     Buffer size want to get
 * @param[in]   timeout  Maximum milliseconds to wait, `ESP_GMF_DATA_QUEUE_NO_WAIT` for no wait,
 *                       `ESP_GMF_DATA_QUEUE_WAIT_FOREVER` to block
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to get buffer
 */
int esp_gmf_data_queue_acquire_write(esp_gmf_data_queue_t *queue, void **buffer, int size, uint32_t timeout);

/**
 * @brief  Get data pointer being written but not send yet
 *
 * @param[in]  queue  Data queue instance
 *
 * @return
 *       - NULL    Fail to get write buffer
 *       - Others  Write data pointer
 */
void *esp_gmf_data_queue_get_write_data(esp_gmf_data_queue_t *queue);

/**
 * @brief  Send data into data queue
 *
 * @param[in]  queue  Data queue instance
 * @param[in]  size   Written data size
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to send buffer
 */
int esp_gmf_data_queue_release_write(esp_gmf_data_queue_t *queue, int size);

/**
 * @brief  Read data from data queue, and add reference count
 *
 * @param[in]   queue    Data queue instance
 * @param[out]  buffer   Buffer in front of queue, this buffer is always valid before call
 *                       `esp_gmf_data_queue_release_read`
 * @param[out]  size     Buffer size in front of queue
 * @param[in]   timeout  Maximum milliseconds to wait, `ESP_GMF_DATA_QUEUE_NO_WAIT` for no wait,
 *                       `ESP_GMF_DATA_QUEUE_WAIT_FOREVER` to block
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to lock reader
 */
int esp_gmf_data_queue_acquire_read(esp_gmf_data_queue_t *queue, void **buffer, int *size, uint32_t timeout);

/**
 * @brief  Release data be read and decrease reference count
 *
 * @param[in]  queue  Data queue instance
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to unlock reader
 */
int esp_gmf_data_queue_release_read(esp_gmf_data_queue_t *queue);

/**
 * @brief  Rewind and send from old position
 *
 * @note  Only support when write in fixed size
 *        It will move read pointer to previous blocks, if write blocks less than setting blocks will rewind to write head
 *
 * @param[in]  queue   Data queue instance
 * @param[in]  blocks  Rewind blocks
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to rewind
 */
int esp_gmf_data_queue_rewind(esp_gmf_data_queue_t *queue, int blocks);

/**
 * @brief  Peek data unlock, call `esp_gmf_data_queue_acquire_read` to read data with block
 *         After peek data, not consume the data and release the lock
 *
 * @param[in]  queue  Data queue instance
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to peek data
 */
int esp_gmf_data_queue_peek_release(esp_gmf_data_queue_t *queue);

/**
 * @brief  Consume all data in queue
 *
 * @param[in]  queue  Data queue instance
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to consume
 */
int esp_gmf_data_queue_consume_all(esp_gmf_data_queue_t *queue);

/**
 * @brief  Check whether there are filled data in queue
 *
 * @param[in]   queue      Data queue instance
 * @param[out]  have_data  Pointer to store the result, true if have data in queue, false if empty
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to query
 */
int esp_gmf_data_queue_have_data(esp_gmf_data_queue_t *queue, bool *have_data);

/**
 * @brief  Query data queue information
 *
 * @param[in]   queue   Data queue instance
 * @param[out]  q_num   Data block number in queue
 * @param[out]  q_size  Total data size kept in queue
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to query
 */
int esp_gmf_data_queue_query(esp_gmf_data_queue_t *queue, int *q_num, int *q_size);

/**
 * @brief  Query available data size
 *
 * @param[in]  queue  Data queue instance
 *
 * @return
 *       - Available  size for write queue
 */
int esp_gmf_data_queue_get_available(esp_gmf_data_queue_t *queue);

/**
 * @brief  Create data queue bus
 *
 * @param[in]  size  Buffer size for data queue bus
 *
 * @return
 *       - NULL    Fail to initialize queue bus
 *       - Others  Data queue bus instance
 */
esp_gmf_data_queue_bus_t *esp_gmf_data_queue_db_create(int size);

/**
 * @brief  Data bus destroy function for data queue
 *
 * @param[in]  handle  Data queue bus handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_data_queue_db_destroy(esp_gmf_db_handle_t handle);

/**
 * @brief  Data bus acquire function for data queue read blocks
 *
 * @param[in]   handle       Data queue bus handle
 * @param[out]  blk          Pointer to store the acquired read block
 * @param[in]   wanted_size  Desired read size, ignored because data queue returns the next complete block
 * @param[in]   block_ticks  Ticks to wait for data
 *
 * @return
 *       - ESP_GMF_IO_OK       On success
 *       - ESP_GMF_IO_FAIL     Invalid argument
 *       - ESP_GMF_IO_ABORT    Data queue was aborted
 *       - ESP_GMF_IO_TIMEOUT  Timeout waiting for data
 */
esp_gmf_err_io_t esp_gmf_data_queue_db_acquire_read(esp_gmf_db_handle_t handle, esp_gmf_data_bus_block_t *blk, uint32_t wanted_size, int block_ticks);

/**
 * @brief  Data bus release function for data queue read blocks
 *
 * @param[in]  handle       Data queue bus handle
 * @param[in]  blk          Read block acquired by `esp_gmf_data_queue_db_acquire_read`
 * @param[in]  block_ticks  Reserved, not used
 *
 * @return
 *       - ESP_GMF_IO_OK    On success
 *       - ESP_GMF_IO_FAIL  Invalid argument or release failed
 */
esp_gmf_err_io_t esp_gmf_data_queue_db_release_read(esp_gmf_db_handle_t handle, esp_gmf_data_bus_block_t *blk, int block_ticks);

/**
 * @brief  Data bus acquire function for data queue write blocks
 *
 * @param[in]   handle       Data queue bus handle
 * @param[out]  blk          Pointer to store the acquired write block
 * @param[in]   wanted_size  Desired writable block size
 * @param[in]   block_ticks  Ticks to wait for free space
 *
 * @return
 *       - ESP_GMF_IO_OK       On success
 *       - ESP_GMF_IO_FAIL     Invalid argument
 *       - ESP_GMF_IO_ABORT    Data queue was aborted
 *       - ESP_GMF_IO_TIMEOUT  Timeout waiting for free space
 */
esp_gmf_err_io_t esp_gmf_data_queue_db_acquire_write(esp_gmf_db_handle_t handle, esp_gmf_data_bus_block_t *blk, uint32_t wanted_size, int block_ticks);

/**
 * @brief  Data bus release function for data queue write blocks
 *
 * @param[in]  handle       Data queue bus handle
 * @param[in]  blk          Write block acquired by `esp_gmf_data_queue_db_acquire_write`
 * @param[in]  block_ticks  Reserved, not used
 *
 * @return
 *       - ESP_GMF_IO_OK    On success
 *       - ESP_GMF_IO_FAIL  Invalid argument or release failed
 */
esp_gmf_err_io_t esp_gmf_data_queue_db_release_write(esp_gmf_db_handle_t handle, esp_gmf_data_bus_block_t *blk, int block_ticks);

/**
 * @brief  Set status of writing to is done
 *
 * @note  Marks the writer as done and wakes blocked readers. After this, read acquire
 *        returns `blk->is_last` on the last queued frame or on an empty frame when the
 *        queue has no data. It is also called from `esp_gmf_data_queue_db_release_write`
 *        when `blk->is_last` is set.
 *
 * @param[in]  handle  Data queue bus handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_data_queue_db_done_write(esp_gmf_db_handle_t handle);

/**
 * @brief  Reset the status of writing to the data queue bus as not done
 *
 * @param[in]  handle  Data queue bus handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_data_queue_db_reset_done_write(esp_gmf_db_handle_t handle);

/**
 * @brief  Set alignment for data queue bus payload buffers
 *
 * @param[in]  handle      Data queue bus handle
 * @param[in]  addr_align  Byte alignment for returned payload buffer addresses
 * @param[in]  size_align  Rounds acquired payload buffer length up to a multiple of this value
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_data_queue_db_set_align(esp_gmf_db_handle_t handle, uint8_t addr_align, uint8_t size_align);

/**
 * @brief  Data bus reset function for data queue
 *
 * @param[in]  handle  Data queue bus handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_data_queue_db_reset(esp_gmf_db_handle_t handle);

/**
 * @brief  Data bus abort function for data queue
 *
 * @param[in]  handle  Data queue bus handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_data_queue_db_abort(esp_gmf_db_handle_t handle);

/**
 * @brief  Data bus clear abort function for data queue
 *
 * @param[in]  handle  Data queue bus handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_data_queue_db_clear_abort(esp_gmf_db_handle_t handle);

/**
 * @brief  Data bus total size query function for data queue
 *
 * @param[in]   handle  Data queue bus handle
 * @param[out]  size    Total queue buffer size
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_data_queue_db_get_total_size(esp_gmf_db_handle_t handle, uint32_t *size);

/**
 * @brief  Data bus filled size query function for data queue
 *
 * @param[in]   handle       Data queue bus handle
 * @param[out]  filled_size  Total valid payload size kept in queue
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_FAIL         Queue metadata is invalid
 */
esp_gmf_err_t esp_gmf_data_queue_db_get_filled_size(esp_gmf_db_handle_t handle, uint32_t *filled_size);

/**
 * @brief  Data bus available size query function for data queue
 *
 * @param[in]   handle     Data queue bus handle
 * @param[out]  free_size  Available payload size for writing
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_data_queue_db_get_free_size(esp_gmf_db_handle_t handle, uint32_t *free_size);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

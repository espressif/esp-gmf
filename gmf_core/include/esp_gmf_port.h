/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2024 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#pragma once

#include <stdbool.h>
#include "sys/queue.h"
#include "esp_gmf_err.h"
#include "esp_gmf_payload.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT (4096)

/**
 * @brief  Handle to a GMF port
 */
typedef struct esp_gmf_port_ *esp_gmf_port_handle_t;

/**
 * @brief  Function pointer type for acquiring data from a port
 */
typedef esp_gmf_err_io_t (*port_acquire)(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks);

/**
 * @brief  Function pointer type for releasing data from a port
 */
typedef esp_gmf_err_io_t (*port_release)(void *handle, esp_gmf_payload_t *load, int wait_ticks);

/**
 * @brief  Function pointer type for freeing a port
 */
typedef void (*port_free)(void *p);

/**
 * @brief  Enumeration for the direction of a GMF port (input or output)
 */
typedef enum {
    ESP_GMF_PORT_DIR_IN  = 0,  /*!< Input port */
    ESP_GMF_PORT_DIR_OUT = 1,  /*!< Output port */
} esp_gmf_port_dir_t;

/**
 * @brief  Enumeration for the type of data handled by a GMF port (byte or block)
 */
typedef enum {
    ESP_GMF_PORT_TYPE_BYTE  = 0x00000001,  /*!< Byte type */
    ESP_GMF_PORT_TYPE_BLOCK = 0x00000002,  /*!< Block type */
} esp_gmf_port_type_t;

/**
 * @brief  Structure defining the I/O operations of a GMF port
 */
typedef struct {
    port_acquire  acquire;  /*!< Function pointer for acquiring data */
    port_release  release;  /*!< Function pointer for releasing data */
    port_free     del;      /*!< Function pointer for freeing the port */
} esp_gmf_port_io_ops_t;

/**
 * @brief  Structure representing a GMF port
 *         The usage of the port in linked elements is as follows
 *
 *          +---------+     +---------------+    +----------+
 *          | In Port +-----> First Element +----> Out Port |
 *          +---------+     +-------+-------+    +----------+
 *                                  |
 *                                  v
 *          +---------+     +-------+-------+    +----------+
 *          | In Port +-----> More Element  +----> Out Port |
 *          +---------+     +-------+-------+    +----------+
 *                                  |
 *                                  v
 *          +---------+     +-------+-------+    +----------+
 *          | In Port +-----> Last Element  +----> Out Port |
 *          +---------+     +---------------+    +----------+
 */
typedef struct esp_gmf_port_ {
    struct esp_gmf_port_  *next;          /*!< Pointer to the next port */
    void                  *writer;        /*!< Acquire out functions caller with the port */
    void                  *reader;        /*!< Acquire in functions caller with the port */
    esp_gmf_port_dir_t     dir;           /*!< Direction of the port */
    esp_gmf_port_type_t    type;          /*!< Type of data handled by the port */
    esp_gmf_port_io_ops_t  ops;           /*!< I/O operations of the port */
    void                  *ctx;           /*!< User context for the port */
    int                    user_buf_len;  /*!< Length of user buffer */
    int                    wait_ticks;    /*!< Timeout for port operations */
    esp_gmf_payload_t     *payload;       /*!< Payload pointer to be set */
    uint8_t               is_shared : 1;  /*!< Payload is shared to the next element port or not, 1 for shared (default), 0 for dedicated */
    esp_gmf_payload_t     *self_payload;  /*!< Self payload of the port */
    struct esp_gmf_port_  *ref_port;      /*!< Pointer to the reference port */
    int8_t                 ref_count;     /*!< Reference count indicating the number of active references */
    uint8_t                out_align;     /*!< Byte alignment of the payload */
} esp_gmf_port_t;

/**
 * @brief  Structure defining the configuration of a GMF port
 */
typedef struct esp_gmf_port_config_ {
    esp_gmf_port_dir_t     dir;         /*!< Direction of the port */
    esp_gmf_port_type_t    type;        /*!< Type of data handled by the port */
    esp_gmf_port_io_ops_t  ops;         /*!< I/O operations of the port */
    void                  *ctx;         /*!< User context associated with the port */
    int                    buf_length;  /*!< Length of the buffer */
    int                    wait_ticks;  /*!< Timeout for port operations */
} esp_gmf_port_config_t;

/**
 * @brief  Initialize a GMF port with the given configuration
 *
 * @param[in]   cfg         Pointer to the configuration structure
 * @param[out]  out_result  Pointer to store the result handle after initialization
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_MEMORY_LACK  Memory allocate failed
 */
esp_gmf_err_t esp_gmf_port_init(esp_gmf_port_config_t *cfg, esp_gmf_port_handle_t *out_result);

/**
 * @brief  Deinitialize a GMF port
 *
 * @param[in]  handle  The handle of the port to be deinitialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_port_deinit(esp_gmf_port_handle_t handle);

/**
 * @brief  Set the self payload for the specific port
 *
 * @param[in]  handle  The handle of the port
 * @param[in]  load    Pointer to the payload structure
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_port_set_payload(esp_gmf_port_handle_t handle, esp_gmf_payload_t *load);

/**
 * @brief  Clean the payload done status of a GMF port
 *
 * @param[in]  handle  The handle of the port
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_port_clean_payload_done(esp_gmf_port_handle_t handle);

/**
 * @brief  Enables or disables shared payload for the specified port
 *
 * @param[in]  handle  The port handle to enable or disable payload sharing on
 * @param[in]  enable  Set to true to enable payload sharing, or false to disable it
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument provided
 */
esp_gmf_err_t esp_gmf_port_enable_payload_share(esp_gmf_port_handle_t handle, bool enable);

/**
 * @brief  Reset the port payload and varaiable of self payload
 *
 * @param[in]  handle  The handle of the port
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_port_reset(esp_gmf_port_handle_t handle);

/**
 * @brief  Set the wait ticks for the specific port
 *
 * @param[in]  handle         The handle of the port
 * @param[in]  wait_ticks_ms  Number of milliseconds to wait
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_port_set_wait_ticks(esp_gmf_port_handle_t handle, int wait_ticks_ms);

/**
 * @brief  Set the esp_gmf_port_acquire_in and esp_gmf_port_release_in caller for the specific port
 *
 * @param[in]  handle  The handle of the port
 * @param[in]  reader  Pointer to the reader
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_port_set_reader(esp_gmf_port_handle_t handle, void *reader);

/**
 * @brief  Set the esp_gmf_port_acquire_out and esp_gmf_port_acquire_out caller for the specific port
 *
 * @param[in]  handle  The handle of the port
 * @param[in]  writer  Pointer to the writer
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_port_set_writer(esp_gmf_port_handle_t handle, void *writer);

/**
 * @brief  Add a GMF port to the end of the list
 *
 * @param[in]  head     The head of the port list
 * @param[in]  io_inst  The port instance to be added
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_port_add_last(esp_gmf_port_handle_t head, esp_gmf_port_handle_t io_inst);

/**
 * @brief  Delete a GMF port from the list
 *
 * @param[in,out]  head     A pointer to the head of the port list
 * @param[in]      io_inst  The port instance to be deleted
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_port_del_at(esp_gmf_port_handle_t *head, esp_gmf_port_handle_t io_inst);

/**
 * @brief  Acquire the expected valid data into the specified payload, regardless of whether the payload is NULL or not
 *         If writer of port is valid, the payload from the previous element stored on the port `payload` pointer is fetched
 *         If the `*load` pointer is NULL, a new payload will be allocated before calling the acquire operation
 *
 * @param[in]      handle       The handle of the port
 * @param[in,out]  load         Pointer to store the acquired payload
 * @param[in]      wanted_size  Size of the payload to be acquired
 * @param[in]      wait_ticks   Number of ticks to wait, in milliseconds
 *
 * @return
 *       - > 0                 The specific length of data being read
 *       - ESP_GMF_IO_OK       Operation successful
 *       - ESP_GMF_IO_FAIL     Operation failed or invalid arguments
 *       - ESP_GMF_IO_ABORT    Operation aborted
 *       - ESP_GMF_IO_TIMEOUT  Operation timed out
 */
esp_gmf_err_io_t esp_gmf_port_acquire_in(esp_gmf_port_handle_t handle, esp_gmf_payload_t **load, uint32_t wanted_size, int wait_ticks);

/**
 * @brief  Call the release operation or clean the payload pointer of the port
 *
 * @param[in]  handle      The handle of the port
 * @param[in]  load        Pointer to the payload to be released
 * @param[in]  wait_ticks  Number of ticks to wait
 *
 * @return
 *       - ESP_GMF_IO_OK       Operation successful
 *       - ESP_GMF_IO_FAIL     Operation failed or invalid arguments
 *       - ESP_GMF_IO_ABORT    Operation aborted
 *       - ESP_GMF_IO_TIMEOUT  Operation timed out
 */
esp_gmf_err_io_t esp_gmf_port_release_in(esp_gmf_port_handle_t handle, esp_gmf_payload_t *load, int wait_ticks);

/**
 * @brief  Acquire the buffer of the expected size into the specified payload,
 *         If the reader of the port is valid, store the provided or allocated payload to the input port of the next element
 *         If reader pointer is NULL, prepare a payload if `*load` is invalid before calling the acquire operation
 *
 * @param[in]      handle       The handle of the port
 * @param[in,out]  load         Pointer to store the acquired payload
 * @param[in]      wanted_size  Size of the payload to be acquired
 * @param[in]      wait_ticks   Number of ticks to wait, in milliseconds
 *
 * @return
 *       - > 0                 The specific length of space can be write
 *       - ESP_GMF_IO_OK       Operation successful
 *       - ESP_GMF_IO_FAIL     Operation failed or invalid arguments
 *       - ESP_GMF_IO_ABORT    Operation aborted
 *       - ESP_GMF_IO_TIMEOUT  Operation timed out
 */
esp_gmf_err_io_t esp_gmf_port_acquire_out(esp_gmf_port_handle_t handle, esp_gmf_payload_t **load, uint32_t wanted_size, int wait_ticks);

/**
 * @brief  Acquire output payload with specified size and byte alignment
 *         Behavior same as `esp_gmf_port_acquire_out` with additional byte alignment
 *
 * @param[in]      handle       The handle of the port
 * @param[in,out]  load         Pointer to store the acquired payload
 * @param[in]      align        Byte alignment of the acquired payload
 * @param[in]      wanted_size  Size of the payload to be acquired
 * @param[in]      wait_ticks   Number of ticks to wait, in milliseconds
 *
 * @return
 *       - ESP_GMF_IO_OK       Operation successful
 *       - ESP_GMF_IO_FAIL     Operation failed or invalid arguments
 *       - ESP_GMF_IO_ABORT    Operation aborted
 *       - ESP_GMF_IO_TIMEOUT  Operation timed out
 */
esp_gmf_err_io_t esp_gmf_port_acquire_aligned_out(esp_gmf_port_handle_t handle, esp_gmf_payload_t **load, uint8_t align,
                                                  uint32_t wanted_size, int wait_ticks);

/**
 * @brief  Call the release operation or clean the payload pointer of the port
 *
 * @param[in]  handle      The handle of the port
 * @param[in]  load        Pointer to the payload to be released
 * @param[in]  wait_ticks  Number of ticks to wait
 *
 * @return
 *       - ESP_GMF_IO_OK       Operation successful
 *       - ESP_GMF_IO_FAIL     Operation failed or invalid arguments
 *       - ESP_GMF_IO_ABORT    Operation aborted
 *       - ESP_GMF_IO_TIMEOUT  Operation timed out
 */
esp_gmf_err_io_t esp_gmf_port_release_out(esp_gmf_port_handle_t handle, esp_gmf_payload_t *load, int wait_ticks);

static inline void *NEW_ESP_GMF_PORT(int dir, int type, void *acq, void *release,
                                     void *del, void *ctx, int length, int ticks_ms)
{
    esp_gmf_port_config_t port_config = {
        .dir = (esp_gmf_port_dir_t)dir,
        .type = type,
        .ops.acquire = (port_acquire)acq,
        .ops.release = (port_release)release,
        .ops.del = (port_free)del,
        .ctx = ctx,
        .buf_length = length,
        .wait_ticks = ticks_ms,
    };
    esp_gmf_port_handle_t new_port = NULL;
    esp_gmf_port_init(&port_config, &new_port);
    return new_port;
}

static inline void *NEW_ESP_GMF_PORT_IN_BYTE(void *acq, void *release, void *del, void *ctx, int length, int ticks_ms)
{
    return NEW_ESP_GMF_PORT(ESP_GMF_PORT_DIR_IN, ESP_GMF_PORT_TYPE_BYTE, acq, release, del, ctx, length, ticks_ms);
}
static inline void *NEW_ESP_GMF_PORT_OUT_BYTE(void *acq, void *release, void *del, void *ctx, int length, int ticks_ms)
{
    return NEW_ESP_GMF_PORT(ESP_GMF_PORT_DIR_OUT, ESP_GMF_PORT_TYPE_BYTE, acq, release, del, ctx, length, ticks_ms);
}

static inline void *NEW_ESP_GMF_PORT_IN_BLOCK(void *acq, void *release, void *del, void *ctx, int length, int ticks_ms)
{
    return NEW_ESP_GMF_PORT(ESP_GMF_PORT_DIR_IN, ESP_GMF_PORT_TYPE_BLOCK, acq, release, del, ctx, length, ticks_ms);
}
static inline void *NEW_ESP_GMF_PORT_OUT_BLOCK(void *acq, void *release, void *del, void *ctx, int length, int ticks_ms)
{
    return NEW_ESP_GMF_PORT(ESP_GMF_PORT_DIR_OUT, ESP_GMF_PORT_TYPE_BLOCK, acq, release, del, ctx, length, ticks_ms);
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */
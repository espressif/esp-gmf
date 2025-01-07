/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2025 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
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

#include "esp_gmf_io.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define HTTP_STREAM_TASK_STACK      (6 * 1024)
#define HTTP_STREAM_TASK_CORE       (0)
#define HTTP_STREAM_TASK_PRIO       (10)
#define HTTP_STREAM_RINGBUFFER_SIZE (20 * 1024)

/**
 * @brief  HTTP Stream hook type
 */
typedef enum {
    HTTP_STREAM_PRE_REQUEST = 0x01, /*!< The event handler will be called before HTTP Client making the connection to the server */
    HTTP_STREAM_ON_REQUEST,         /*!< The event handler will be called when HTTP Client is requesting data,
                                     * If the function return the value (-1: ESP_GMF_ERR_FAIL), HTTP Client will be stopped
                                     * If the function return the value > 0, HTTP Stream will ignore the post_field
                                     * If the function return the value = 0, HTTP Stream continue send data from post_field (if any)
                                     */
    HTTP_STREAM_ON_RESPONSE,        /*!< The event handler will be called when HTTP Client is receiving data
                                     * If the function return the value (-1: ESP_GMF_ERR_FAIL), HTTP Client will be stopped
                                     * If the function return the value > 0, HTTP Stream will ignore the read function
                                     * If the function return the value = 0, HTTP Stream continue read data from HTTP Server
                                     */
    HTTP_STREAM_POST_REQUEST,       /*!< The event handler will be called after HTTP Client send header and body to the server, before fetching the headers */
    HTTP_STREAM_FINISH_REQUEST,     /*!< The event handler will be called after HTTP Client fetch the header and ready to read HTTP body */
} http_stream_event_id_t;

/**
 * @brief  Stream event message
 */
typedef struct {
    http_stream_event_id_t event_id;    /*!< Event ID */
    void                  *http_client; /*!< Reference to HTTP Client using by this HTTP Stream */
    void                  *buffer;      /*!< Reference to Buffer using by the Audio Element */
    int                    buffer_len;  /*!< Length of buffer */
    void                  *user_data;   /*!< User data context, from `http_io_cfg_t` */
    esp_gmf_io_handle_t    el;          /*!< Audio element context */
} http_stream_event_msg_t;

typedef int (*http_io_event_handle_t)(http_stream_event_msg_t *msg);

/**
 * @brief  HTTP Stream configurations
 *         Default value will be used if any entry is zero
 */
typedef struct {
    int                    task_stack;          /*!< Task stack size */
    int                    task_core;           /*!< Task running in core (0 or 1) */
    int                    task_prio;           /*!< Task priority (based on freeRTOS priority) */
    bool                   stack_in_ext;        /*!< Try to allocate stack in external memory */
    int                    dir;                 /*!< Type of stream */
    int                    out_buf_size;        /*!< Size of output buffer */
    http_io_event_handle_t event_handle;        /*!< The hook function for HTTP Stream */
    void                  *user_data;           /*!< User data context */
    const char            *cert_pem;            /*!< SSL server certification, PEM format as string, if the client requires to verify server */
    esp_err_t (*crt_bundle_attach)(void *conf); /*!< Function pointer to esp_crt_bundle_attach. Enables the use of certification
                                                bundle for server verification, must be enabled in menuconfig */
} http_io_cfg_t;

#define HTTP_STREAM_CFG_DEFAULT() {                    \
    .dir               = ESP_GMF_IO_DIR_READER,        \
    .out_buf_size      = HTTP_STREAM_RINGBUFFER_SIZE,  \
    .task_stack        = HTTP_STREAM_TASK_STACK,       \
    .task_core         = HTTP_STREAM_TASK_CORE,        \
    .task_prio         = HTTP_STREAM_TASK_PRIO,        \
    .stack_in_ext      = true,                         \
    .event_handle      = NULL,                         \
    .user_data         = NULL,                         \
    .cert_pem          = NULL,                         \
    .crt_bundle_attach = NULL,                         \
}

/**
 * @brief  Initialize the HTTP stream I/O element with the specified configuration
 *
 * @param[in]   config  Pointer to an `http_io_cfg_t` structure containing the configuration
 *                      settings for the HTTP I/O element
 * @param[out]  io      Pointer to a `esp_gmf_io_handle_t` where the initialized I/O object
 *                      handle will be stored
 *
 * @return
 *       - ESP_GMF_ERR_OK           Initialization successful
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument(s)
 *       - ESP_GMF_ERR_MEMORY_LACK  Insufficient memory for initialization
 */
esp_gmf_err_t esp_gmf_io_http_init(http_io_cfg_t *config, esp_gmf_io_handle_t *io);

/**
 * @brief  Cast or update the configuration of an HTTP stream I/O element
 *
 * @param[in]  config  Pointer to an `http_io_cfg_t` structure containing the new configuration
 *                     settings for the HTTP I/O element
 * @param[in]  obj     Handle to the existing HTTP stream I/O element
 *
 * @return
 *       - ESP_GMF_ERR_OK           Configuration update successful
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument(s)
 *       - ESP_GMF_ERR_MEMORY_LACK  Insufficient memory for configuration update
 */
esp_gmf_err_t esp_gmf_io_http_cast(http_io_cfg_t *config, esp_gmf_io_handle_t obj);

/**
 * @brief  Reset http information.
 *
 *         This function can be used in event_handler of http_stream.
 *         User can call this function to connect to next track in playlist when he/she gets `HTTP_STREAM_FINISH_TRACK` event
 *
 * @param  el  The http_stream element handle
 *
 * @return
 *       - ESP_GMF_ERR_OK    on success
 *       - ESP_GMF_ERR_FAIL  on errors
 */
esp_gmf_err_t esp_gmf_io_http_reset(esp_gmf_io_handle_t el);

/**
 * @brief  Set SSL server certification
 *
 * @note  PEM format as string, if the client requires to verify server
 *
 * @param  el    The http_stream element handle
 * @param  cert  server certification
 *
 * @return
 *       - ESP_GMF_ERR_OK  on success
 */
esp_gmf_err_t esp_gmf_io_http_set_server_cert(esp_gmf_io_handle_t el, const char *cert);

#ifdef __cplusplus
}
#endif /* __cplusplus */
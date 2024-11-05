/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2022 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
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

#include "esp_gmf_err.h"
#include "esp_gmf_info.h"
#include "esp_gmf_oal_mem.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Initialize the file information by given handle
 *
 * @param[in]  handle  Pointer to the file information handle to initialize
 *
 * @return
 *       - ESP_GMF_ERR_OK  Success
 */
static inline esp_gmf_err_t esp_gmf_info_file_init(esp_gmf_info_file_t *handle)
{
    if (handle->uri) {
        esp_gmf_oal_free((char *)handle->uri);
        handle->uri = NULL;
    }
    handle->pos = 0;
    handle->size = 0;
    return ESP_GMF_ERR_OK;
}

/**
 * @brief  Deinitialize the file information by given handle
 *
 * @param[in]  handle  Pointer to the file information handle to deinitialize
 *
 * @return
 *       - ESP_GMF_ERR_OK  Success
 */
static inline esp_gmf_err_t esp_gmf_info_file_deinit(esp_gmf_info_file_t *handle)
{
    if (handle->uri) {
        esp_gmf_oal_free((char *)handle->uri);
    }
    return ESP_GMF_ERR_OK;
}

/**
 * @brief  Update the file position of the specific handle
 *
 * @param[in]  handle    Pointer to the file information handle
 * @param[in]  byte_pos  Byte position to update by
 *
 * @return
 *       - ESP_GMF_ERR_OK  Success
 */
static inline esp_gmf_err_t esp_gmf_info_file_update_pos(esp_gmf_info_file_t *handle, uint64_t byte_pos)
{
    handle->pos += byte_pos;
    return ESP_GMF_ERR_OK;
}

/**
 * @brief  Set the file position of the specific handle
 *
 * @param[in]  handle    Pointer to the file information handle
 * @param[in]  byte_pos  Byte position to set
 *
 * @return
 *       - ESP_GMF_ERR_OK  Success
 */
static inline esp_gmf_err_t esp_gmf_info_file_set_pos(esp_gmf_info_file_t *handle, uint64_t byte_pos)
{
    handle->pos = byte_pos;
    return ESP_GMF_ERR_OK;
}

/**
 * @brief  Get the position of the specific handle
 *
 * @param[in]   handle    Pointer to the file information handle
 * @param[out]  byte_pos  Pointer to store the byte position
 *
 * @return
 *       - ESP_GMF_ERR_OK  Success
 */
static inline esp_gmf_err_t esp_gmf_info_file_get_pos(esp_gmf_info_file_t *handle, uint64_t *byte_pos)
{
    *byte_pos = handle->pos;
    return ESP_GMF_ERR_OK;
}

/**
 * @brief  Set the total size of the specific handle
 *
 * @param[in]  handle      Pointer to the file information handle
 * @param[in]  total_size  Total size to set
 *
 * @return
 *       - ESP_GMF_ERR_OK  Success
 */
static inline esp_gmf_err_t esp_gmf_info_file_set_size(esp_gmf_info_file_t *handle, uint64_t total_size)
{
    handle->size = total_size;
    return ESP_GMF_ERR_OK;
}

/**
 * @brief  Get the total size of the specific handle
 *
 * @param[in]   handle      Pointer to the file information handle
 * @param[out]  total_size  Pointer to store the total size
 *
 * @return
 *       - ESP_GMF_ERR_OK  Success
 */
static inline esp_gmf_err_t esp_gmf_info_file_get_size(esp_gmf_info_file_t *handle, uint64_t *total_size)
{
    *total_size = handle->size;
    return ESP_GMF_ERR_OK;
}

/**
 * @brief  Set the URI of the specific handle
 *
 * @param[in]  handle  Pointer to the file information handle
 * @param[in]  uri     URI to set
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_MEMORY_LACK  Memory allocation failed
 */
static inline esp_gmf_err_t esp_gmf_info_file_set_uri(esp_gmf_info_file_t *handle, const char *uri)
{
    if (handle->uri) {
        esp_gmf_oal_free((void *)handle->uri);
        handle->uri = NULL;
    }
    if (uri) {
        handle->uri = esp_gmf_oal_strdup(uri);
        if (handle->uri == NULL) {
            return ESP_GMF_ERR_MEMORY_LACK;
        }
    }
    return ESP_GMF_ERR_OK;
}

/**
 * @brief  Get the URI of the specific handle
 *
 * @param[in]   handle  Pointer to the file information handle
 * @param[out]  uri     Pointer to store the URI
 *
 * @return
 *       - ESP_GMF_ERR_OK  Success
 */
static inline esp_gmf_err_t esp_gmf_info_file_get_uri(esp_gmf_info_file_t *handle, char **uri)
{
    *uri = (char *)handle->uri;
    return ESP_GMF_ERR_OK;
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */
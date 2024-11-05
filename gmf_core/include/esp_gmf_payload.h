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
#include "esp_gmf_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Structure representing a payload in GMF
 */
typedef struct {
    uint8_t  *buf;             /*!< Pointer to the payload buffer */
    size_t    buf_length;      /*!< Length of the payload buffer */
    size_t    valid_size;      /*!< Size of valid data in the payload buffer */
    bool      is_done;         /*!< Flag indicating if this payload buffer marks the end of the stream */
    uint64_t  pts;             /*!< Presentation time stamp */
    uint8_t   needs_free : 1;  /*!< Flag indicating if the payload buffer needs to be freed by esp_gmf_payload_delete or not*/
} esp_gmf_payload_t;

/**
 * @brief  Create a new payload instance without buffer
 *
 * @param[out]  instance  Pointer to store the newly created payload instance
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_MEMORY_LACK  Not enough memory to create the payload instance
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument provided
 */
esp_gmf_err_t esp_gmf_payload_new(esp_gmf_payload_t **instance);

/**
 * @brief  Create a new payload instance and buffer by specified buffer length
 *
 * @param[in]   buf_length  Length of the payload buffer
 * @param[out]  instance    Pointer to store the newly created payload instance
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_MEMORY_LACK  Not enough memory to create the payload instance
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument provided
 */
esp_gmf_err_t esp_gmf_payload_new_with_len(uint32_t buf_length, esp_gmf_payload_t **instance);

/**
 * @brief  Copy data and done flag from one payload instance to another
 *
 * @param[in]   src   Source payload instance
 * @param[out]  dest  Destination payload instance
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument provided
 */
esp_gmf_err_t esp_gmf_payload_copy_data(esp_gmf_payload_t *src, esp_gmf_payload_t *dest);

/**
 * @brief  Reallocate the buffer of a payload instance to the specified length
 *         Check if the given payload buffer length is sufficient; if not, allocate a new buffer with the specified length
 *
 * @param[in]  instance    Payload instance to reallocate the buffer for
 * @param[in]  new_length  New length for the payload buffer
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_MEMORY_LACK  Not enough memory to reallocate the buffer
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid instance or new_length is zero
 */
esp_gmf_err_t esp_gmf_payload_realloc_buf(esp_gmf_payload_t *instance, uint32_t new_length);

/**
 * @brief  Reallocate the buffer of a payload instance to the specified length with specified byte alignment
 *         Behavior same as `esp_gmf_payload_realloc_buf` with an additional alignment request
 *
 * @param[in]  instance    Payload instance to reallocate the buffer for
 * @param[in]  align       Byte alignment for the new payload buffer
 * @param[in]  new_length  New length for the payload buffer
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_MEMORY_LACK  Not enough memory to reallocate the buffer
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid instance or new_length is zero
 */
esp_gmf_err_t esp_gmf_payload_realloc_aligned_buf(esp_gmf_payload_t *instance, uint8_t align, uint32_t new_length);

/**
 * @brief  Set the done flag for a payload instance
 *
 * @param[in]  instance  Payload instance to set as done
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument provided
 */
esp_gmf_err_t esp_gmf_payload_set_done(esp_gmf_payload_t *instance);

/**
 * @brief  Clean the done flag for a payload instance
 *
 * @param[in]  instance  Payload instance to clean the done status for
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument provided
 */
esp_gmf_err_t esp_gmf_payload_clean_done(esp_gmf_payload_t *instance);

/**
 * @brief  Delete a payload instance, if needs_free is set free associated resources
 *
 * @param[in]  instance  Payload instance to delete
 */
void esp_gmf_payload_delete(esp_gmf_payload_t *instance);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
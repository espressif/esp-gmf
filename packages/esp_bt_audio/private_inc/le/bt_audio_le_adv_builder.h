/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Opaque handle type for extended advertising builder
 */
typedef struct bt_audio_le_adv_builder *bt_audio_le_adv_builder_t;

/**
 * @brief  Create and initialize an extended advertising builder
 *
 * @param[in]   size     Maximum advertising data size in bytes
 * @param[out]  builder  Receives the new extended advertising builder handle; set to NULL on error.
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  If builder is NULL
 *       - ESP_ERR_NO_MEM       If allocation fails
 */
esp_err_t bt_audio_le_adv_builder_init(size_t size, bt_audio_le_adv_builder_t *builder);

/**
 * @brief  Deinitialize and free an extended advertising builder
 *
 * @param[in]  builder  Extended advertising builder handle
 */
void bt_audio_le_adv_builder_deinit(bt_audio_le_adv_builder_t builder);

/**
 * @brief  Add a raw advertising field
 *
 * @param[in]  builder    Extended advertising builder handle
 * @param[in]  type       Field type
 * @param[in]  value      Pointer to field value
 * @param[in]  value_len  Length of field value
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid arguments
 *       - ESP_ERR_NO_MEM       Not enough space for the field
 */
esp_err_t bt_audio_le_adv_builder_add_field(bt_audio_le_adv_builder_t builder, uint8_t type,
                                            const uint8_t *value, size_t value_len);

/**
 * @brief  Add service data field
 *
 * @param[in]  builder  Extended advertising builder handle
 * @param[in]  data     Pointer to service data
 * @param[in]  len      Length of service data
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid arguments
 *       - ESP_ERR_NO_MEM       Not enough space for the field
 */
esp_err_t bt_audio_le_adv_builder_add_service_data(bt_audio_le_adv_builder_t builder, const uint8_t *data, size_t len);

/**
 * @brief  Add 16-bit service UUID field
 *
 * @param[in]  builder  Extended advertising builder handle
 * @param[in]  uuid     16-bit service UUID
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid arguments
 *       - ESP_ERR_NO_MEM       Not enough space for the field
 */
esp_err_t bt_audio_le_adv_builder_add_service_uuid16(bt_audio_le_adv_builder_t builder, uint16_t uuid);

/**
 * @brief  Add manufacturer specific data
 *
 * @param[in]  builder  Extended advertising builder handle
 * @param[in]  data     Pointer to manufacturer data
 * @param[in]  len      Length of manufacturer data
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid arguments
 *       - ESP_ERR_NO_MEM       Not enough space for the field
 */
esp_err_t bt_audio_le_adv_builder_add_manufacturer_data(bt_audio_le_adv_builder_t builder,
                                                        const uint8_t *data, size_t len);

/**
 * @brief  Add device name field
 *
 * @param[in]  builder  Extended advertising builder handle
 * @param[in]  name     Null-terminated device name string
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid arguments
 *       - ESP_ERR_NO_MEM       Not enough space for the field
 */
esp_err_t bt_audio_le_adv_builder_add_name(bt_audio_le_adv_builder_t builder, const char *name);

/**
 * @brief  Add appearance field
 *
 * @param[in]  builder     Extended advertising builder handle
 * @param[in]  appearance  Appearance value
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid arguments
 *       - ESP_ERR_NO_MEM       Not enough space for the field
 */
esp_err_t bt_audio_le_adv_builder_add_appearance(bt_audio_le_adv_builder_t builder, uint16_t appearance);

/**
 * @brief  Add TX power field
 *
 * @param[in]  builder   Extended advertising builder handle
 * @param[in]  tx_power  TX power value in dBm
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid arguments
 *       - ESP_ERR_NO_MEM       Not enough space for the field
 */
esp_err_t bt_audio_le_adv_builder_add_tx_power(bt_audio_le_adv_builder_t builder, int8_t tx_power);

/**
 * @brief  Add flags field
 *
 * @param[in]  builder  Extended advertising builder handle
 * @param[in]  flags    Flags value
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid arguments
 *       - ESP_ERR_NO_MEM       Not enough space for the field
 */
esp_err_t bt_audio_le_adv_builder_add_flags(bt_audio_le_adv_builder_t builder, uint8_t flags);

/**
 * @brief  Get total length of built advertising data
 *
 * @param[in]   builder  Extended advertising builder handle
 * @param[out]  size     Pointer to store total data length
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid arguments
 */
esp_err_t bt_audio_le_adv_builder_get_total_len(bt_audio_le_adv_builder_t builder, size_t *size);

/**
 * @brief  Get the built advertising data buffer
 *
 * @param[in]   builder          Extended advertising builder handle
 * @param[out]  out_buffer       Output buffer to copy advertising data into
 * @param[in]   out_buffer_size  Size of output buffer in bytes
 * @param[out]  out_size         Pointer to store actual size of data copied
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid arguments
 *       - ESP_ERR_NO_MEM       Output buffer too small
 */
esp_err_t bt_audio_le_adv_builder_get_buffer(bt_audio_le_adv_builder_t builder, uint8_t *out_buffer,
                                             size_t out_buffer_size, size_t *out_size);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

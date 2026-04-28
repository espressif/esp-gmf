/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_io.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Espressif embed flash configurations, if any entry is zero then the configuration will be set to default values
 */
typedef struct {
    int               max_files;  /*!< IO direction, reader or writer */
    const char       *name;       /*!< Name for this instance */
    esp_gmf_io_cfg_t  io_cfg;     /*!< IO configuration for task and buffer */
} embed_flash_io_cfg_t;

#define EMBED_FLASH_CFG_DEFAULT()  {    \
    .max_files = 200,                   \
    .name      = NULL,                  \
    .io_cfg    = {                      \
        .thread = {                     \
            .stack        = 0,          \
            .prio         = 0,          \
            .core         = 0,          \
            .stack_in_ext = false,      \
        },                              \
        .buffer_cfg = {                 \
            .io_size     = 0,           \
            .buffer_size = 0,           \
            .read_filter = NULL,        \
        },                              \
        .enable_speed_monitor = false,  \
    },                                  \
}

/**
 * @brief  Embed information in flash
 */
typedef struct embed_item_info {
    const uint8_t *address;  /*!< The corresponding address in flash */
    int            size;     /*!< Size of corresponding data */
} embed_item_info_t;

/**
 * @brief  Initializes the embed flash stream I/O with the provided configuration
 *
 * @param[in]   config  Pointer to the embed flash IO configuration
 * @param[out]  io      Pointer to the embed flash IO handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_io_embed_flash_init(embed_flash_io_cfg_t *config, esp_gmf_io_handle_t *io);

/**
 * @brief  Set the embed flash context
 *         This function mainly provides information about embed flash data
 *
 * @param[in]  io       The embed flash element handle
 * @param[in]  context  The embed flash context
 * @param[in]  max_num  The number of embed flash context
 *
 * @return
 *       - ESP_GMF_ERR_OK
 *       - ESP_GMF_ERR_FAIL
 */
esp_gmf_err_t esp_gmf_io_embed_flash_set_context(esp_gmf_io_handle_t io, const embed_item_info_t *context, int max_num);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

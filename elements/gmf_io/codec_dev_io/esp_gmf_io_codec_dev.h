/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_codec_dev.h"
#include "esp_gmf_io.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Audio Codec Device IO configurations, if any entry is zero then the configuration will be set to default values
 */
typedef struct {
    esp_codec_dev_handle_t  dev;     /*!< Audio Codec Device handler */
    esp_gmf_io_dir_t        dir;     /*!< IO direction, reader or writer */
    const char             *name;    /*!< Name for this instance */
    esp_gmf_io_cfg_t        io_cfg;  /*!< IO configuration for task and buffer */
} codec_dev_io_cfg_t;

#define ESP_GMF_IO_CODEC_DEV_CFG_DEFAULT()  {  \
    .dev    = NULL,                            \
    .dir    = ESP_GMF_IO_DIR_NONE,             \
    .name   = NULL,                            \
    .io_cfg = {                                \
        .thread = {                            \
            .stack        = 0,                 \
            .prio         = 0,                 \
            .core         = 0,                 \
            .stack_in_ext = false,             \
        },                                     \
        .buffer_cfg = {                        \
            .io_size     = 0,                  \
            .buffer_size = 0,                  \
        },                                     \
        .enable_speed_monitor = false,         \
    },                                         \
}

/**
 * @brief  Initializes the Audio Codec Device I/O with the provided configuration
 *
 * @param[in]   config  Audio codec device io configuration
 * @param[out]  io      Audio codec device io handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_io_codec_dev_init(codec_dev_io_cfg_t *config, esp_gmf_io_handle_t *io);

/**
 * @brief  Set or update the audio codec device handle for an existing I/O instance
 *
 * @param[in]  io   Audio codec device I/O handle
 * @param[in]  dev  New audio codec device handle to set
 *
 * @return
 *       - ESP_GMF_ERR_OK             Success
 *       - ESP_GMF_ERR_INVALID_ARG    Invalid arguments provided
 *       - ESP_GMF_ERR_INVALID_STATE  Invalid state
 */
esp_gmf_err_t esp_gmf_io_codec_dev_set_dev(esp_gmf_io_handle_t io, esp_codec_dev_handle_t dev);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

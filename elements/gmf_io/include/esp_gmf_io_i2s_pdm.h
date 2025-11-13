/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "driver/i2s_pdm.h"
#include "esp_gmf_io.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  I2S PDM IO configurations, if any entry is zero then the configuration will be set to default values
 */
typedef struct {
    i2s_chan_handle_t  pdm_chan;  /*!< I2S tx channel handler */
    int                dir;       /*!< IO direction, reader or writer */
    const char        *name;      /*!< Name for this instance */
    esp_gmf_io_cfg_t   io_cfg;    /*!< IO configuration for task and buffer */
} i2s_pdm_io_cfg_t;

#define ESP_GMF_IO_I2S_PDM_CFG_DEFAULT()  {  \
    .pdm_chan = NULL,                        \
    .dir      = ESP_GMF_IO_DIR_READER,       \
    .name     = NULL,                        \
    .io_cfg   = {                            \
        .thread = {                          \
            .stack        = 0,               \
            .prio         = 0,               \
            .core         = 0,               \
            .stack_in_ext = false,           \
        },                                   \
        .buffer_cfg = {                      \
            .io_size     = 0,                \
            .buffer_size = 0,                \
        },                                   \
        .enable_speed_monitor = false,       \
    },                                       \
}

/**
 * @brief  Initializes the I2S PDM I/O with the provided configuration
 *
 * @param[in]   config  Pointer to the file IO configuration
 * @param[out]  io      Pointer to the file IO handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_io_i2s_pdm_init(i2s_pdm_io_cfg_t *config, esp_gmf_io_handle_t *io);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

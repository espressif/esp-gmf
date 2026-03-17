/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_gmf_io.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Fake IO configurations
 */
typedef struct {
    int          dir;   /*!< IO direction, reader or writer */
    const char  *name;  /*!< Name for this instance */
    int          init_return;
} fake_io_cfg_t;

typedef struct {
    esp_gmf_io_t      base;
    esp_gmf_err_t     open_return;
    esp_gmf_err_io_t  acquire_read_return;
    esp_gmf_err_io_t  release_read_return;
    esp_gmf_err_io_t  acquire_write_return;
    esp_gmf_err_io_t  release_write_return;
    esp_gmf_err_t     seek_return;
    uint32_t          seek_called_count;
    esp_gmf_err_t     close_return;
    esp_gmf_err_t     delete_return;
    esp_gmf_err_t     new_return;
} fake_io_t;

#define FAKE_IO_CFG_DEFAULT() {   \
    .dir  = ESP_GMF_IO_DIR_NONE,  \
    .name = NULL,                 \
}

/**
 * @brief  Initializes the fake stream I/O with the provided configuration
 *
 * @param[in]   config  Pointer to the fake IO configuration
 * @param[out]  io      Pointer to the fake IO handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK  Success
 *       - other           error codes  Initialization failed
 */
esp_gmf_err_t fake_io_init(fake_io_cfg_t *config, esp_gmf_io_handle_t *io);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

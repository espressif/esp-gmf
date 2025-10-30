/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "driver/sdm.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  SDM peripheral handle
 *
 *         This structure represents a Sigma Delta Modulation channel handle
 */
typedef struct {
    sdm_channel_handle_t  sdm_chan;  /*!< SDM channel handle */
} periph_sdm_handle_t;

/**
 * @brief  Initialize the SDM channel
 *
 *         This function initializes a Sigma Delta Modulation channel using the provided configuration structure.
 *
 * @param[in]   cfg            Pointer to sdm_config_t
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  periph_handle  Pointer to store the returned periph_sdm_handle_t handle
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_sdm_init(void *cfg, int cfg_size, void **periph_handle);

/**
 * @brief  Deinitialize the SDM channel
 *
 *         This function deinitializes the SDM channel and frees the allocated resources.
 *         It should be called when the channel is no longer needed.
 *
 * @param[in]  periph_handle  Handle returned by periph_sdm_init
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_sdm_deinit(void *periph_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

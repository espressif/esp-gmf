/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Acquire a LDO channel
 *
 *         This function acquire LDO channel using the provided configuration structure.
 *
 * @param[in]   cfg            Pointer to esp_ldo_channel_config_t
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  periph_handle  Pointer to store the returned esp_ldo_channel_handle_t handle
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_ldo_init(void *cfg, int cfg_size, void **periph_handle);

/**
 * @brief  Release a LDO channel
 *
 *         This function release a LDO channel and frees the allocated resources.
 *
 * @param[in]  periph_handle  Handle returned by periph_ldo_init
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_ldo_deinit(void *periph_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

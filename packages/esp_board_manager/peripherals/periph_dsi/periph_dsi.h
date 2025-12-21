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
 * @brief  Initialize the DSI bus
 *
 *         This function initializes the DSI bus using the provided configuration structure.
 *
 * @param[in]   cfg            Pointer to esp_lcd_dsi_bus_config_t
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  periph_handle  Pointer to store the returned esp_lcd_dsi_bus_handle_t handle
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_dsi_init(void *cfg, int cfg_size, void **periph_handle);

/**
 * @brief  Deinitialize the DSI bus
 *
 *         This function deinitializes the DSI bus and frees the allocated resources.
 *
 * @param[in]  periph_handle  Handle returned by periph_dsi_init
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_dsi_deinit(void *periph_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Share copier element configuration structure
 */
typedef struct {
    uint8_t  copies;    /*!< Number of copies to make of each frame */
    uint8_t  q_number;  /*!< Number of message queues for storing copied frames */
} capture_share_copy_el_cfg_t;

/**
 * @brief  Initialize share copier element
 *
 * @param[in]   config  Share copier element configuration (optional)
 *                      If set to `NULL`, default `copies` is 2 and `q_number` is 3
 * @param[out]  handle  Pointer to store GMF object handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_MEMORY_LACK  Not enough memory
 *
 * @note  The created element must be properly configured before use
 */
esp_gmf_err_t capture_share_copy_el_init(capture_share_copy_el_cfg_t *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Enable or disable share copier port
 *
 * @param[in]  handle  Share copier element handle
 * @param[in]  port    Port number to enable/disable
 * @param[in]  enable  true to enable the port, false to disable
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t capture_share_copy_el_enable(esp_gmf_element_handle_t handle, uint8_t port, bool enable);

/**
 * @brief  Create new output port for share copier
 *
 * @note  The created output port must be maintained and released by the user
 *        when no longer needed
 *
 * @param[in]  handle  Share copier element handle
 * @param[in]  port    Port number to create output for
 *
 * @return
 *       - NULL    Invalid argument
 *       - Others  New output port handle
 */
esp_gmf_port_handle_t capture_share_copy_el_new_out_port(esp_gmf_element_handle_t handle, uint8_t port);

/**
 * @brief  Enable/disable single-frame fetch mode for share copier
 *
 * @param[in]  handle  Share copier element handle
 * @param[in]  port    Port number for fetch once control
 * @param[in]  enable  Whether fetch only one frame
 *                       true: Fill to shared port after fetched once
 *                       false: Continuous streaming fill to port
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t capture_share_copy_el_set_single_fetch(esp_gmf_element_handle_t handle, uint8_t port, bool enable);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

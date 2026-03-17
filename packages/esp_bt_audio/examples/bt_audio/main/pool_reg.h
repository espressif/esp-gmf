/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "esp_gmf_pool.h"
#include "esp_gmf_err.h"

/**
 * @brief  Register GMF pool with required elements and IO types
 *
 * @param[in]  pool  Pool handle
 *
 * @return
 *       - ESP_GMF_ERR_OK  On success
 *       - Others          On failure
 */
esp_gmf_err_t pool_reg(esp_gmf_pool_handle_t pool);

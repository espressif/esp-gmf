/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once
#include "driver/ana_cmpr.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Analog comparator peripheral handle structure
 */
typedef struct {
    ana_cmpr_handle_t  cmpr_handle;  /*!< Analog comparator handle */
    ana_cmpr_unit_t    unit;         /*!< Analog comparator unit */
} periph_anacmpr_handle_t;

/**
 * @brief  Initialize an analog comparator peripheral
 *
 *         This function initializes an analog comparator peripheral using the provided configuration structure.
 *         It creates the analog comparator unit and retrieves the associated GPIO numbers.
 *
 * @param[in]   cfg            Pointer to ana_cmpr_config_t
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  periph_handle  Returns the periph_anacmpr_handle_t handle
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_anacmpr_init(void *cfg, int cfg_size, void **periph_handle);

/**
 * @brief  Deinitialize an analog comparator peripheral
 *
 *         This function deinitializes an analog comparator peripheral and frees the allocated resources.
 *
 * @param[in]  periph_handle  Handle returned by periph_anacmpr_init
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_anacmpr_deinit(void *periph_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief  Initialize the console
 */
void cli_init(void);

/**
 * @brief  Callback function for device found
 *
 * @param[in]  name  Device name
 * @param[in]  bda   Device address
 *
 * @return
 *       - void
 */
void cli_bt_device_found(const char *name, const uint8_t *bda);

/**
 * @brief  Callback function for connection state changed
 *
 * @param[in]  bda        Device address
 * @param[in]  connected  Connection state
 *
 * @return
 *       - void
 */
void cli_bt_device_conn_st_chg(const uint8_t *bda, bool connected);

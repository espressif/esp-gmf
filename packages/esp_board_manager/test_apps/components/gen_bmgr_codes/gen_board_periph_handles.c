/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Auto-generated peripheral handle definition file
 * DO NOT MODIFY THIS FILE MANUALLY
 *
 * See LICENSE file for details.
 */

#include <stddef.h>
#include "esp_board_periph.h"
#include "periph_gpio.h"
#include "periph_i2c.h"
#include "periph_i2s.h"
#include "periph_i2s.h"
#include "periph_ledc.h"
#include "periph_spi.h"

// Peripheral handle array
esp_board_periph_entry_t g_esp_board_periph_handles[] = {
    {
        .next = &g_esp_board_periph_handles[1],
        .type = "gpio",
        .role = "none",
        .init = periph_gpio_init,
        .deinit = periph_gpio_deinit
    },
    {
        .next = &g_esp_board_periph_handles[2],
        .type = "i2c",
        .role = "master",
        .init = periph_i2c_init,
        .deinit = periph_i2c_deinit
    },
    {
        .next = &g_esp_board_periph_handles[3],
        .type = "i2s",
        .role = "master",
        .init = periph_i2s_init,
        .deinit = periph_i2s_deinit
    },
    {
        .next = &g_esp_board_periph_handles[4],
        .type = "i2s",
        .role = "slave",
        .init = periph_i2s_init,
        .deinit = periph_i2s_deinit
    },
    {
        .next = &g_esp_board_periph_handles[5],
        .type = "ledc",
        .role = "none",
        .init = periph_ledc_init,
        .deinit = periph_ledc_deinit
    },
    {
        .next = NULL,
        .type = "spi",
        .role = "master",
        .init = periph_spi_init,
        .deinit = periph_spi_deinit
    },
};

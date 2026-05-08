/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

/* TODO: Implement LCD touch over SPI.
 *
 * Planned behavior:
 * - register a unique linker entry named "lcd_touch_sub_spi"
 * - create esp_lcd_panel_io_spi_config_t based panel IO
 * - reuse esp_lcd_touch_config_t and lcd_touch_factory_entry_t()
 * - do not publish an I2C effective address
 *
 * This file is intentionally not added to the component build in the first
 * dev_lcd_touch implementation phase.
 */

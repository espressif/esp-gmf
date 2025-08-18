/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Auto-generated peripheral configuration file
 * DO NOT MODIFY THIS FILE MANUALLY
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_board_periph.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "hal/gpio_types.h"
#include "periph_gpio.h"
#include "periph_i2s.h"
#include "periph_ledc.h"
#include "periph_spi.h"

// Peripheral configuration structures
const static i2c_master_bus_config_t esp_bmgr_i2c_master_cfg = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = 2,
    .scl_io_num = 1,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .intr_priority = 1,
    .trans_queue_depth = 0,
    .flags = {
            .enable_internal_pullup = true,
        },
};

const static periph_i2s_config_t esp_bmgr_i2s_audio_out_cfg = {
    .port = I2S_NUM_0,
    .role = I2S_ROLE_MASTER,
    .mode = I2S_COMM_MODE_STD,
    .direction = I2S_DIR_TX,
    .i2s_cfg = {
            .std = {
                    .clk_cfg = {
                            .sample_rate_hz = 48000,
                            .clk_src = I2S_CLK_SRC_DEFAULT,
                            .ext_clk_freq_hz = 0,
                            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
                        },
                    .slot_cfg = {
                            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                            .slot_mode = I2S_SLOT_MODE_STEREO,
                            .slot_mask = I2S_STD_SLOT_BOTH,
                            .ws_width = 16,
                            .ws_pol = false,
                            .bit_shift = true,
                            .left_align = true,
                            .big_endian = false,
                            .bit_order_lsb = false,
                        },
                    .gpio_cfg = {
                            .mclk = 42,
                            .bclk = 40,
                            .ws = 39,
                            .dout = 41,
                            .din = 3,
                            .invert_flags = {
                                    .mclk_inv = false,
                                    .bclk_inv = false,
                                    .ws_inv = false,
                                },
                        },
                },
        },
};

const static periph_i2s_config_t esp_bmgr_i2s_audio_in_cfg = {
    .port = I2S_NUM_0,
    .role = I2S_ROLE_MASTER,
    .mode = I2S_COMM_MODE_STD,
    .direction = I2S_DIR_RX,
    .i2s_cfg = {
            .std = {
                    .clk_cfg = {
                            .sample_rate_hz = 48000,
                            .clk_src = I2S_CLK_SRC_DEFAULT,
                            .ext_clk_freq_hz = 0,
                            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
                        },
                    .slot_cfg = {
                            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                            .slot_mode = I2S_SLOT_MODE_STEREO,
                            .slot_mask = I2S_STD_SLOT_BOTH,
                            .ws_width = 16,
                            .ws_pol = false,
                            .bit_shift = true,
                            .left_align = true,
                            .big_endian = false,
                            .bit_order_lsb = false,
                        },
                    .gpio_cfg = {
                            .mclk = 42,
                            .bclk = 40,
                            .ws = 39,
                            .dout = 41,
                            .din = 3,
                            .invert_flags = {
                                    .mclk_inv = false,
                                    .bclk_inv = false,
                                    .ws_inv = false,
                                },
                        },
                },
        },
};

static periph_spi_config_t esp_bmgr_spi_display_cfg = {
    .spi_port = SPI2_HOST,
    .spi_bus_config = {
            .mosi_io_num = 46,
            .miso_io_num = 13,
            .sclk_io_num = 18,
            .quadwp_io_num = 11,
            .quadhd_io_num = 12,
            .data4_io_num = -1,
            .data5_io_num = -1,
            .data6_io_num = -1,
            .data7_io_num = -1,
            .data_io_default_level = false,
            .max_transfer_sz = 72000,
            .flags = 0,
            .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
            .intr_flags = 0,
        },
};

const static periph_ledc_config_t esp_bmgr_ledc_backlight_cfg = {
    .handle = {
            .channel = LEDC_CHANNEL_0,
            .speed_mode = LEDC_LOW_SPEED_MODE,
        },
    .gpio_num = 44,
    .duty = 2048,
    .freq_hz = 2000,
    .duty_resolution = LEDC_TIMER_10_BIT,
    .timer_sel = LEDC_TIMER_0,
    .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
    .output_invert = false,
};

const static periph_gpio_config_t esp_bmgr_gpio_power_lcd_cfg = {
    .gpio_config = {
            .pin_bit_mask = 9,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        },
    .default_level = 0,
};

const static periph_gpio_config_t esp_bmgr_gpio_power_audio_cfg = {
    .gpio_config = {
            .pin_bit_mask = 48,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        },
    .default_level = 1,
};

const static periph_gpio_config_t esp_bmgr_gpio_pa_control_cfg = {
    .gpio_config = {
            .pin_bit_mask = 15,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        },
    .default_level = 0,
};

// Peripheral descriptor array
const esp_board_periph_desc_t g_esp_board_peripherals[] = {
    {
        .next = &g_esp_board_peripherals[1],
        .name = "i2c_master",
        .type = "i2c",
        .format = NULL,
        .role = "master",
        .cfg = &esp_bmgr_i2c_master_cfg,
        .cfg_size = sizeof(esp_bmgr_i2c_master_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[2],
        .name = "i2s_audio_out",
        .type = "i2s",
        .format = "std-out",
        .role = "master",
        .cfg = &esp_bmgr_i2s_audio_out_cfg,
        .cfg_size = sizeof(esp_bmgr_i2s_audio_out_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[3],
        .name = "i2s_audio_in",
        .type = "i2s",
        .format = "std-in",
        .role = "slave",
        .cfg = &esp_bmgr_i2s_audio_in_cfg,
        .cfg_size = sizeof(esp_bmgr_i2s_audio_in_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[4],
        .name = "spi_display",
        .type = "spi",
        .format = NULL,
        .role = "master",
        .cfg = &esp_bmgr_spi_display_cfg,
        .cfg_size = sizeof(esp_bmgr_spi_display_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[5],
        .name = "ledc_backlight",
        .type = "ledc",
        .format = NULL,
        .role = "none",
        .cfg = &esp_bmgr_ledc_backlight_cfg,
        .cfg_size = sizeof(esp_bmgr_ledc_backlight_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[6],
        .name = "gpio_power_lcd",
        .type = "gpio",
        .format = NULL,
        .role = "none",
        .cfg = &esp_bmgr_gpio_power_lcd_cfg,
        .cfg_size = sizeof(esp_bmgr_gpio_power_lcd_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[7],
        .name = "gpio_power_audio",
        .type = "gpio",
        .format = NULL,
        .role = "none",
        .cfg = &esp_bmgr_gpio_power_audio_cfg,
        .cfg_size = sizeof(esp_bmgr_gpio_power_audio_cfg),
        .id = 0,
    },
    {
        .next = NULL,
        .name = "gpio_pa_control",
        .type = "gpio",
        .format = NULL,
        .role = "none",
        .cfg = &esp_bmgr_gpio_pa_control_cfg,
        .cfg_size = sizeof(esp_bmgr_gpio_pa_control_cfg),
        .id = 0,
    },
};

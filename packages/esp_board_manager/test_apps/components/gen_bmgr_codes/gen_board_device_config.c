/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Auto-generated device configuration file
 * DO NOT MODIFY THIS FILE MANUALLY
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_board_device.h"
#include "dev_audio_codec.h"
#include "dev_display_lcd_spi.h"
#include "dev_fatfs_sdcard.h"
#include "dev_gpio_ctrl.h"
#include "dev_lcd_touch_i2c.h"
#include "dev_ledc_ctrl.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_types.h"

// Device configuration structures
const static dev_gpio_ctrl_config_t esp_bmgr_audio_power_cfg = {
    .name = "audio_power",
    .type = "gpio_ctrl",
    .gpio_name = "gpio_power_audio",
    .active_level = 1,
    .default_level = 0,
};

const static dev_gpio_ctrl_config_t esp_bmgr_lcd_power_cfg = {
    .name = "lcd_power",
    .type = "gpio_ctrl",
    .gpio_name = "gpio_power_lcd",
    .active_level = 0,
    .default_level = 1,
};

const static dev_audio_codec_config_t esp_bmgr_audio_dac_cfg = {
    .name = "audio_dac",
    .chip = "es8311",
    .type = "audio_codec",
    .adc_enabled = false,
    .adc_max_channel = 0,
    .adc_channel_mask = 0x3,
    .adc_init_gain = 0,
    .dac_enabled = true,
    .dac_max_channel = 1,
    .dac_channel_mask = 0x1,
    .dac_init_gain = 0,
    .pa_cfg = {
            .name = "gpio_pa_control",
            .port = 15,
            .active_level = 1,
            .gain = 6.0,
        },
    .i2c_cfg = {
            .name = "i2c_master",
            .port = 0,
            .address = 48,
            .frequency = 400000,
        },
    .i2s_cfg = {
            .name = "i2s_audio_out",
            .port = 0,
        },
    .metadata = NULL,
    .metadata_size = 0,
    .mclk_enabled = true,
    .aec_enabled = false,
    .eq_enabled = false,
    .alc_enabled = false,
};

const static dev_audio_codec_config_t esp_bmgr_audio_adc_cfg = {
    .name = "audio_adc",
    .chip = "es7210",
    .type = "audio_codec",
    .adc_enabled = true,
    .adc_max_channel = 4,
    .adc_channel_mask = 0x7,
    .adc_init_gain = 0,
    .dac_enabled = false,
    .dac_max_channel = 0,
    .dac_channel_mask = 0x0,
    .dac_init_gain = 0,
    .pa_cfg = {
            .name = "none",
            .port = -1,
            .active_level = 0,
            .gain = 0.0,
        },
    .i2c_cfg = {
            .name = "i2c_master",
            .port = 0,
            .address = 128,
            .frequency = 400000,
        },
    .i2s_cfg = {
            .name = "i2s_audio_in",
            .port = 0,
        },
    .metadata = NULL,
    .metadata_size = 0,
    .mclk_enabled = false,
    .aec_enabled = false,
    .eq_enabled = false,
    .alc_enabled = false,
};

const static dev_fatfs_sdcard_config_t esp_bmgr_fs_sdcard_cfg = {
    .name = "fs_sdcard",
    .mount_point = "/sdcard",
    .vfs_config = {
            .format_if_mount_failed = true,
            .max_files = 5,
            .allocation_unit_size = 16384,
        },
    .frequency = SDMMC_FREQ_HIGHSPEED,
    .slot = SDMMC_HOST_SLOT_1,
    .bus_width = 1,
    .slot_flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP,
    .pins = {
            .clk = 16,
            .cmd = 38,
            .d0 = 17,
            .d1 = -1,
            .d2 = -1,
            .d3 = -1,
            .d4 = -1,
            .d5 = -1,
            .d6 = -1,
            .d7 = -1,
            .cd = -1,
            .wp = -1,
        },
};

const static dev_display_lcd_spi_config_t esp_bmgr_display_lcd_cfg = {
    .name = "display_lcd",
    .chip = "st77916",
    .type = "display_lcd_spi",
    .spi_name = "spi_display",
    .io_spi_config = {
            .cs_gpio_num = 14,
            .dc_gpio_num = 45,
            .spi_mode = 0,
            .pclk_hz = 40000000,
            .trans_queue_depth = 2,
            .on_color_trans_done = NULL,
            .user_ctx = NULL,
            .lcd_cmd_bits = 32,
            .lcd_param_bits = 8,
            .cs_ena_pretrans = 0,
            .cs_ena_posttrans = 0,
            .flags = {
                    .dc_high_on_cmd = false,
                    .dc_low_on_data = false,
                    .dc_low_on_param = false,
                    .octal_mode = false,
                    .quad_mode = true,
                    .sio_mode = false,
                    .lsb_first = false,
                    .cs_high_active = false,
                },
        },
    .panel_config = {
            .reset_gpio_num = 47,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
            .bits_per_pixel = 16,
            .flags = {
                    .reset_active_high = true,
                },
            .vendor_config = NULL,
        },
    .swap_xy = false,
    .mirror_x = false,
    .mirror_y = false,
    .x_max = 360,
    .y_max = 360,
};

const static dev_lcd_touch_i2c_config_t esp_bmgr_lcd_touch_cfg = {
    .name = "lcd_touch",
    .chip = "cst816s",
    .type = "lcd_touch_i2c",
    .i2c_name = "i2c_master",
    .i2c_addr = {0x15, 0x00},
    .io_i2c_config = {
            .dev_addr = 21,
            .control_phase_bytes = 1,
            .dc_bit_offset = 0,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 0,
            .scl_speed_hz = 100000,
            .flags = {
                    .dc_low_on_data = false,
                    .disable_control_phase = true,
                },
        },
    .touch_config = {
            .x_max = 360,
            .y_max = 360,
            .rst_gpio_num = -1,
            .int_gpio_num = 10,
            .levels = {
                    .reset = 0,
                    .interrupt = 0,
                },
            .flags = {
                    .swap_xy = false,
                    .mirror_x = false,
                    .mirror_y = false,
                },
            .process_coordinates = NULL,
            .interrupt_callback = NULL,
            .user_data = NULL,
            .driver_data = NULL,
        },
};

const static dev_ledc_ctrl_config_t esp_bmgr_lcd_brightness_cfg = {
    .name = "lcd_brightness",
    .type = "ledc_ctrl",
    .ledc_name = "ledc_backlight",
    .default_percent = 100,
};

// Device descriptor array
const esp_board_device_desc_t g_esp_board_devices[] = {
    {
        .next = &g_esp_board_devices[1],
        .name = "audio_power",
        .type = "gpio_ctrl",
        .cfg = &esp_bmgr_audio_power_cfg,
        .cfg_size = sizeof(esp_bmgr_audio_power_cfg),
    },
    {
        .next = &g_esp_board_devices[2],
        .name = "lcd_power",
        .type = "gpio_ctrl",
        .cfg = &esp_bmgr_lcd_power_cfg,
        .cfg_size = sizeof(esp_bmgr_lcd_power_cfg),
    },
    {
        .next = &g_esp_board_devices[3],
        .name = "audio_dac",
        .type = "audio_codec",
        .cfg = &esp_bmgr_audio_dac_cfg,
        .cfg_size = sizeof(esp_bmgr_audio_dac_cfg),
    },
    {
        .next = &g_esp_board_devices[4],
        .name = "audio_adc",
        .type = "audio_codec",
        .cfg = &esp_bmgr_audio_adc_cfg,
        .cfg_size = sizeof(esp_bmgr_audio_adc_cfg),
    },
    {
        .next = &g_esp_board_devices[5],
        .name = "fs_sdcard",
        .type = "fatfs_sdcard",
        .cfg = &esp_bmgr_fs_sdcard_cfg,
        .cfg_size = sizeof(esp_bmgr_fs_sdcard_cfg),
    },
    {
        .next = &g_esp_board_devices[6],
        .name = "display_lcd",
        .type = "display_lcd_spi",
        .cfg = &esp_bmgr_display_lcd_cfg,
        .cfg_size = sizeof(esp_bmgr_display_lcd_cfg),
    },
    {
        .next = &g_esp_board_devices[7],
        .name = "lcd_touch",
        .type = "lcd_touch_i2c",
        .cfg = &esp_bmgr_lcd_touch_cfg,
        .cfg_size = sizeof(esp_bmgr_lcd_touch_cfg),
    },
    {
        .next = NULL,
        .name = "lcd_brightness",
        .type = "ledc_ctrl",
        .cfg = &esp_bmgr_lcd_brightness_cfg,
        .cfg_size = sizeof(esp_bmgr_lcd_brightness_cfg),
    },
};

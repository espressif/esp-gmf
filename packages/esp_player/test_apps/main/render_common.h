/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_audio_render.h"
#include "esp_gmf_io_codec_dev.h"
#include "esp_gmf_pool.h"

#include "esp_player.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

esp_player_err_t audio_render_create_handle(esp_audio_render_stream_handle_t *stream_handle,
                                            uint32_t sample_rate,
                                            uint8_t bits_per_sample,
                                            uint8_t channels,
                                            esp_audio_render_stream_id_t stream_id);

void audio_render_destroy_handle(void);

void audio_render_set_max_stream_num(uint8_t max_stream_num);

esp_player_err_t video_render_create_handle(void **render_handle, uint32_t pixel_format, uint32_t fps);

void video_render_destroy_handle(void);

#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT
void video_render_reconfig_lcd(void);
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT */

/** UT-only: init video render + one dummy frame draw before Unity tests */
esp_player_err_t video_render_test_app_setup(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

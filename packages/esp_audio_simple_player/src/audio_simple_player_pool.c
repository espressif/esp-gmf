/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_audio_dec.h"
#include "esp_audio_simple_player_private.h"

static const char *TAG = "ASP_POOL";

void asp_pool_register_io(esp_asp_handle_t handle)
{
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)handle;
#ifdef CONFIG_ESP_AUDIO_SIMPLE_PLAYER_HTTP_EN
#include "esp_gmf_io_http.h"
    http_io_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    esp_gmf_io_handle_t http = NULL;
    http_cfg.dir = ESP_GMF_IO_DIR_READER;
    http_cfg.event_handle = NULL;
    esp_gmf_io_http_init(&http_cfg, &http);
    esp_gmf_pool_register_io(player->pool, http, NULL);
#endif  /* CONFIG_ESP_AUDIO_SIMPLE_PLAYER_HTTP_EN */

#ifdef CONFIG_ESP_AUDIO_SIMPLE_PLAYER_FILE_EN
#include "esp_gmf_io_file.h"
    file_io_cfg_t fs_cfg = FILE_IO_CFG_DEFAULT();
    fs_cfg.dir = ESP_GMF_IO_DIR_READER;
    esp_gmf_io_handle_t fs = NULL;
    esp_gmf_io_file_init(&fs_cfg, &fs);
    esp_gmf_pool_register_io(player->pool, fs, NULL);
#endif  /* CONFIG_ESP_AUDIO_SIMPLE_PLAYER_FILE_EN */

#ifdef CONFIG_ESP_AUDIO_SIMPLE_PLAYER_EMBED_FLASH_EN
#include "esp_gmf_io_embed_flash.h"
    embed_flash_io_cfg_t flash_cfg = EMBED_FLASH_CFG_DEFAULT();
    esp_gmf_io_handle_t flash = NULL;
    esp_gmf_io_embed_flash_init(&flash_cfg, &flash);
    esp_gmf_pool_register_io(player->pool, flash, NULL);
#endif  /* CONFIG_ESP_AUDIO_SIMPLE_PLAYER_EMBED_FLASH_EN */
}

void asp_pool_register_audio(esp_asp_handle_t handle)
{
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)handle;
    esp_audio_simple_dec_cfg_t es_dec_cfg = DEFAULT_ESP_GMF_AUDIO_DEC_CONFIG();
    esp_gmf_element_handle_t es_hd = NULL;
    esp_gmf_audio_dec_init(&es_dec_cfg, &es_hd);
    esp_gmf_pool_register_element(player->pool, es_hd, NULL);

#ifdef CONFIG_ESP_AUDIO_SIMPLE_PLAYER_RESAMPLE_EN
#include "esp_gmf_rate_cvt.h"
    esp_ae_rate_cvt_cfg_t rate_cvt_cfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
    rate_cvt_cfg.dest_rate = CONFIG_AUDIO_SIMPLE_PLAYER_RESAMPLE_DEST_RATE;
    esp_gmf_element_handle_t rate_hd = NULL;
    esp_gmf_rate_cvt_init(&rate_cvt_cfg, &rate_hd);
    esp_gmf_pool_register_element(player->pool, rate_hd, NULL);
    ESP_LOGI(TAG, "Dest rate:%ld", rate_cvt_cfg.dest_rate);
#endif  /* CONFIG_ESP_AUDIO_SIMPLE_PLAYER_RESAMPLE_EN */

#ifdef CONFIG_ESP_AUDIO_SIMPLE_PLAYER_CH_CVT_EN
#include "esp_gmf_ch_cvt.h"
    esp_ae_ch_cvt_cfg_t ch_cvt_cfg = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
    esp_gmf_element_handle_t ch_hd = NULL;
    ch_cvt_cfg.dest_ch = CONFIG_AUDIO_SIMPLE_PLAYER_CH_CVT_DEST;
    esp_gmf_ch_cvt_init(&ch_cvt_cfg, &ch_hd);
    esp_gmf_pool_register_element(player->pool, ch_hd, NULL);
    ESP_LOGI(TAG, "Dest channels:%d", ch_cvt_cfg.dest_ch);
#endif  /* CONFIG_ESP_AUDIO_SIMPLE_PLAYER_CH_CVT_EN */

#ifdef CONFIG_ESP_AUDIO_SIMPLE_PLAYER_BIT_CVT_EN
#include "esp_gmf_bit_cvt.h"
    esp_ae_bit_cvt_cfg_t bit_cvt_cfg = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
    bit_cvt_cfg.dest_bits = CONFIG_AUDIO_SIMPLE_PLAYER_BIT_CVT_DEST_BITS;
    esp_gmf_element_handle_t bit_hd = NULL;
    esp_gmf_bit_cvt_init(&bit_cvt_cfg, &bit_hd);
    esp_gmf_pool_register_element(player->pool, bit_hd, NULL);
    ESP_LOGI(TAG, "Dest bits:%d", bit_cvt_cfg.dest_bits);
#endif  /* CONFIG_ESP_AUDIO_SIMPLE_PLAYER_BIT_CVT_EN */
}

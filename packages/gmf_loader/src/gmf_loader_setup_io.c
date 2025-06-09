/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_gmf_err.h"
#include "esp_gmf_io.h"
#include "esp_gmf_io_codec_dev.h"
#include "esp_gmf_io_embed_flash.h"
#include "esp_gmf_io_file.h"
#include "esp_gmf_io_http.h"
#include "esp_gmf_pool.h"

#include "esp_codec_dev.h"

static const char *TAG = "GMF_SETUP_IO";

#if defined(CONFIG_GMF_IO_INIT_CODEC_DEV_RX) || defined(CONFIG_GMF_IO_INIT_CODEC_DEV_TX)
static esp_gmf_err_t gmf_loader_setup_default_codec_dev(esp_gmf_pool_handle_t pool, esp_gmf_io_dir_t dir)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);

    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_io_handle_t dev = NULL;
    codec_dev_io_cfg_t tx_codec_dev_cfg = ESP_GMF_IO_CODEC_DEV_CFG_DEFAULT();
    tx_codec_dev_cfg.dir = dir;
    tx_codec_dev_cfg.dev = NULL;
    ret = esp_gmf_io_codec_dev_init(&tx_codec_dev_cfg, &dev);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init codec dev io");
    ret = esp_gmf_pool_register_io(pool, dev, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_io_deinit(dev); return ret;}, "Failed to register codec dev io");
    return ret;
}
#endif  /* CONFIG_GMF_IO_INIT_CODEC_DEV_RX || CONFIG_GMF_IO_INIT_CODEC_DEV_TX */

#if defined(CONFIG_GMF_IO_INIT_FILE_READER) || defined(CONFIG_GMF_IO_INIT_FILE_WRITER)
static esp_gmf_err_t gmf_loader_setup_default_fs_io(esp_gmf_pool_handle_t pool, esp_gmf_io_dir_t dir)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);

    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    file_io_cfg_t fs_cfg = FILE_IO_CFG_DEFAULT();
    esp_gmf_io_handle_t hd = NULL;
    fs_cfg.dir = dir;
    ret = esp_gmf_io_file_init(&fs_cfg, &hd);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init file io");
    ret = esp_gmf_pool_register_io(pool, hd, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_io_deinit(hd); return ret;}, "Failed to register file io");
    return ret;
}
#endif  /* CONFIG_GMF_IO_INIT_FILE_READER || CONFIG_GMF_IO_INIT_FILE_WRITER */

#if defined(CONFIG_GMF_IO_INIT_FLASH_READER)
static esp_gmf_err_t gmf_loader_setup_default_flash_io(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);

    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_io_handle_t hd = NULL;
    embed_flash_io_cfg_t flash_cfg = EMBED_FLASH_CFG_DEFAULT();
    ret = esp_gmf_io_embed_flash_init(&flash_cfg, &hd);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init flash io");
    ret = esp_gmf_pool_register_io(pool, hd, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_io_deinit(hd); return ret;}, "Failed to register flash io");
    return ret;
}
#endif  /* CONFIG_GMF_IO_INIT_FLASH_READER */

#if defined(CONFIG_GMF_IO_INIT_HTTP_READER) || defined(CONFIG_GMF_IO_INIT_HTTP_WRITER)
static esp_gmf_err_t gmf_loader_setup_default_http_io(esp_gmf_pool_handle_t pool, esp_gmf_io_dir_t dir)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);

    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_io_handle_t hd = NULL;
    http_io_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_cfg.dir = dir;
    http_cfg.event_handle = NULL;
    if (dir == ESP_GMF_IO_DIR_READER) {
#ifdef CONFIG_GMF_IO_INIT_HTTP_READER
        http_cfg.out_buf_size = CONFIG_GMF_IO_HTTP_READER_OUT_BUF_SIZE;
        if (CONFIG_GMF_IO_HTTP_READER_CERT_PEM[0] == '\0') {
            http_cfg.cert_pem = NULL;
        } else {
            http_cfg.cert_pem = CONFIG_GMF_IO_HTTP_READER_CERT_PEM;
        }
        http_cfg.task_stack = CONFIG_GMF_IO_HTTP_READER_TASK_STACK;
#ifdef CONFIG_GMF_IO_HTTP_READER_STACK_IN_EXT
        http_cfg.stack_in_ext = true;
#else
        http_cfg.stack_in_ext = false;
#endif  /* CONFIG_GMF_IO_HTTP_READER_STACK_IN_EXT */
        http_cfg.task_core = CONFIG_GMF_IO_HTTP_READER_TASK_CORE;
        http_cfg.task_prio = CONFIG_GMF_IO_HTTP_READER_TASK_PRIORITY;
#endif  /* CONFIG_GMF_IO_INIT_HTTP_READER */
    } else {
#ifdef CONFIG_GMF_IO_INIT_HTTP_WRITER
        http_cfg.out_buf_size = CONFIG_GMF_IO_HTTP_WRITER_OUT_BUF_SIZE;
        if (CONFIG_GMF_IO_HTTP_WRITER_CERT_PEM[0] == '\0') {
            http_cfg.cert_pem = NULL;
        } else {
            http_cfg.cert_pem = CONFIG_GMF_IO_HTTP_WRITER_CERT_PEM;
        }
        http_cfg.task_stack = CONFIG_GMF_IO_HTTP_WRITER_TASK_STACK;
#ifdef CONFIG_GMF_IO_HTTP_WRITER_STACK_IN_EXT
        http_cfg.stack_in_ext = true;
#else
        http_cfg.stack_in_ext = false;
#endif  /* CONFIG_GMF_IO_HTTP_WRITER_STACK_IN_EXT */
        http_cfg.task_core = CONFIG_GMF_IO_HTTP_WRITER_TASK_CORE;
        http_cfg.task_prio = CONFIG_GMF_IO_HTTP_WRITER_TASK_PRIORITY;
#endif  /* CONFIG_GMF_IO_INIT_HTTP_WRITER */
    }

    ret = esp_gmf_io_http_init(&http_cfg, &hd);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init http io");
    ret = esp_gmf_pool_register_io(pool, hd, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_io_deinit(hd); return ret;}, "Failed to register http io");
    return ret;
}
#endif  /* CONFIG_GMF_IO_INIT_HTTP_READER || CONFIG_GMF_IO_INIT_HTTP_WRITER */

esp_gmf_err_t gmf_loader_setup_io_default(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;

#ifdef CONFIG_GMF_IO_INIT_CODEC_DEV_RX
    ret = gmf_loader_setup_default_codec_dev(pool, ESP_GMF_IO_DIR_READER);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register codec dev io");
#endif  /* CONFIG_GMF_IO_INIT_CODEC_DEV_RX */

#ifdef CONFIG_GMF_IO_INIT_CODEC_DEV_TX
    ret = gmf_loader_setup_default_codec_dev(pool, ESP_GMF_IO_DIR_WRITER);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register codec dev io");
#endif  /* CONFIG_GMF_IO_INIT_CODEC_DEV_TX */

#ifdef CONFIG_GMF_IO_INIT_FILE_READER
    ret = gmf_loader_setup_default_fs_io(pool, ESP_GMF_IO_DIR_READER);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register fs io");
#endif  /* CONFIG_GMF_IO_INIT_FILE_READER */

#ifdef CONFIG_GMF_IO_INIT_FILE_WRITER
    ret = gmf_loader_setup_default_fs_io(pool, ESP_GMF_IO_DIR_WRITER);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register fs io");
#endif  /* CONFIG_GMF_IO_INIT_FILE_WRITER */

#ifdef CONFIG_GMF_IO_INIT_HTTP_READER
    ret = gmf_loader_setup_default_http_io(pool, ESP_GMF_IO_DIR_READER);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register http io");
#endif  /* CONFIG_GMF_IO_INIT_HTTP_READER */

#ifdef CONFIG_GMF_IO_INIT_HTTP_WRITER
    ret = gmf_loader_setup_default_http_io(pool, ESP_GMF_IO_DIR_WRITER);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register http io");
#endif  /* CONFIG_GMF_IO_INIT_HTTP_WRITER */

#ifdef CONFIG_GMF_IO_INIT_FLASH_READER
    ret = gmf_loader_setup_default_flash_io(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register flash io");
#endif  /* CONFIG_GMF_IO_INIT_FLASH_READER */

    return ret;
}

esp_gmf_err_t gmf_loader_teardown_io_default(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);
    return ESP_GMF_ERR_OK;
}

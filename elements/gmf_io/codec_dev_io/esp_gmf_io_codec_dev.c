/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>
#include <string.h>
#include "esp_gmf_io_codec_dev.h"
#include "esp_gmf_oal_mem.h"
#include "esp_log.h"

/**
 * @brief Codec device io context in GMF
 */
typedef struct {
    esp_gmf_io_t base;    /*!< The GMF codec dev io handle */
    bool         is_open; /*!< The flag of whether opened */
} codec_dev_io_stream_t;

static const char *TAG = "ESP_GMF_CODEC_DEV";

static esp_gmf_err_t esp_gmf_io_codec_dev_new(void *cfg, esp_gmf_obj_handle_t *io)
{
    return esp_gmf_io_codec_dev_init(cfg, io);
}

static esp_gmf_err_t _codec_dev_open(esp_gmf_io_handle_t io)
{
    codec_dev_io_stream_t *codec_dev_io = (codec_dev_io_stream_t *)io;
    codec_dev_io_cfg_t *cfg = (codec_dev_io_cfg_t *)OBJ_GET_CFG(codec_dev_io);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL;);
    ESP_GMF_CHECK(TAG, cfg->dev, {return ESP_GMF_ERR_FAIL;}, "There is no activated I2S driver handle");
    codec_dev_io->is_open = true;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_io_t _codec_dev_acquire_read(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted_size, int block_ticks)
{
    codec_dev_io_stream_t *codec_dev_io = (codec_dev_io_stream_t *)handle;
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    codec_dev_io_cfg_t *cfg = (codec_dev_io_cfg_t *)OBJ_GET_CFG(codec_dev_io);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_IO_FAIL;);
    if (esp_codec_dev_read(cfg->dev, pload->buf, wanted_size) != ESP_GMF_IO_OK) {
        ESP_LOGE(TAG, "Read failed, wanted: %ld", wanted_size);
        return ESP_GMF_IO_FAIL;
    }
    pload->valid_size = wanted_size;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t _codec_dev_release_read(esp_gmf_io_handle_t handle, void *payload, int block_ticks)
{
    codec_dev_io_stream_t *codec_dev_io = (codec_dev_io_stream_t *)handle;
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    esp_gmf_info_file_t info = {0};
    esp_gmf_io_get_info((esp_gmf_io_handle_t)codec_dev_io, &info);
    ESP_LOGD(TAG, "Update len = %d, pos = %d/%d", pload->valid_size, (int)info.pos, (int)info.size);
    esp_gmf_io_update_pos((esp_gmf_io_handle_t)handle, pload->valid_size);
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t _codec_dev_acquire_write(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted_size, int block_ticks)
{
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t _codec_dev_release_write(esp_gmf_io_handle_t handle, void *payload, int block_ticks)
{
    codec_dev_io_stream_t *codec_dev_io = (codec_dev_io_stream_t *)handle;
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    codec_dev_io_cfg_t *cfg = (codec_dev_io_cfg_t *)OBJ_GET_CFG(codec_dev_io);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_IO_FAIL;);
    size_t wlen = pload->valid_size;
    if (esp_codec_dev_write(cfg->dev, pload->buf, pload->valid_size) != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Write failed, valid: %d", pload->valid_size);
        return ESP_GMF_IO_FAIL;
    }
    esp_gmf_info_file_t info = {0};
    if (wlen > 0) {
        esp_gmf_io_update_pos((esp_gmf_io_handle_t)handle, wlen);
    }
    esp_gmf_io_get_info((esp_gmf_io_handle_t)codec_dev_io, &info);
    ESP_LOGD(TAG, "Write len: %d, pos: %d/%d", pload->valid_size, (int)info.pos, (int)info.size);
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_t _codec_dev_seek(esp_gmf_io_handle_t io, uint64_t seek_byte_pos)
{
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _codec_dev_close(esp_gmf_io_handle_t io)
{
    codec_dev_io_stream_t *codec_dev_io = (codec_dev_io_stream_t *)io;
    esp_gmf_info_file_t info = {0};
    esp_gmf_io_get_info((esp_gmf_io_handle_t)codec_dev_io, &info);
    ESP_LOGI(TAG, "CLose, %p, pos = %d/%d", codec_dev_io, (int)info.pos, (int)info.size);
    if (codec_dev_io->is_open) {
        codec_dev_io->is_open = false;
    }
    esp_gmf_io_set_pos((esp_gmf_io_handle_t)io, 0);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _codec_dev_delete(esp_gmf_io_handle_t io)
{
    if (io != NULL) {
        codec_dev_io_stream_t *codec_dev_io = (codec_dev_io_stream_t *)io;
        ESP_LOGD(TAG, "Delete, %s-%p", OBJ_GET_TAG(codec_dev_io), codec_dev_io);
        esp_gmf_oal_free(OBJ_GET_CFG(codec_dev_io));
        esp_gmf_io_deinit(io);
        esp_gmf_oal_free(codec_dev_io);
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_io_codec_dev_init(codec_dev_io_cfg_t *config, esp_gmf_io_handle_t *io)
{
    ESP_GMF_NULL_CHECK(TAG, config, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, io, {return ESP_GMF_ERR_INVALID_ARG;});
    *io = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    codec_dev_io_stream_t *codec_dev_io = esp_gmf_oal_calloc(1, sizeof(codec_dev_io_stream_t));
    ESP_GMF_MEM_VERIFY(TAG, codec_dev_io, return ESP_GMF_ERR_MEMORY_LACK,
                       "codec device", sizeof(codec_dev_io_stream_t));
    codec_dev_io->base.dir = config->dir;
    codec_dev_io->base.type = ESP_GMF_IO_TYPE_BYTE;
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)codec_dev_io;
    obj->new_obj = esp_gmf_io_codec_dev_new;
    obj->del_obj = _codec_dev_delete;
    codec_dev_io_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto _codec_dev_fail;},
                       "codec device configuration", sizeof(*config));
    memcpy(cfg, config, sizeof(*config));
    esp_gmf_obj_set_config(obj, cfg, sizeof(*config));
    ret = esp_gmf_obj_set_tag(obj, (config->name == NULL ? "io_codec_dev" : config->name));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto _codec_dev_fail, "Failed to set obj tag");
    codec_dev_io->base.close = _codec_dev_close;
    codec_dev_io->base.open = _codec_dev_open;
    codec_dev_io->base.seek = _codec_dev_seek;
    codec_dev_io->base.reset = NULL;
    esp_gmf_io_init(obj, NULL);
    if (codec_dev_io->base.dir == ESP_GMF_IO_DIR_WRITER) {
        codec_dev_io->base.acquire_write = _codec_dev_acquire_write;
        codec_dev_io->base.release_write = _codec_dev_release_write;
    } else if (codec_dev_io->base.dir == ESP_GMF_IO_DIR_READER) {
        codec_dev_io->base.acquire_read = _codec_dev_acquire_read;
        codec_dev_io->base.release_read = _codec_dev_release_read;
    } else {
        ESP_LOGE(TAG, "Does not set read or write function");
        ret = ESP_GMF_ERR_NOT_SUPPORT;
        goto _codec_dev_fail;
    }
    *io = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), codec_dev_io);
    return ESP_GMF_ERR_OK;
_codec_dev_fail:
    esp_gmf_obj_delete(obj);
    return ret;
}

esp_gmf_err_t esp_gmf_io_codec_dev_set_dev(esp_gmf_io_handle_t io, esp_codec_dev_handle_t dev)
{
    codec_dev_io_stream_t *codec_dev_io = (codec_dev_io_stream_t *)io;
    ESP_GMF_NULL_CHECK(TAG, codec_dev_io, return ESP_GMF_ERR_INVALID_ARG;);

    codec_dev_io_cfg_t *cfg = (codec_dev_io_cfg_t *)OBJ_GET_CFG(codec_dev_io);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_INVALID_STATE;);

    cfg->dev = dev;
    return ESP_GMF_ERR_OK;
}

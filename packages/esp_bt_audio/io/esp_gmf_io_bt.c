/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

#include "esp_gmf_err.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_io_bt.h"

#include "esp_bt_audio_stream.h"

typedef struct {
    esp_gmf_io_t                  base;    /*!< The GMF bluetooth io handle */
    esp_bt_audio_stream_packet_t  packet;  /*!< The packet of Bluetooth stream */
} bt_io_stream_t;

static const char *TAG = "ESP_GMF_IO_BT";

static esp_gmf_err_t _bt_new(void *cfg, esp_gmf_obj_handle_t *io)
{
    return esp_gmf_io_bt_init(cfg, io);
}

static esp_gmf_err_t _bt_delete(esp_gmf_obj_handle_t io)
{
    bt_io_stream_t *bt_io = (bt_io_stream_t *)io;
    ESP_LOGD(TAG, "Delete, %s-%p", OBJ_GET_TAG(bt_io), bt_io);
    void *cfg = OBJ_GET_CFG(io);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_io_deinit(io);
    esp_gmf_oal_free(bt_io);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _bt_open(esp_gmf_io_handle_t io)
{
    ESP_LOGD(TAG, "Open, %s-%p", OBJ_GET_TAG(io), io);
    bt_io_cfg_t *cfg = (bt_io_cfg_t *)OBJ_GET_CFG(io);
    if (cfg->stream == NULL) {
        ESP_LOGE(TAG, "Error open Bluetooth I/O, stream = NULL");
        return ESP_GMF_ERR_FAIL;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _bt_close(esp_gmf_io_handle_t io)
{
    bt_io_stream_t *bt_io = (bt_io_stream_t *)io;
    memset(&bt_io->packet, 0, sizeof(esp_bt_audio_stream_packet_t));
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_io_t _bt_acquire_read(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted_size, int block_ticks)
{
    bt_io_cfg_t *cfg = (bt_io_cfg_t *)OBJ_GET_CFG(handle);
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    bt_io_stream_t *bt_io = (bt_io_stream_t *)handle;
    esp_err_t ret = esp_bt_audio_stream_acquire_read(cfg->stream, &bt_io->packet, pdTICKS_TO_MS(block_ticks));
    if (ret == ESP_OK) {
        pload->buf = bt_io->packet.data;
        pload->valid_size = bt_io->packet.size;
        pload->buf_length = bt_io->packet.size;
        pload->meta_flag = bt_io->packet.bad_frame ? ESP_GMF_META_FLAG_AUD_RECOVERY_PLC : 0;
        pload->is_done = bt_io->packet.is_done;
    } else {
        pload->buf = NULL;
        pload->valid_size = 0;
        pload->buf_length = 0;
        pload->meta_flag = 0;
        pload->is_done = 0;
    }
    if (ret == ESP_OK || ret == ESP_ERR_TIMEOUT) {
        // If read timeout, return OK to continue waiting for next read
        return ESP_GMF_IO_OK;
    } else {
        return ESP_GMF_IO_FAIL;
    }
}

static esp_gmf_err_io_t _bt_release_read(esp_gmf_io_handle_t handle, void *payload, int block_ticks)
{
    bt_io_cfg_t *cfg = (bt_io_cfg_t *)OBJ_GET_CFG(handle);
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    bt_io_stream_t *bt_io = (bt_io_stream_t *)handle;
    esp_bt_audio_stream_release_read(cfg->stream, &bt_io->packet);
    pload->buf = NULL;
    pload->valid_size = 0;
    pload->meta_flag = 0;
    memset(&bt_io->packet, 0, sizeof(esp_bt_audio_stream_packet_t));
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t _bt_acquire_write(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted_size, int block_ticks)
{
    bt_io_cfg_t *cfg = (bt_io_cfg_t *)OBJ_GET_CFG(handle);
    if (cfg->stream == NULL) {
        ESP_LOGE(TAG, "Error acquire write bt io, stream = NULL");
        return ESP_GMF_IO_FAIL;
    }
    (void)block_ticks;
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    bt_io_stream_t *bt_io = (bt_io_stream_t *)handle;
    memset(&bt_io->packet, 0, sizeof(esp_bt_audio_stream_packet_t));
    esp_err_t ret = esp_bt_audio_stream_acquire_write(cfg->stream, &bt_io->packet, wanted_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error acquire write bt io, ret=%d", ret);
        pload->buf = NULL;
        pload->buf_length = 0;
        pload->valid_size = 0;
        return ESP_GMF_IO_FAIL;
    }
    pload->buf = bt_io->packet.data;
    pload->buf_length = bt_io->packet.size;
    pload->valid_size = bt_io->packet.size;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t _bt_release_write(esp_gmf_io_handle_t handle, void *payload, int block_ticks)
{
    bt_io_cfg_t *cfg = (bt_io_cfg_t *)OBJ_GET_CFG(handle);
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    bt_io_stream_t *bt_io = (bt_io_stream_t *)handle;
    bt_io->packet.size = pload->valid_size;
    bt_io->packet.bad_frame = pload->meta_flag & ESP_GMF_META_FLAG_AUD_RECOVERY_PLC;
    bt_io->packet.is_done = pload->is_done;
    esp_err_t ret = esp_bt_audio_stream_release_write(cfg->stream, &bt_io->packet, pdTICKS_TO_MS(block_ticks));
    ret = ret == ESP_OK ? ESP_GMF_IO_OK : ESP_GMF_IO_FAIL;
    pload->buf = NULL;
    pload->buf_length = 0;
    pload->valid_size = 0;
    memset(&bt_io->packet, 0, sizeof(esp_bt_audio_stream_packet_t));
    return ret;
}

esp_gmf_err_t esp_gmf_io_bt_init(bt_io_cfg_t *config, esp_gmf_io_handle_t *io)
{
    ESP_GMF_NULL_CHECK(TAG, config, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, io, {return ESP_GMF_ERR_INVALID_ARG;});
    *io = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    bt_io_stream_t *bt_io = esp_gmf_oal_calloc(1, sizeof(bt_io_stream_t));
    ESP_GMF_MEM_VERIFY(TAG, bt_io, return ESP_GMF_ERR_MEMORY_LACK,
                       "bt stream", sizeof(bt_io_stream_t));
    bt_io->base.dir = config->dir;
    bt_io->base.type = ESP_GMF_IO_TYPE_BLOCK;
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)bt_io;
    obj->new_obj = _bt_new;
    obj->del_obj = _bt_delete;
    bt_io_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto _bt_init_fail;},
                       "bt stream configuration", sizeof(*config));
    memcpy(cfg, config, sizeof(*config));
    esp_gmf_obj_set_config(obj, cfg, sizeof(*config));
    ret = esp_gmf_obj_set_tag(obj, "io_bt");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto _bt_init_fail, "Failed to set obj tag");
    bt_io->base.close = _bt_close;
    bt_io->base.open = _bt_open;
    bt_io->base.seek = NULL;
    bt_io->base.reset = NULL;
    esp_gmf_io_init(obj, NULL);
    if (config->dir == ESP_GMF_IO_DIR_WRITER) {
        bt_io->base.acquire_write = _bt_acquire_write;
        bt_io->base.release_write = _bt_release_write;
    } else if (config->dir == ESP_GMF_IO_DIR_READER) {
        bt_io->base.acquire_read = _bt_acquire_read;
        bt_io->base.release_read = _bt_release_read;
    } else {
        ESP_LOGW(TAG, "Does not set read or write function");
        ret = ESP_GMF_ERR_NOT_SUPPORT;
        goto _bt_init_fail;
    }
    *io = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), bt_io);

    return ESP_GMF_ERR_OK;
_bt_init_fail:
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    return ret;
}

esp_gmf_err_t esp_gmf_io_bt_set_stream(esp_gmf_io_handle_t io, esp_bt_audio_stream_handle_t stream)
{
    ESP_GMF_NULL_CHECK(TAG, io, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, stream, {return ESP_GMF_ERR_INVALID_ARG;});
    bt_io_cfg_t *cfg = (bt_io_cfg_t *)OBJ_GET_CFG(io);
    ESP_GMF_NULL_CHECK(TAG, cfg, {return ESP_GMF_ERR_INVALID_STATE;});

    esp_bt_audio_stream_dir_t dir = ESP_BT_AUDIO_STREAM_DIR_UNKNOWN;
    if (esp_bt_audio_stream_get_dir(stream, &dir) != ESP_OK) {
        ESP_LOGE(TAG, "Error set bt io stream, get dir failed, stream=%p", stream);
        return ESP_GMF_ERR_FAIL;
    }

    if ((dir == ESP_BT_AUDIO_STREAM_DIR_SINK && cfg->dir == ESP_GMF_IO_DIR_READER) ||
        (dir == ESP_BT_AUDIO_STREAM_DIR_SOURCE && cfg->dir == ESP_GMF_IO_DIR_WRITER)) {
        cfg->stream = stream;
    } else {
        ESP_LOGE(TAG, "Error set bt io stream, stream = %p, dir = %d", stream, cfg->dir);
        return ESP_GMF_ERR_FAIL;
    }

    return ESP_GMF_ERR_OK;
}

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "esp_gmf_err.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_io_bt.h"

#include "esp_bt_audio_stream.h"

typedef struct {
    esp_gmf_io_t                  base;         /*!< The GMF bluetooth io handle */
    esp_bt_audio_stream_packet_t  packet;       /*!< Stream packet; data/size also hold discard scratch */
    bool                          discard;      /*!< Discard writes instead of sending to stream */
    uint32_t                      pace_us;      /*!< Discard pace period in us, 0 to run free */
    int64_t                       pace_next_us; /*!< Time the next discarded frame is due */
} bt_io_stream_t;

static const char *TAG = "ESP_GMF_IO_BT";

static bool _bt_owns_packet_data(const bt_io_stream_t *bt_io)
{
    return bt_io->packet.data != NULL && bt_io->packet.data_owner == (void *)bt_io;
}

static void _bt_free_owned_packet_data(bt_io_stream_t *bt_io)
{
    if (_bt_owns_packet_data(bt_io)) {
        esp_gmf_oal_free(bt_io->packet.data);
    }
    memset(&bt_io->packet, 0, sizeof(bt_io->packet));
}

static void _bt_clear_payload(esp_gmf_payload_t *pload)
{
    pload->buf = NULL;
    pload->buf_length = 0;
    pload->valid_size = 0;
}

static esp_gmf_err_io_t _bt_acquire_discard_packet(bt_io_stream_t *bt_io,
                                                   esp_gmf_payload_t *pload,
                                                   uint32_t wanted_size)
{
    uint32_t n = wanted_size ? wanted_size : 1;
    if (n > bt_io->packet.size || bt_io->packet.data == NULL) {
        uint8_t *p = (uint8_t *)esp_gmf_oal_realloc(bt_io->packet.data, n);
        if (p == NULL) {
            ESP_LOGE(TAG, "Error acquire discard write, realloc %lu failed", (unsigned long)n);
            _bt_clear_payload(pload);
            return ESP_GMF_IO_FAIL;
        }
        bt_io->packet.data = p;
        bt_io->packet.size = n;
        bt_io->packet.data_owner = bt_io;
    }
    pload->buf = bt_io->packet.data;
    pload->buf_length = bt_io->packet.size;
    pload->valid_size = 0;
    return ESP_GMF_IO_OK;
}

static void _bt_pace_discard(bt_io_stream_t *bt_io)
{
    if (bt_io->pace_us == 0) {
        return;
    }
    int64_t now = esp_timer_get_time();
    if (bt_io->pace_next_us == 0 || now - bt_io->pace_next_us > (int64_t)bt_io->pace_us * 4) {
        bt_io->pace_next_us = now;
    }
    bt_io->pace_next_us += bt_io->pace_us;

    int64_t wait_us = bt_io->pace_next_us - now;
    if (wait_us <= 0) {
        return;
    }
    TickType_t ticks = pdMS_TO_TICKS((wait_us + 999) / 1000);
    vTaskDelay(ticks ? ticks : 1);
}

static esp_gmf_err_t _bt_writer_io(esp_gmf_io_handle_t io, const char *action,
                                   bt_io_stream_t **out)
{
    *out = NULL;
    if (io == NULL || strcmp(OBJ_GET_TAG(io), "io_bt") != 0) {
        ESP_LOGE(TAG, "%s failed: not a Bluetooth writer I/O", action);
        return ESP_GMF_ERR_INVALID_ARG;
    }

    bt_io_cfg_t *cfg = (bt_io_cfg_t *)OBJ_GET_CFG(io);
    if (cfg == NULL) {
        ESP_LOGE(TAG, "%s failed: Bluetooth I/O configuration is missing", action);
        return ESP_GMF_ERR_INVALID_STATE;
    }
    if (cfg->dir != ESP_GMF_IO_DIR_WRITER) {
        ESP_LOGE(TAG, "%s failed: not a Bluetooth writer I/O", action);
        return ESP_GMF_ERR_INVALID_ARG;
    }
    *out = (bt_io_stream_t *)io;
    return ESP_GMF_ERR_OK;
}

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
    _bt_free_owned_packet_data(bt_io);
    esp_gmf_io_deinit(io);
    esp_gmf_oal_free(bt_io);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _bt_open(esp_gmf_io_handle_t io)
{
    ESP_LOGD(TAG, "Open, %s-%p", OBJ_GET_TAG(io), io);
    bt_io_stream_t *bt_io = (bt_io_stream_t *)io;
    bt_io_cfg_t *cfg = (bt_io_cfg_t *)OBJ_GET_CFG(io);
    bt_io->pace_next_us = 0;
    if (bt_io->discard) {
        return ESP_GMF_ERR_OK;
    }
    if (cfg->stream == NULL) {
        ESP_LOGE(TAG, "Error open Bluetooth I/O, stream = NULL");
        return ESP_GMF_ERR_FAIL;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _bt_close(esp_gmf_io_handle_t io)
{
    _bt_free_owned_packet_data((bt_io_stream_t *)io);
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
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    bt_io_stream_t *bt_io = (bt_io_stream_t *)handle;
    if (bt_io->discard) {
        return _bt_acquire_discard_packet(bt_io, pload, wanted_size);
    }
    if (_bt_owns_packet_data(bt_io)) {
        _bt_free_owned_packet_data(bt_io);
    }
    memset(&bt_io->packet, 0, sizeof(esp_bt_audio_stream_packet_t));
    esp_err_t ret = esp_bt_audio_stream_acquire_write(cfg->stream, &bt_io->packet, wanted_size);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Stream %p refused a write (ret=%d)", cfg->stream, ret);
        return _bt_acquire_discard_packet(bt_io, pload, wanted_size);
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
    if (_bt_owns_packet_data(bt_io) || bt_io->packet.data == NULL) {
        _bt_clear_payload(pload);
        _bt_pace_discard(bt_io);
        return ESP_GMF_IO_OK;
    }
    bt_io->packet.size = pload->valid_size;
    bt_io->packet.bad_frame = pload->meta_flag & ESP_GMF_META_FLAG_AUD_RECOVERY_PLC;
    bt_io->packet.is_done = pload->is_done;
    esp_err_t ret = esp_bt_audio_stream_release_write(cfg->stream, &bt_io->packet, pdTICKS_TO_MS(block_ticks));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Stream %p dropped a payload (ret=%d)", cfg->stream, ret);
    }
    _bt_clear_payload(pload);
    memset(&bt_io->packet, 0, sizeof(esp_bt_audio_stream_packet_t));
    return ESP_GMF_IO_OK;
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

esp_gmf_err_t esp_gmf_io_bt_set_discard(esp_gmf_io_handle_t io, bool enable)
{
    bt_io_stream_t *bt_io = NULL;
    esp_gmf_err_t ret = _bt_writer_io(io, "Set discard", &bt_io);
    if (ret != ESP_GMF_ERR_OK) {
        return ret;
    }
    bt_io_cfg_t *cfg = (bt_io_cfg_t *)OBJ_GET_CFG(io);
    if (!enable && cfg->stream == NULL) {
        ESP_LOGE(TAG, "Set discard failed: no bound stream");
        return ESP_GMF_ERR_INVALID_STATE;
    }

    bt_io->discard = enable;
    bt_io->pace_next_us = 0;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_io_bt_set_discard_pace(esp_gmf_io_handle_t io, uint32_t frame_us)
{
    bt_io_stream_t *bt_io = NULL;
    esp_gmf_err_t ret = _bt_writer_io(io, "Set discard pace", &bt_io);
    if (ret != ESP_GMF_ERR_OK) {
        return ret;
    }
    bt_io->pace_us = frame_us;
    bt_io->pace_next_us = 0;
    return ESP_GMF_ERR_OK;
}

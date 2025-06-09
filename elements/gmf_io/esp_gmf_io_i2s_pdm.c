/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>
#include <string.h>
#include "esp_gmf_io_i2s_pdm.h"
#include "esp_gmf_oal_mem.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define PDM_TX_DONE_BIT BIT(0)

/**
 * @brief I2S pdm io context in GMF
 */
typedef struct {
    esp_gmf_io_t       base;      /*!< The GMF i2s pdm io handle */
    bool               is_open;   /*!< The flag of whether opened */
    EventGroupHandle_t pdm_event; /*!< The event group handle of i2s pdm */
} i2s_pdm_io_stream_t;

static const char *TAG = "ESP_GMF_IIS_PDM";

bool _i2s_pdm_tx_done_callback(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx)
{
    EventGroupHandle_t evt = (EventGroupHandle_t)user_ctx;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t xResult = xEventGroupSetBitsFromISR(evt, PDM_TX_DONE_BIT, &xHigherPriorityTaskWoken);
    return xResult;
}

static esp_gmf_err_t _i2s_pdm_new(void *cfg, esp_gmf_obj_handle_t *io)
{
    return esp_gmf_io_i2s_pdm_init(cfg, io);
}

static esp_gmf_err_t _i2s_pdm_open(esp_gmf_io_handle_t io)
{
    i2s_pdm_io_stream_t *i2s_pdm_io = (i2s_pdm_io_stream_t *)io;
    i2s_pdm_io_cfg_t *cfg = (i2s_pdm_io_cfg_t *)OBJ_GET_CFG(i2s_pdm_io);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL;);
    if (cfg->pdm_chan == NULL) {
        ESP_LOGE(TAG, "There is no activated I2S driver handle");
        return ESP_GMF_ERR_FAIL;
    }
    if (cfg->dir == ESP_GMF_IO_DIR_WRITER) {
        i2s_pdm_io->pdm_event = xEventGroupCreate();
        ESP_GMF_CHECK(TAG, i2s_pdm_io->pdm_event, return ESP_GMF_ERR_MEMORY_LACK, "Failed to create i2s pdm event");
        i2s_event_callbacks_t cbs = {
            .on_recv = NULL,
            .on_recv_q_ovf = NULL,
            .on_sent = NULL,
            .on_send_q_ovf = _i2s_pdm_tx_done_callback,
        };
        i2s_channel_register_event_callback(cfg->pdm_chan, &cbs, i2s_pdm_io->pdm_event);
    }
    i2s_channel_enable(cfg->pdm_chan);
    i2s_pdm_io->is_open = true;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_io_t _i2s_pdm_acquire_read(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted_size, int block_ticks)
{
    i2s_pdm_io_stream_t *i2s_pdm_io = (i2s_pdm_io_stream_t *)handle;
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    size_t rlen = 0;
    i2s_pdm_io_cfg_t *cfg = (i2s_pdm_io_cfg_t *)OBJ_GET_CFG(i2s_pdm_io);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_IO_FAIL;);
    if (i2s_channel_read(cfg->pdm_chan, pload->buf, wanted_size, &rlen, ESP_GMF_MAX_DELAY) != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Read I2S data error, wanted: %ld", wanted_size);
        return ESP_GMF_IO_FAIL;
    }
    pload->valid_size = rlen;
    ESP_LOGD(TAG, "Read len: %d", rlen);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_io_t _i2s_pdm_release_read(esp_gmf_io_handle_t handle, void *payload, int block_ticks)
{
    i2s_pdm_io_stream_t *i2s_pdm_io = (i2s_pdm_io_stream_t *)handle;
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    esp_gmf_info_file_t info = {0};
    esp_gmf_io_get_info((esp_gmf_io_handle_t)i2s_pdm_io, &info);
    ESP_LOGD(TAG, "Update len = %d, pos = %d/%d", pload->valid_size, (int)info.pos, (int)info.size);
    esp_gmf_io_update_pos((esp_gmf_io_handle_t)handle, pload->valid_size);
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t _i2s_pdm_acquire_write(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted_size, int block_ticks)
{
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t _i2s_pdm_release_write(esp_gmf_io_handle_t handle, void *payload, int block_ticks)
{
    i2s_pdm_io_stream_t *i2s_pdm_io = (i2s_pdm_io_stream_t *)handle;
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    size_t wlen = 0;
    i2s_pdm_io_cfg_t *cfg = (i2s_pdm_io_cfg_t *)OBJ_GET_CFG(i2s_pdm_io);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_IO_FAIL;);
    if (i2s_channel_write(cfg->pdm_chan, pload->buf, pload->valid_size, &wlen, ESP_GMF_MAX_DELAY) != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "I2S write failed, valid: %d", pload->valid_size);
        return ESP_GMF_IO_FAIL;
    }
    esp_gmf_info_file_t info = {0};
    if (pload->is_done && i2s_pdm_io->pdm_event) {
        ESP_LOGE(TAG, "Clear the PDM_TX_DONE_BIT, len = %d", pload->valid_size);
        xEventGroupClearBits(i2s_pdm_io->pdm_event, PDM_TX_DONE_BIT);
    }
    if (wlen > 0) {
        esp_gmf_io_update_pos((esp_gmf_io_handle_t)handle, wlen);
    }
    esp_gmf_io_get_info((esp_gmf_io_handle_t)i2s_pdm_io, &info);
    ESP_LOGD(TAG, "Write len = %d, pos = %d/%d", pload->valid_size, (int)info.pos, (int)info.size);
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_t _i2s_pdm_seek(esp_gmf_io_handle_t io, uint64_t seek_byte_pos)
{
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _i2s_pdm_reset(esp_gmf_io_handle_t io)
{
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _i2s_pdm_close(esp_gmf_io_handle_t io)
{
    i2s_pdm_io_stream_t *i2s_pdm_io = (i2s_pdm_io_stream_t *)io;
    esp_gmf_info_file_t info = {0};
    i2s_pdm_io_cfg_t *cfg = (i2s_pdm_io_cfg_t *)OBJ_GET_CFG(i2s_pdm_io);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_IO_FAIL;);
    esp_gmf_io_get_info((esp_gmf_io_handle_t)i2s_pdm_io, &info);
    ESP_LOGI(TAG, "Try to close, %p, pos = %d/%d, pdm_event: %p", i2s_pdm_io, (int)info.pos, (int)info.size, i2s_pdm_io->pdm_event);
    if (i2s_pdm_io->pdm_event) {
        xEventGroupWaitBits(i2s_pdm_io->pdm_event, PDM_TX_DONE_BIT, pdTRUE, pdFALSE, ESP_GMF_MAX_DELAY);
    }
    ESP_LOGI(TAG, "CLose, %p, pos = %d/%d", i2s_pdm_io, (int)info.pos, (int)info.size);
    if (i2s_pdm_io->is_open) {
        i2s_pdm_io->is_open = false;
    }
    esp_gmf_io_set_pos((esp_gmf_io_handle_t)io, 0);
    i2s_channel_disable(cfg->pdm_chan);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _i2s_pdm_delete(esp_gmf_io_handle_t io)
{
    i2s_pdm_io_stream_t *i2s_pdm_io = (i2s_pdm_io_stream_t *)io;
    ESP_LOGD(TAG, "Delete, %s-%p", OBJ_GET_TAG(i2s_pdm_io), i2s_pdm_io);
    if (i2s_pdm_io->pdm_event) {
        vEventGroupDelete(i2s_pdm_io->pdm_event);
    }
    void *cfg = OBJ_GET_CFG(io);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_io_deinit(io);
    esp_gmf_oal_free(i2s_pdm_io);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_io_i2s_pdm_init(i2s_pdm_io_cfg_t *config, esp_gmf_io_handle_t *io)
{
    ESP_GMF_NULL_CHECK(TAG, config, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, io, {return ESP_GMF_ERR_INVALID_ARG;});
    *io = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    i2s_pdm_io_stream_t *i2s_pdm_io = esp_gmf_oal_calloc(1, sizeof(i2s_pdm_io_stream_t));
    ESP_GMF_MEM_VERIFY(TAG, i2s_pdm_io, {return ESP_GMF_ERR_MEMORY_LACK;},
                       "I2s pdm stream", sizeof(i2s_pdm_io_stream_t));
    i2s_pdm_io->base.dir = config->dir;
    i2s_pdm_io->base.type = ESP_GMF_IO_TYPE_BYTE;
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)i2s_pdm_io;
    obj->new_obj = _i2s_pdm_new;
    obj->del_obj = _i2s_pdm_delete;
    i2s_pdm_io_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto _i2s_pdm_fail;},
                       "I2s pdm stream configuration", sizeof(*config));
    memcpy(cfg, config, sizeof(*config));
    esp_gmf_obj_set_config(obj, cfg, sizeof(*config));
    ret = esp_gmf_obj_set_tag(obj, (config->name == NULL ? "io_i2s_pdm" : config->name));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto _i2s_pdm_fail, "Failed to set obj tag");
    i2s_pdm_io->base.close = _i2s_pdm_close;
    i2s_pdm_io->base.open = _i2s_pdm_open;
    i2s_pdm_io->base.seek = _i2s_pdm_seek;
    i2s_pdm_io->base.reset = _i2s_pdm_reset;
    esp_gmf_io_init(obj, NULL);
    if (cfg->dir == ESP_GMF_IO_DIR_WRITER) {
        i2s_pdm_io->base.acquire_write = _i2s_pdm_acquire_write;
        i2s_pdm_io->base.release_write = _i2s_pdm_release_write;
    } else if (cfg->dir == ESP_GMF_IO_DIR_READER) {
        i2s_pdm_io->base.acquire_read = _i2s_pdm_acquire_read;
        i2s_pdm_io->base.release_read = _i2s_pdm_release_read;
    } else {
        ESP_LOGW(TAG, "Does not set read or write function");
        ret = ESP_GMF_ERR_NOT_SUPPORT;
        goto _i2s_pdm_fail;
    }
    *io = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), i2s_pdm_io);
    return ESP_GMF_ERR_OK;
_i2s_pdm_fail:
    esp_gmf_obj_delete(obj);
    return ret;
}

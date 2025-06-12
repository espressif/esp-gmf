/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_gmf_io_embed_flash.h"
#include "esp_gmf_oal_mem.h"
#include "esp_log.h"

#define MAX_FILES_INDEX 1000

/**
 * @brief Embed flash io context in GMF
 */
typedef struct {
    esp_gmf_io_t       base;      /*!< The GMF embed flash io handle */
    int                cur;       /*!< The current stream pos */
    int                max_files; /*!< The max file number */
    embed_item_info_t *items;     /*!< The embed flash stream item */
} embed_flash_io_t;

static const char *const TAG = "ESP_GMF_EMBED_FLASH";

static esp_gmf_err_t _embed_flash_new(void *cfg, esp_gmf_obj_handle_t *io)
{
    return esp_gmf_io_embed_flash_init(cfg, io);
}

static esp_gmf_err_t _embed_flash_open(esp_gmf_io_handle_t io)
{
    embed_flash_io_t *embed_flash = (embed_flash_io_t *)io;
    if (embed_flash->items == NULL) {
        ESP_LOGE(TAG, "There is no embedded items, please call embed_flash_io_set_context first");
        return ESP_GMF_ERR_FAIL;
    }
    char *uri = NULL;
    esp_gmf_io_get_uri((esp_gmf_io_handle_t)embed_flash, &uri);
    if (uri == NULL) {
        ESP_LOGE(TAG, "The file URI is NULL!");
        return ESP_GMF_ERR_FAIL;
    }
    // The embedded file path like "embed://tone/0_alarm.mp3"
    char *temp = strchr(uri, '_');
    if (temp == NULL) {
        ESP_LOGE(TAG, "No _ in file name, %s", uri);
        return ESP_GMF_ERR_FAIL;
    }
    *temp = 0;
    temp = strrchr(uri, '/');
    if (temp == NULL) {
        ESP_LOGE(TAG, "The file name is incorrect!, %s", uri);
        return ESP_GMF_ERR_FAIL;
    }
    temp++;
    int file_index = strtoul(temp, 0, 10);
    ESP_LOGI(TAG, "The read item is %d, %s", file_index, uri);
    if (embed_flash->max_files < file_index) {
        ESP_LOGE(TAG, "The file index is out of range, %d", file_index);
        return ESP_GMF_ERR_FAIL;
    }

    embed_flash->cur = file_index;
    uint64_t total_bytes = embed_flash->items[embed_flash->cur].size;
    esp_gmf_io_set_size(io, total_bytes);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_io_t _embed_flash_acquire_read(esp_gmf_io_handle_t io, void *payload, uint32_t wanted_size, int block_ticks)
{
    embed_flash_io_t *embed_flash = (embed_flash_io_t *)io;
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    esp_gmf_info_file_t info = {0};
    esp_gmf_io_get_info((esp_gmf_io_handle_t)embed_flash, &info);
    if ((info.pos + wanted_size) > info.size) {
        wanted_size = info.size - info.pos;
    }
    ESP_LOGD(TAG, "Embed read data, ret:%ld, pos: %llu/%llu", wanted_size, info.pos, info.size);
    if (wanted_size == 0) {
        ESP_LOGW(TAG, "No more data, ret:%ld, pos: %llu/%llu", wanted_size, info.pos, info.size);
        pload->is_done = true;
    }
    memcpy(pload->buf, embed_flash->items[embed_flash->cur].address + info.pos, wanted_size);
    pload->valid_size = wanted_size;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t _embed_flash_release_read(esp_gmf_io_handle_t io, void *payload, int block_ticks)
{
    embed_flash_io_t *embed_flash = (embed_flash_io_t *)io;
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    esp_gmf_info_file_t info = {0};
    esp_gmf_io_get_info((esp_gmf_io_handle_t)embed_flash, &info);
    ESP_LOGD(TAG, "Update len = %d, pos = %d/%d", pload->valid_size, (int)info.pos, (int)info.size);
    esp_gmf_io_update_pos((esp_gmf_io_handle_t)embed_flash, pload->valid_size);
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_t _embed_flash_close(esp_gmf_io_handle_t io)
{
    esp_gmf_info_file_t info = {0};
    esp_gmf_io_get_info((esp_gmf_io_handle_t)io, &info);
    ESP_LOGI(TAG, "Closed, pos: %llu/%llu", info.pos, info.size);
    esp_gmf_io_set_pos(io, 0);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _embed_flash_destroy(esp_gmf_io_handle_t io)
{
    embed_flash_io_t *embed_flash = (embed_flash_io_t *)io;
    ESP_LOGD(TAG, "Delete, %s-%p", OBJ_GET_TAG(embed_flash), embed_flash);
    void *cfg = OBJ_GET_CFG(io);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    if (embed_flash->items) {
        esp_gmf_oal_free(embed_flash->items);
    }
    esp_gmf_io_deinit(io);
    esp_gmf_oal_free(embed_flash);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_io_embed_flash_init(embed_flash_io_cfg_t *config, esp_gmf_io_handle_t *io)
{
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_INVALID_ARG;);
    ESP_GMF_NULL_CHECK(TAG, io, return ESP_GMF_ERR_INVALID_ARG;);
    *io = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    embed_flash_io_t *embed_flash = esp_gmf_oal_calloc(1, sizeof(embed_flash_io_t));
    ESP_GMF_MEM_VERIFY(TAG, embed_flash, return ESP_GMF_ERR_MEMORY_LACK,
                       "embed flash stream", sizeof(embed_flash_io_t));
    embed_flash->base.dir = ESP_GMF_IO_DIR_READER;
    embed_flash->base.type = ESP_GMF_IO_TYPE_BYTE;
    embed_flash->max_files = config->max_files;
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)embed_flash;
    obj->new_obj = _embed_flash_new;
    obj->del_obj = _embed_flash_destroy;
    embed_flash_io_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto _embed_fail;},
                       "embed flash stream configuration", sizeof(*config));
    memcpy(cfg, config, sizeof(*config));
    esp_gmf_obj_set_config(obj, cfg, sizeof(*config));
    ret = esp_gmf_obj_set_tag(obj, (config->name == NULL ? "io_embed_flash" : config->name));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto _embed_fail, "Failed to set obj tag");
    embed_flash->base.open = _embed_flash_open;
    embed_flash->base.close = _embed_flash_close;
    embed_flash->base.seek = NULL;
    embed_flash->base.reset = NULL;
    esp_gmf_io_init(obj, NULL);
    if (embed_flash->base.dir == ESP_GMF_IO_DIR_READER) {
        embed_flash->base.acquire_read = _embed_flash_acquire_read;
        embed_flash->base.release_read = _embed_flash_release_read;
    } else {
        ESP_LOGE(TAG, "Does not support this operation, %d", embed_flash->base.dir);
        ret = ESP_GMF_ERR_NOT_SUPPORT;
        goto _embed_fail;
    }
    *io = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), embed_flash);
    return ESP_GMF_ERR_OK;
_embed_fail:
    esp_gmf_obj_delete(obj);
    return ret;
}

esp_gmf_err_t esp_gmf_io_embed_flash_set_context(esp_gmf_io_handle_t io, const embed_item_info_t *context, int max_num)
{
    ESP_GMF_NULL_CHECK(TAG, io, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, context, return ESP_GMF_ERR_INVALID_ARG);
    embed_flash_io_t *embed_flash = (embed_flash_io_t *)io;
    if (embed_flash->items) {
        esp_gmf_oal_free(embed_flash->items);
    }
    embed_flash->items = esp_gmf_oal_calloc(max_num, sizeof(embed_item_info_t));
    ESP_GMF_MEM_VERIFY(TAG, embed_flash->items, return ESP_GMF_ERR_MEMORY_LACK,
                       "embed item information", sizeof(embed_item_info_t));
    embed_flash->max_files = max_num;
    memcpy(embed_flash->items, context, sizeof(embed_item_info_t) * max_num);
    return ESP_GMF_ERR_OK;
}

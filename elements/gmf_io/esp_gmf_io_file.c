/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <sys/unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include "errno.h"

#include "esp_gmf_io_file.h"
#include "esp_gmf_oal_mem.h"
#include "fcntl.h"
#include "esp_log.h"

/**
 * @brief File io context in GMF
 */
typedef struct {
    esp_gmf_io_t base;    /*!< The GMF file io handle */
    bool         is_open; /*!< The flag of whether opened */
    int          file;    /*!< The handle of file stream */
} file_io_stream_t;

static const char *TAG = "ESP_GMF_FILE";

static char *get_mount_path(char *uri)
{
    /* support format: /sdcard, /spiffs, /storage etc ... */
    if (uri[0] == '/') {
        return uri;
    }
    /* support format: scheme://basepath... */
    char *skip_scheme = strstr(uri, "://");
    if (skip_scheme == NULL) {
        return NULL;
    }
    skip_scheme += 2;
    /* support format: scheme:///basepath... */
    if (skip_scheme[1] == '/') {
        skip_scheme++;
    }
    return skip_scheme;
}

static esp_gmf_err_t _file_new(void *cfg, esp_gmf_obj_handle_t *io)
{
    return esp_gmf_io_file_init(cfg, io);
}

static esp_gmf_err_t _file_open(esp_gmf_io_handle_t io)
{
    file_io_stream_t *file_io = (file_io_stream_t *)io;
    char *uri = NULL;
    esp_gmf_io_get_uri((esp_gmf_io_handle_t)file_io, &uri);
    if (uri == NULL) {
        ESP_LOGE(TAG, "Error, uri is not set, handle: %p", io);
        return ESP_GMF_ERR_FAIL;
    }
    ESP_LOGI(TAG, "Open, dir:%d, uri:%s", ((file_io_cfg_t *)file_io->base.parent.cfg)->dir, uri);
    char *path = get_mount_path(uri);
    if (path == NULL) {
        ESP_LOGE(TAG, "Invalid URI (%s).", uri);
        return ESP_GMF_ERR_FAIL;
    }
    if (file_io->is_open) {
        ESP_LOGE(TAG, "Already opened, p: %p, path: %s", file_io, path);
        return ESP_GMF_ERR_FAIL;
    }
    if (((file_io_cfg_t *)file_io->base.parent.cfg)->dir == ESP_GMF_IO_DIR_READER) {
        file_io->file = open(path, O_RDONLY);
        if (file_io->file <= 0) {
            ESP_LOGE(TAG, "Failed to open on read, path: %s, err: %s", path, strerror(errno));
            return ESP_GMF_ERR_FAIL;
        }
        struct stat sz = {0};
        stat(path, &sz);
        esp_gmf_io_set_size((esp_gmf_io_handle_t)file_io, sz.st_size);
        esp_gmf_info_file_t info = {0};
        esp_gmf_io_get_info((esp_gmf_io_handle_t)file_io, &info);
        ESP_LOGI(TAG, "File size: %d byte, file position: %lld", (int)sz.st_size, info.pos);
        if (info.pos > 0) {
            if (lseek(file_io->file, info.pos, SEEK_SET) < 0) {
                ESP_LOGE(TAG, "Seek to %lld failed, err: %s", info.pos, strerror(errno));
                return ESP_GMF_ERR_FAIL;
            }
        }
    } else if (((file_io_cfg_t *)file_io->base.parent.cfg)->dir == ESP_GMF_IO_DIR_WRITER) {
        file_io->file = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
        if (file_io->file == -1) {
            ESP_LOGE(TAG, "Failed to open on write, path: %s, err: %s", path, strerror(errno));
            return ESP_GMF_ERR_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "The type must be reader or writer");
        return ESP_GMF_ERR_FAIL;
    }
    file_io->is_open = true;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_io_t _file_acquire_read(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted_size, int block_ticks)
{
    file_io_stream_t *file_io = (file_io_stream_t *)handle;
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    int rlen = read(file_io->file, pload->buf, wanted_size);
    ESP_LOGD(TAG, "Read len: %d-%ld", rlen, wanted_size);
    if (rlen == -1) {
        ESP_LOGE(TAG, "The error is happened in reading data, error msg: %s", strerror(errno));
        return ESP_GMF_IO_FAIL;
    }
    pload->valid_size = rlen;
    if (rlen < wanted_size) {
        rlen = read(file_io->file, pload->buf + rlen, (wanted_size - rlen));
        if (rlen == 0) {
            pload->is_done = true;
            ESP_LOGI(TAG, "No more data, ret: %d", rlen);
            return ESP_GMF_IO_OK;
        }
        pload->valid_size += rlen;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t _file_release_read(esp_gmf_io_handle_t handle, void *payload, int block_ticks)
{
    file_io_stream_t *file_io = (file_io_stream_t *)handle;
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    esp_gmf_info_file_t info = {0};
    esp_gmf_io_get_info((esp_gmf_io_handle_t)file_io, &info);
    ESP_LOGD(TAG, "Update len = %d, pos = %d/%d", pload->valid_size, (int)info.pos, (int)info.size);
    esp_gmf_io_update_pos((esp_gmf_io_handle_t)handle, pload->valid_size);
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t _file_acquire_write(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted_size, int block_ticks)
{
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t _file_release_write(esp_gmf_io_handle_t handle, void *payload, int block_ticks)
{
    file_io_stream_t *file_io = (file_io_stream_t *)handle;
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    int wlen = 0;
    wlen = write(file_io->file, pload->buf, pload->valid_size);
    fsync(file_io->file);
    esp_gmf_info_file_t info = {0};
    esp_gmf_io_get_info((esp_gmf_io_handle_t)file_io, &info);
    ESP_LOGD(TAG, "Write len = %d, pos = %d/%d", pload->valid_size, (int)info.pos, (int)info.size);
    if (wlen > 0) {
        esp_gmf_io_update_pos((esp_gmf_io_handle_t)handle, wlen);
    }
    if (wlen == -1) {
        ESP_LOGE(TAG, "The error is happened in writing data, error msg:%s", strerror(errno));
        return ESP_GMF_IO_FAIL;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_t _file_seek(esp_gmf_io_handle_t io, uint64_t seek_byte_pos)
{
    file_io_stream_t *file_io = (file_io_stream_t *)io;
    esp_gmf_info_file_t info = {0};
    esp_gmf_io_get_info((esp_gmf_io_handle_t)file_io, &info);
    ESP_LOGI(TAG, "Seek position, total_bytes: %lld, seek: %lld",
             info.size, seek_byte_pos);
    if (seek_byte_pos > info.size) {
        ESP_LOGE(TAG, "Seek position is out of range, total_bytes: %lld, seek: %lld",
                 info.size, seek_byte_pos);
        return ESP_GMF_ERR_OUT_OF_RANGE;
    }
    if (lseek(file_io->file, seek_byte_pos, SEEK_SET) < 0) {
        ESP_LOGE(TAG, "Error seek file, error message: %s, line: %d", strerror(errno), __LINE__);
        return ESP_GMF_ERR_FAIL;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _file_reset(esp_gmf_io_handle_t io)
{
    file_io_stream_t *file_io = (file_io_stream_t *)io;
    if (file_io->file != 0) {
        lseek(file_io->file, 0, SEEK_SET);
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _file_close(esp_gmf_io_handle_t io)
{
    file_io_stream_t *file_io = (file_io_stream_t *)io;
    esp_gmf_info_file_t info = {0};
    esp_gmf_io_get_info((esp_gmf_io_handle_t)file_io, &info);
    ESP_LOGI(TAG, "CLose, %p, pos = %d/%d", file_io, (int)info.pos, (int)info.size);
    if (file_io->is_open) {
        close(file_io->file);
        file_io->is_open = false;
    }
    esp_gmf_io_set_pos((esp_gmf_io_handle_t)io, 0);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _file_delete(esp_gmf_io_handle_t io)
{
    file_io_stream_t *file_io = (file_io_stream_t *)io;
    ESP_LOGD(TAG, "Delete, %s-%p", OBJ_GET_TAG(file_io), file_io);
    void *cfg = OBJ_GET_CFG(io);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_io_deinit(io);
    esp_gmf_oal_free(file_io);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_io_file_init(file_io_cfg_t *config, esp_gmf_io_handle_t *io)
{
    ESP_GMF_NULL_CHECK(TAG, config, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, io, {return ESP_GMF_ERR_INVALID_ARG;});
    *io = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    file_io_stream_t *file_io = esp_gmf_oal_calloc(1, sizeof(file_io_stream_t));
    ESP_GMF_MEM_VERIFY(TAG, file_io, return ESP_GMF_ERR_MEMORY_LACK,
                       "file stream", sizeof(file_io_stream_t));
    file_io->base.dir = config->dir;
    file_io->base.type = ESP_GMF_IO_TYPE_BYTE;
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)file_io;
    obj->new_obj = _file_new;
    obj->del_obj = _file_delete;
    file_io_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto _file_fail;},
                       "file stream configuration", sizeof(*config));
    memcpy(cfg, config, sizeof(*config));
    esp_gmf_obj_set_config(obj, cfg, sizeof(*config));
    ret = esp_gmf_obj_set_tag(obj, (config->name == NULL ? "io_file" : config->name));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto _file_fail, "Failed to set obj tag");
    file_io->base.close = _file_close;
    file_io->base.open = _file_open;
    file_io->base.seek = _file_seek;
    file_io->base.reset = _file_reset;
    file_io_cfg_t *fat_cfg = (file_io_cfg_t *)config;
    esp_gmf_io_init(obj, NULL);
    if (fat_cfg->dir == ESP_GMF_IO_DIR_WRITER) {
        file_io->base.acquire_write = _file_acquire_write;
        file_io->base.release_write = _file_release_write;
    } else if (fat_cfg->dir == ESP_GMF_IO_DIR_READER) {
        file_io->base.acquire_read = _file_acquire_read;
        file_io->base.release_read = _file_release_read;
    } else {
        ESP_LOGW(TAG, "Does not set read or write function");
        ret = ESP_GMF_ERR_NOT_SUPPORT;
        goto _file_fail;
    }
    *io = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), file_io);
    return ESP_GMF_ERR_OK;
_file_fail:
    esp_gmf_obj_delete(obj);
    return ret;
}

/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sys/unistd.h>
#include "freertos/FreeRTOS.h"
#include <sys/stat.h>
#include "esp_gmf_oal_mem.h"
#include "gmf_fake_io.h"
#include "esp_log.h"

static const char *TAG = "FAKE_IO";

static esp_gmf_err_t _file_open(esp_gmf_io_handle_t io)
{
    fake_io_t *file_io = (fake_io_t *)io;
    ESP_LOGI(TAG, "%s, %s-%p", __func__, OBJ_GET_TAG(file_io), file_io);
    return file_io->open_return;
}

esp_gmf_err_io_t _file_acquire_read(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted_size, int block_ticks)
{
    fake_io_t *file_io = (fake_io_t *)handle;
    ESP_LOGD(TAG, "%s, %s-%p", __func__, OBJ_GET_TAG(file_io), file_io);
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    pload->valid_size = wanted_size;
    vTaskDelay(3 / portTICK_PERIOD_MS);
    return file_io->acquire_read_return;
}

esp_gmf_err_io_t _file_release_read(esp_gmf_io_handle_t handle, void *payload, int block_ticks)
{
    fake_io_t *file_io = (fake_io_t *)handle;
    ESP_LOGD(TAG, "%s, %s-%p", __func__, OBJ_GET_TAG(file_io), file_io);
    vTaskDelay(2 / portTICK_PERIOD_MS);
    return file_io->release_read_return;
}

esp_gmf_err_io_t _file_acquire_write(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted_size, int block_ticks)
{
    fake_io_t *file_io = (fake_io_t *)handle;
    ESP_LOGD(TAG, "%s, %s-%p", __func__, OBJ_GET_TAG(file_io), file_io);
    vTaskDelay(2 / portTICK_PERIOD_MS);
    return file_io->acquire_write_return;
}

esp_gmf_err_io_t _file_release_write(esp_gmf_io_handle_t handle, void *payload, int block_ticks)
{
    fake_io_t *file_io = (fake_io_t *)handle;
    ESP_LOGD(TAG, "%s, %s-%p", __func__, OBJ_GET_TAG(file_io), file_io);
    vTaskDelay(2 / portTICK_PERIOD_MS);
    return file_io->release_write_return;
}

esp_gmf_err_t _file_seek(esp_gmf_io_handle_t io, uint64_t seek_byte_pos)
{
    fake_io_t *file_io = (fake_io_t *)io;
    ESP_LOGI(TAG, "%s, seek to %llu, %s-%p", __func__, seek_byte_pos, OBJ_GET_TAG(file_io), file_io);
    file_io->seek_called_count++;
    return file_io->seek_return;
}

static esp_gmf_err_t _file_close(esp_gmf_io_handle_t io)
{
    fake_io_t *file_io = (fake_io_t *)io;
    ESP_LOGI(TAG, "%s, %s-%p", __func__, OBJ_GET_TAG(file_io), file_io);
    return file_io->close_return;
}

static esp_gmf_err_t _file_delete(esp_gmf_io_handle_t io)
{
    fake_io_t *file_io = (fake_io_t *)io;
    ESP_LOGI(TAG, "%s, %s-%p", __func__, OBJ_GET_TAG(file_io), file_io);
    void *cfg = OBJ_GET_CFG(io);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_io_deinit(io);
    esp_gmf_oal_free(file_io);
    return file_io->delete_return;
}

static esp_gmf_err_t fake_io_new(void *cfg, esp_gmf_obj_handle_t *io)
{
    ESP_GMF_MEM_CHECK(TAG, cfg, return ESP_ERR_INVALID_ARG);
    ESP_GMF_MEM_CHECK(TAG, io, return ESP_ERR_INVALID_ARG);
    esp_gmf_obj_handle_t new_io = NULL;
    fake_io_cfg_t *config = (fake_io_cfg_t *)cfg;
    int ret = fake_io_init(config, &new_io);
    if (ret != ESP_GMF_ERR_OK) {
        return ret;
    }
    *io = new_io;
    ESP_LOGI(TAG, "New an object,%s-%p", OBJ_GET_TAG(new_io), new_io);
    fake_io_t *file_io = (fake_io_t *)new_io;
    return file_io->new_return;
}

esp_gmf_err_t fake_io_init(fake_io_cfg_t *config, esp_gmf_io_handle_t *io)
{
    ESP_GMF_MEM_CHECK(TAG, config, return ESP_ERR_INVALID_ARG);
    ESP_GMF_MEM_CHECK(TAG, io, return ESP_ERR_INVALID_ARG);
    fake_io_t *file_io = esp_gmf_oal_calloc(1, sizeof(fake_io_t));
    ESP_GMF_MEM_CHECK(TAG, file_io, return ESP_ERR_NO_MEM);
    file_io->base.dir = config->dir;
    file_io->base.type = ESP_GMF_IO_TYPE_BYTE;

    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)file_io;
    fake_io_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_CHECK(TAG, cfg, {esp_gmf_oal_free(file_io); return ESP_GMF_ERR_MEMORY_LACK;});
    memcpy(cfg, config, sizeof(*config));
    esp_gmf_obj_set_config(obj, cfg, sizeof(*cfg));

    int ret = esp_gmf_obj_set_tag(obj, (config->name == NULL ? "file" : config->name));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto _file_fail, "Failed set OBJ tag");
    obj->new_obj = fake_io_new;
    obj->del_obj = _file_delete;

    file_io->base.close = _file_close;
    file_io->base.open = _file_open;
    file_io->base.seek = _file_seek;
    fake_io_cfg_t *fat_cfg = (fake_io_cfg_t *)config;

    file_io->open_return = ESP_GMF_ERR_OK;
    file_io->acquire_read_return = ESP_GMF_IO_OK;
    file_io->release_read_return = ESP_GMF_IO_OK;
    file_io->acquire_write_return = ESP_GMF_IO_OK;
    file_io->release_write_return = ESP_GMF_IO_OK;
    file_io->seek_return = ESP_GMF_ERR_OK;
    file_io->close_return = ESP_GMF_ERR_OK;
    file_io->delete_return = ESP_GMF_ERR_OK;
    file_io->new_return = ESP_GMF_ERR_OK;

    if (config->init_return <= 0) {
        config->init_return = ESP_GMF_ERR_OK;
    }

    esp_gmf_io_init(obj, NULL);
    if (fat_cfg->dir == ESP_GMF_IO_DIR_WRITER) {
        file_io->base.acquire_write = _file_acquire_write;
        file_io->base.release_write = _file_release_write;
    } else if (fat_cfg->dir == ESP_GMF_IO_DIR_READER) {
        file_io->base.acquire_read = _file_acquire_read;
        file_io->base.release_read = _file_release_read;
    } else {
        ESP_LOGW(TAG, "Does not set read or write function,%x", fat_cfg->dir);
        ret = ESP_GMF_ERR_NOT_SUPPORT;
        goto _file_fail;
    }

    *io = obj;
    ESP_LOGI(TAG, "Init Fake IO,%s-%p", OBJ_GET_TAG(obj), file_io);
    return config->init_return;

_file_fail:
    _file_delete(obj);
    return ret;
}

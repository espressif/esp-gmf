/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2024 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <string.h>
#include "esp_gmf_pic_element.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_mutex.h"
#include "esp_log.h"
#include "esp_gmf_info_file.h"

static const char *TAG = "ESP_GMF_PIC_EL";

esp_gmf_err_t esp_gmf_pic_el_init(esp_gmf_pic_element_handle_t handle, esp_gmf_element_cfg_t *config)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_pic_element_t *pic = (esp_gmf_pic_element_t *)handle;
    config->ctx = (void *)pic;
    esp_gmf_element_init(&pic->base, config);
    esp_gmf_info_file_init(&pic->file_info);
    pic->lock = esp_gmf_oal_mutex_create();
    ESP_GMF_MEM_CHECK(TAG, pic->lock, {
        esp_gmf_element_deinit(&pic->base);
        esp_gmf_oal_free(pic);
        return ESP_GMF_ERR_MEMORY_LACK;
    });
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_pic_el_get_metadata(esp_gmf_pic_element_handle_t handle, esp_gmf_info_metadata_t *meta)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, meta, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_pic_element_t *pic = (esp_gmf_pic_element_t *)handle;
    esp_gmf_oal_mutex_lock(pic->lock);
    memcpy(meta, &pic->meta_info, sizeof(pic->meta_info));
    esp_gmf_oal_mutex_unlock(pic->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_pic_el_set_metadata(esp_gmf_pic_element_handle_t handle, void *value, int len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, value, return ESP_GMF_ERR_INVALID_ARG);

    esp_gmf_pic_element_t *pic = (esp_gmf_pic_element_t *)handle;
    esp_gmf_oal_mutex_lock(pic->lock);
    pic->meta_info.content = (void *)esp_gmf_oal_calloc(1, len);
    pic->meta_info.length = 0;
    ESP_GMF_MEM_CHECK(TAG, pic->meta_info.content, esp_gmf_oal_mutex_unlock(pic->lock); return ESP_GMF_ERR_MEMORY_LACK);
    pic->meta_info.length = len;
    memcpy(pic->meta_info.content, value, len);
    esp_gmf_oal_mutex_unlock(pic->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_pic_el_get_file_info(esp_gmf_pic_element_handle_t handle, esp_gmf_info_file_t *info)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, info, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_pic_element_t *pic = (esp_gmf_pic_element_t *)handle;
    esp_gmf_oal_mutex_lock(pic->lock);
    memcpy(info, &pic->file_info, sizeof(pic->file_info));
    esp_gmf_oal_mutex_unlock(pic->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_pic_el_set_file_info(esp_gmf_pic_element_handle_t handle, esp_gmf_info_file_t *info)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, info, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_pic_element_t *pic = (esp_gmf_pic_element_t *)handle;
    esp_gmf_oal_mutex_lock(pic->lock);
    memcpy(&pic->file_info, info, sizeof(pic->file_info));
    esp_gmf_oal_mutex_unlock(pic->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_pic_el_get_pic_info(esp_gmf_pic_element_handle_t handle, esp_gmf_info_pic_t *info)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, info, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_pic_element_t *pic = (esp_gmf_pic_element_t *)handle;
    esp_gmf_oal_mutex_lock(pic->lock);
    memcpy(info, &pic->pic_info, sizeof(pic->pic_info));
    esp_gmf_oal_mutex_unlock(pic->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_pic_el_set_pic_info(esp_gmf_pic_element_handle_t handle, esp_gmf_info_pic_t *info)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, info, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_pic_element_t *pic = (esp_gmf_pic_element_t *)handle;
    esp_gmf_oal_mutex_lock(pic->lock);
    memcpy(&pic->pic_info, info, sizeof(pic->pic_info));
    esp_gmf_oal_mutex_unlock(pic->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_pic_el_set_file_size(esp_gmf_pic_element_handle_t handle, uint64_t size)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_pic_element_t *pic = (esp_gmf_pic_element_t *)handle;
    esp_gmf_oal_mutex_lock(pic->lock);
    esp_gmf_info_file_set_size(&pic->file_info, size);
    esp_gmf_oal_mutex_unlock(pic->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_pic_el_update_file_pos(esp_gmf_pic_element_handle_t handle, uint64_t pos)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_pic_element_t *pic = (esp_gmf_pic_element_t *)handle;
    esp_gmf_oal_mutex_lock(pic->lock);
    esp_gmf_info_file_update_pos(&pic->file_info, pos);
    esp_gmf_oal_mutex_unlock(pic->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_pic_el_deinit(esp_gmf_pic_element_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_pic_element_t *pic = (esp_gmf_pic_element_t *)handle;
    esp_gmf_oal_mutex_lock(pic->lock);

    if (pic->meta_info.content) {
        esp_gmf_oal_free(pic->meta_info.content);
        pic->meta_info.length = 0;
    }
    esp_gmf_element_deinit(&pic->base);
    esp_gmf_info_file_deinit(&pic->file_info);
    esp_gmf_oal_mutex_unlock(pic->lock);
    esp_gmf_oal_mutex_destroy(pic->lock);
    return ESP_GMF_ERR_OK;
}
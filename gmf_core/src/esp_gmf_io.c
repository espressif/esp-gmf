/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include <string.h>
#include "esp_gmf_io.h"
#include "esp_log.h"
#include "esp_gmf_info_file.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_sys.h"
#include "esp_gmf_job.h"
#include "esp_gmf_new_databus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "ESP_GMF_IO";

#define IO_EVT_TASK_BLOCK_RUN_BIT  BIT(0)
#define IO_EVT_TASK_HOLD_DONE_BIT  BIT(1)
#define IO_EVT_TASK_SEEK_DONE_BIT  BIT(2)

#define ESP_GMF_IO_SEEK_POS_INVALID  UINT64_MAX

static esp_gmf_err_t esp_gmf_io_update_speed_stats(esp_gmf_io_handle_t handle, uint32_t bytes, uint64_t time_ms)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    esp_gmf_io_speed_stats_t *stats = io->speed_stats;
    if (stats == NULL) {
        return ESP_GMF_ERR_OK;
    }
    stats->total_bytes += bytes;
    stats->total_time_ms += time_ms;
    /* Calculate average speed */
    if (stats->total_time_ms > 0) {
        stats->average_speed_kbps = stats->total_bytes * 8 / stats->total_time_ms;
    } else {
        stats->average_speed_kbps = 0;
    }
    /* Calculate current speed (100 milliseconds interval) */
    uint64_t interval_time_ms = stats->total_time_ms - stats->last_total_time_ms;
    if (interval_time_ms >= 100) {
        uint64_t interval_bytes = stats->total_bytes - stats->last_total_bytes;
        uint32_t current_speed_kbps = (uint32_t)(interval_bytes * 8 / interval_time_ms);
        stats->last_total_bytes = stats->total_bytes;
        stats->last_total_time_ms = stats->total_time_ms;
        if (stats->current_speed_kbps == 0) {
            stats->current_speed_kbps = current_speed_kbps;
        } else {
            /* smoothing io speed with coefficient 0.9 using integer arithmetic
               new_speed = (1 * old_speed + 9 * current_speed) / 10 */
            stats->current_speed_kbps = (uint32_t)((1 * (uint64_t)stats->current_speed_kbps + 9 * (uint64_t)current_speed_kbps) / 10);
        }
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t io_process_read(esp_gmf_io_handle_t handle)
{
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    esp_gmf_data_bus_block_t blk = {0};
    esp_gmf_job_err_t job_err = ESP_GMF_JOB_ERR_OK;
    esp_gmf_err_io_t db_ret = esp_gmf_db_acquire_write(io->data_bus, &blk, io->io_cfg.buffer_cfg.io_size, portMAX_DELAY);
    if (db_ret != ESP_GMF_IO_OK) {
        if (db_ret == ESP_GMF_IO_ABORT) {
            return ESP_GMF_JOB_ERR_ABORT;
        }
        return ESP_GMF_JOB_ERR_FAIL;
    }
    if (io->acquire_read && io->release_read) {
        esp_gmf_payload_t payload = {
            .buf = blk.buf,
            .buf_length = blk.buf_length,
            .valid_size = 0,
            .is_done = false,
        };
        uint64_t start_time_ms = esp_gmf_oal_sys_get_time_ms();
        esp_gmf_err_io_t io_ret = io->acquire_read(handle, &payload, payload.buf_length, portMAX_DELAY);
        if (io_ret == ESP_GMF_IO_OK) {
            uint64_t interval_time_ms = esp_gmf_oal_sys_get_time_ms() - start_time_ms;
            esp_gmf_io_update_speed_stats(io, payload.valid_size, interval_time_ms);
            blk.valid_size = payload.valid_size;
            blk.is_last = payload.is_done;
            if (payload.is_done) {
                job_err = ESP_GMF_JOB_ERR_DONE;
            }
        } else {
            esp_gmf_db_abort(io->data_bus);
            job_err = ESP_GMF_JOB_ERR_FAIL;
        }
        io->release_read(handle, &payload, portMAX_DELAY);
    } else {
        job_err = ESP_GMF_JOB_ERR_FAIL;
    }
    esp_gmf_db_release_write(io->data_bus, &blk, portMAX_DELAY);
    return job_err;
}

static esp_gmf_job_err_t io_process_write(esp_gmf_io_handle_t handle)
{
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    esp_gmf_data_bus_block_t blk = {0};
    esp_gmf_job_err_t job_err = ESP_GMF_JOB_ERR_OK;
    esp_gmf_err_io_t db_ret = esp_gmf_db_acquire_read(io->data_bus, &blk, io->io_cfg.buffer_cfg.io_size, portMAX_DELAY);
    if (db_ret != ESP_GMF_IO_OK) {
        if (db_ret == ESP_GMF_IO_ABORT) {
            return ESP_GMF_JOB_ERR_ABORT;
        }
        return ESP_GMF_JOB_ERR_FAIL;
    }
    if (blk.valid_size > 0) {
        if (io->acquire_write && io->release_write) {
            esp_gmf_payload_t payload = {
                .buf = blk.buf,
                .buf_length = blk.buf_length,
                .valid_size = blk.valid_size,
                .is_done = blk.is_last,
            };
            io->acquire_write(handle, &payload, payload.valid_size, portMAX_DELAY);
            uint64_t start_time_ms = esp_gmf_oal_sys_get_time_ms();
            esp_gmf_err_io_t io_ret = io->release_write(handle, &payload, portMAX_DELAY);
            if (io_ret == ESP_GMF_IO_OK) {
                uint64_t interval_ms = esp_gmf_oal_sys_get_time_ms() - start_time_ms;
                esp_gmf_io_update_speed_stats(io, payload.valid_size, interval_ms);
            } else {
                job_err = ESP_GMF_JOB_ERR_FAIL;
            }
        } else {
            job_err = ESP_GMF_JOB_ERR_FAIL;
        }
        if (blk.is_last) {
            job_err = ESP_GMF_JOB_ERR_DONE;
        }
    } else if (blk.valid_size == 0 && blk.is_last) {
        job_err = ESP_GMF_JOB_ERR_DONE;
    } else {
        job_err = ESP_GMF_JOB_ERR_FAIL;
    }
    esp_gmf_db_release_read(io->data_bus, &blk, portMAX_DELAY);
    return job_err;
}

static esp_gmf_err_t seek_in_cache(esp_gmf_io_t *io, uint64_t seek_pos)
{
    esp_gmf_info_file_t info = {0};
    esp_gmf_io_get_info((esp_gmf_io_handle_t)io, &info);
    if (seek_pos >= info.pos) {
        uint64_t drop_bytes = seek_pos - info.pos;
        uint32_t filled_size = 0;
        esp_gmf_db_get_filled_size(io->data_bus, &filled_size);
        if ((uint64_t)filled_size >= drop_bytes) {
            if (drop_bytes > 0) {
                esp_gmf_data_bus_block_t blk = {0};
                if (esp_gmf_db_acquire_read(io->data_bus, &blk, drop_bytes, portMAX_DELAY) != ESP_GMF_IO_OK) {
                    return ESP_GMF_ERR_FAIL;
                }
                esp_gmf_db_release_read(io->data_bus, &blk, portMAX_DELAY);
            }
            esp_gmf_io_set_pos(io, seek_pos);
            ESP_LOGI(TAG, "Seek within buffer, drop %llu bytes, seek to %llu, [%p-%s]", drop_bytes, seek_pos, io, OBJ_GET_TAG(io));
            return ESP_GMF_ERR_OK;
        }
    }
    return ESP_GMF_ERR_FAIL;
}

static esp_gmf_job_err_t esp_gmf_io_process(esp_gmf_io_handle_t handle, void *params)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_JOB_ERR_FAIL);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    if (io->seek_pos != ESP_GMF_IO_SEEK_POS_INVALID) {
        esp_gmf_db_reset(io->data_bus);
        esp_gmf_err_t ret = io->seek(handle, io->seek_pos);
        io->seek_pos = ESP_GMF_IO_SEEK_POS_INVALID;
        xEventGroupSetBits((EventGroupHandle_t)io->evt_group, IO_EVT_TASK_SEEK_DONE_BIT);
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "IO seek failed, [%p-%s]", io, OBJ_GET_TAG(io));
            return ESP_GMF_JOB_ERR_FAIL;
        }
    }
    if (io->_is_hold) {
        ESP_LOGD(TAG, "IO process task holding, [%p-%s]", io, OBJ_GET_TAG(io));
        xEventGroupWaitBits((EventGroupHandle_t)io->evt_group, IO_EVT_TASK_HOLD_DONE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        ESP_LOGD(TAG, "IO process task resumed, [%p-%s]", io, OBJ_GET_TAG(io));
    }
    esp_gmf_job_err_t ret = ESP_GMF_JOB_ERR_OK;
    if (io->dir == ESP_GMF_IO_DIR_READER) {
        ret = io_process_read(handle);
    } else if (io->dir == ESP_GMF_IO_DIR_WRITER) {
        ret = io_process_write(handle);
    }
    if (ret == ESP_GMF_JOB_ERR_ABORT && (io->_is_hold || io->seek_pos != ESP_GMF_IO_SEEK_POS_INVALID)) {
        return ESP_GMF_JOB_ERR_OK;
    }
    return ret;
}

static esp_gmf_err_t io_register_task(esp_gmf_io_handle_t handle)
{
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    char name[ESP_GMF_JOB_LABLE_MAX_LEN] = "";
    esp_gmf_job_str_cat(name, ESP_GMF_JOB_LABLE_MAX_LEN, OBJ_GET_TAG(io), ESP_GMF_JOB_STR_PROCESS, strlen(ESP_GMF_JOB_STR_PROCESS));
    esp_gmf_task_register_ready_job(io->task_hd, name, esp_gmf_io_process, ESP_GMF_JOB_TIMES_INFINITE, handle, true);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t esp_gmf_io_task_evt(esp_gmf_event_pkt_t *pkt, void *ctx)
{
    if (pkt->type == ESP_GMF_EVT_TYPE_CHANGE_STATE) {
        esp_gmf_event_state_t st = (esp_gmf_event_state_t)pkt->sub;
        if (st == ESP_GMF_EVENT_STATE_FINISHED || st == ESP_GMF_EVENT_STATE_STOPPED || st == ESP_GMF_EVENT_STATE_ERROR) {
            if (ctx) {
                xEventGroupSetBits((EventGroupHandle_t)ctx, IO_EVT_TASK_BLOCK_RUN_BIT);
            }
        }
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_io_init(esp_gmf_io_handle_t handle, esp_gmf_io_cfg_t *io_cfg)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    if (io_cfg) {
        memcpy(&io->io_cfg, io_cfg, sizeof(esp_gmf_io_cfg_t));
    } else {
        memset(&io->io_cfg, 0, sizeof(esp_gmf_io_cfg_t));
    }
    io->_is_done = 0;
    io->_is_abort = 0;
    io->_is_hold = 0;
    io->task_timeout_ms = 1000;
    io->evt_group = NULL;
    io->seek_pos = ESP_GMF_IO_SEEK_POS_INVALID;
    return esp_gmf_info_file_init(&io->attr);
}

esp_gmf_err_t esp_gmf_io_deinit(esp_gmf_io_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    if (io->task_hd) {
        esp_gmf_task_deinit(io->task_hd);
        io->task_hd = NULL;
        vEventGroupDelete((EventGroupHandle_t)io->evt_group);
        io->evt_group = NULL;
    }
    if (io->data_bus) {
        esp_gmf_db_deinit(io->data_bus);
        io->data_bus = NULL;
    }
    if (io->speed_stats) {
        esp_gmf_oal_free(io->speed_stats);
        io->speed_stats = NULL;
    }
    return esp_gmf_info_file_deinit(&io->attr);
}

esp_gmf_err_t esp_gmf_io_enable_speed_monitor(esp_gmf_io_handle_t handle, bool enabled)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    if (enabled) {
        if (io->speed_stats == NULL) {
            io->speed_stats = esp_gmf_oal_calloc(1, sizeof(esp_gmf_io_speed_stats_t));
            ESP_GMF_NULL_CHECK(TAG, io->speed_stats, return ESP_GMF_ERR_MEMORY_LACK);
        } else {
            memset(io->speed_stats, 0, sizeof(esp_gmf_io_speed_stats_t));
        }
    } else {
        if (io->speed_stats) {
            esp_gmf_oal_free(io->speed_stats);
            io->speed_stats = NULL;
        }
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_io_get_speed_stats(esp_gmf_io_handle_t handle, esp_gmf_io_speed_stats_t *stats)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, stats, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    if (io->speed_stats) {
        memcpy(stats, io->speed_stats, sizeof(esp_gmf_io_speed_stats_t));
    } else {
        memset(stats, 0, sizeof(esp_gmf_io_speed_stats_t));
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_io_get_score(esp_gmf_io_handle_t handle, const char *url, int *score)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, url, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, score, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    *score = ESP_GMF_IO_SCORE_NONE;
    if (io->get_score) {
        return io->get_score(io, url, score);
    }
    return ESP_GMF_ERR_NOT_SUPPORT;
}

esp_gmf_err_t esp_gmf_io_open(esp_gmf_io_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    if (io->open == NULL) {
        return ESP_GMF_ERR_NOT_READY;
    }
    int ret = ESP_GMF_ERR_OK;
    esp_gmf_io_cfg_t *io_cfg = &io->io_cfg;
    if (io->task_hd == NULL && io_cfg && io_cfg->thread.stack > 0) {
        if (io->data_bus == NULL && io_cfg->buffer_cfg.buffer_size > 0) {
            ret = esp_gmf_db_new_block(1, io_cfg->buffer_cfg.buffer_size, &io->data_bus);
            if (ret != ESP_GMF_ERR_OK) {
                ESP_LOGE(TAG, "Failed to create data bus, size:%d", io_cfg->buffer_cfg.buffer_size);
                return ret;
            }
            esp_gmf_data_bus_type_t db_type = 0;
            esp_gmf_db_get_type(io->data_bus, &db_type);
            io->type = db_type;
            ESP_LOGD(TAG, "Created data bus: %p, type: %d, size: %d", io->data_bus, io->type, io_cfg->buffer_cfg.buffer_size);
        } else {
            ESP_LOGE(TAG, "Failed to create data bus");
            return ESP_GMF_ERR_FAIL;
        }
        esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
        cfg.thread.stack = io_cfg->thread.stack;
        cfg.thread.prio = io_cfg->thread.prio;
        cfg.thread.core = io_cfg->thread.core;
        cfg.thread.stack_in_ext = io_cfg->thread.stack_in_ext;
        cfg.name = OBJ_GET_TAG(io);
        cfg.ctx = handle;
        cfg.cb = NULL;
        if (esp_gmf_task_init(&cfg, &io->task_hd) != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to create new IO task, [%p-%s]", io, OBJ_GET_TAG(io));
            return ESP_GMF_ERR_FAIL;
        }
        if (io->evt_group == NULL) {
            io->evt_group = xEventGroupCreate();
            ESP_GMF_NULL_CHECK(TAG, io->evt_group, return ESP_GMF_ERR_FAIL);
        }
        esp_gmf_task_set_event_func(io->task_hd, esp_gmf_io_task_evt, io->evt_group);
        io_register_task(handle);
        ESP_LOGD(TAG, "Initialize a GMF IO[%p-%s], stack:%d, thread:%p-%s", handle, OBJ_GET_TAG(io),
                 io_cfg == NULL ? -1 : io_cfg->thread.stack, io->task_hd, OBJ_GET_TAG(io->task_hd));
    }
    ret = io->open(io);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "esp_gmf_io_open failed");
    if (io->task_hd) {
        ret = esp_gmf_task_run(io->task_hd);
    }
    esp_gmf_io_enable_speed_monitor(io, io_cfg->enable_speed_monitor);
    return ret;
}

esp_gmf_err_t esp_gmf_io_seek(esp_gmf_io_handle_t handle, uint64_t seek_byte_pos)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    esp_gmf_info_file_t info = {0};
    esp_gmf_io_get_info(handle, &info);
    if (io->seek == NULL) {
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    if (info.size > 0 && seek_byte_pos >= info.size) {
        ESP_LOGE(TAG, "The seek position is out of range, pos %llu >= %llu, io: %p-%s", seek_byte_pos, info.size, io, OBJ_GET_TAG(io));
        return ESP_GMF_ERR_OUT_OF_RANGE;
    }
    int ret = ESP_GMF_ERR_OK;
    if (io->data_bus && io->task_hd) {
        if (seek_in_cache(io, seek_byte_pos) == ESP_GMF_ERR_OK) {
            return ESP_GMF_ERR_OK;
        }
        io->seek_pos = seek_byte_pos;
        /* Set position as subclasses might rely on the updated position to perform the seek (e.g., HTTP range header) */
        esp_gmf_io_set_pos(io, seek_byte_pos);
        esp_gmf_db_abort(io->data_bus);
        esp_gmf_event_state_t st;
        esp_gmf_task_get_state(io->task_hd, &st);
        if (st == ESP_GMF_EVENT_STATE_FINISHED || st == ESP_GMF_EVENT_STATE_STOPPED || st == ESP_GMF_EVENT_STATE_ERROR) {
            ESP_LOGI(TAG, "Task is %s, restarting for seek...", esp_gmf_event_get_state_str(st));
            esp_gmf_task_reset(io->task_hd);
            io_register_task(handle);
            esp_gmf_task_run(io->task_hd);
        }
        ESP_LOGD(TAG, "Async seek requested to %llu, [%p-%s]", seek_byte_pos, io, OBJ_GET_TAG(io));
        xEventGroupWaitBits((EventGroupHandle_t)io->evt_group, IO_EVT_TASK_SEEK_DONE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        ESP_LOGD(TAG, "Async seek done to %llu, [%p-%s]", seek_byte_pos, io, OBJ_GET_TAG(io));
    } else {
        /* Set position as subclasses might rely on the updated position to perform the seek (e.g., HTTP range header) */
        esp_gmf_io_set_pos(io, seek_byte_pos);
        ret = io->seek(io, seek_byte_pos);
    }
    return ret;
}

esp_gmf_err_t esp_gmf_io_close(esp_gmf_io_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    if (io->close == NULL) {
        return ESP_GMF_ERR_NOT_READY;
    }
    ESP_LOGD(TAG, "%s, [%p-%s]", __func__, io, OBJ_GET_TAG(io));
    if (io->task_hd && io->task_timeout_ms > 0) {
        xEventGroupWaitBits((EventGroupHandle_t)io->evt_group, IO_EVT_TASK_BLOCK_RUN_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(io->task_timeout_ms));
    }
    if (io->prev_close) {
        io->prev_close(io);
    }
    if (io->data_bus && io->task_hd) {
        esp_gmf_db_abort(io->data_bus);
        if (io->_is_hold) {
            io->_is_hold = 0;
            xEventGroupSetBits((EventGroupHandle_t)io->evt_group, IO_EVT_TASK_HOLD_DONE_BIT);
        }
        esp_gmf_task_stop(io->task_hd);
    }
    return io->close(io);
}

esp_gmf_err_io_t esp_gmf_io_acquire_read(esp_gmf_io_handle_t handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_IO_FAIL);
    ESP_GMF_NULL_CHECK(TAG, load, return ESP_GMF_IO_FAIL);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    if (io->_is_abort) {
        load->valid_size = 0;
        return ESP_GMF_IO_ABORT;
    }
    if (io->_is_done) {
        load->valid_size = 0;
        load->is_done = true;
        return ESP_GMF_IO_OK;
    }
    if (io->data_bus && io->task_hd) {
        if (io->io_cfg.buffer_cfg.read_filter) {
            return io->io_cfg.buffer_cfg.read_filter(io, load, wanted_size, wait_ticks);
        }
        esp_gmf_err_io_t ret = esp_gmf_db_acquire_read(io->data_bus, &io->db_block, wanted_size, wait_ticks);
        if (ret != ESP_GMF_IO_OK) {
            return ret;
        }
        load->buf = io->db_block.buf;
        load->buf_length = io->db_block.buf_length;
        load->valid_size = io->db_block.valid_size;
        load->is_done = io->db_block.is_last;
        return ESP_GMF_IO_OK;
    } else {
        if (io->acquire_read == NULL) {
            return ESP_GMF_IO_FAIL;
        }
        uint64_t start_time_ms = esp_gmf_oal_sys_get_time_ms();
        esp_gmf_err_io_t ret = io->acquire_read(io, load, wanted_size, wait_ticks);
        if (ret == ESP_GMF_IO_OK) {
            uint64_t interval_ms = esp_gmf_oal_sys_get_time_ms() - start_time_ms;
            esp_gmf_io_update_speed_stats(io, load->valid_size, interval_ms);
        }
        return ret;
    }
}

esp_gmf_err_io_t esp_gmf_io_release_read(esp_gmf_io_handle_t handle, esp_gmf_payload_t *load, int wait_ticks)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_IO_FAIL);
    ESP_GMF_NULL_CHECK(TAG, load, return ESP_GMF_IO_FAIL);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    esp_gmf_err_io_t ret = ESP_GMF_IO_OK;
    if (io->_is_abort) {
        load->valid_size = 0;
        return ESP_GMF_IO_ABORT;
    }
    if (io->_is_done) {
        load->valid_size = 0;
        load->is_done = true;
        return ESP_GMF_IO_OK;
    }
    if (io->data_bus && io->task_hd) {
        if (load->valid_size < io->db_block.valid_size) {
            io->db_block.valid_size = load->valid_size;
        }
        ret = esp_gmf_db_release_read(io->data_bus, &io->db_block, wait_ticks);
        memset(&io->db_block, 0, sizeof(esp_gmf_data_bus_block_t));
        if (ret == ESP_GMF_IO_OK && load->valid_size > 0) {
            esp_gmf_info_file_update_pos(&io->attr, load->valid_size);
        }
        load->buf = NULL;
        load->buf_length = 0;
    } else {
        if (io->release_read == NULL) {
            return ESP_GMF_IO_FAIL;
        }
        ret = io->release_read(io, load, wait_ticks);
        if (ret == ESP_GMF_IO_OK && load->valid_size > 0) {
            esp_gmf_info_file_update_pos(&io->attr, load->valid_size);
        }
    }
    ESP_LOGV(TAG, "Read len = %d, pos = %llu/%llu", load->valid_size, io->attr.pos, io->attr.size);
    return ret;
}

esp_gmf_err_io_t esp_gmf_io_acquire_write(esp_gmf_io_handle_t handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_IO_FAIL);
    ESP_GMF_NULL_CHECK(TAG, load, return ESP_GMF_IO_FAIL);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    if (io->_is_abort) {
        load->valid_size = 0;
        return ESP_GMF_IO_ABORT;
    }
    if (io->_is_done) {
        load->valid_size = 0;
        load->is_done = true;
        return ESP_GMF_IO_OK;
    }
    if (io->data_bus && io->task_hd) {
        esp_gmf_err_io_t ret = esp_gmf_db_acquire_write(io->data_bus, &io->db_block, wanted_size, wait_ticks);
        if (ret == ESP_GMF_IO_OK) {
            load->buf = io->db_block.buf;
            load->buf_length = io->db_block.buf_length;
            load->valid_size = io->db_block.valid_size;
        }
        return ret;
    } else {
        if (io->acquire_write == NULL) {
            return ESP_GMF_IO_FAIL;
        }
        return io->acquire_write(io, load, wanted_size, wait_ticks);
    }
}

esp_gmf_err_io_t esp_gmf_io_release_write(esp_gmf_io_handle_t handle, esp_gmf_payload_t *load, int wait_ticks)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_IO_FAIL);
    ESP_GMF_NULL_CHECK(TAG, load, return ESP_GMF_IO_FAIL);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    esp_gmf_err_io_t ret = ESP_GMF_IO_OK;
    if (io->_is_abort) {
        load->valid_size = 0;
        return ESP_GMF_IO_ABORT;
    }
    if (io->_is_done) {
        load->valid_size = 0;
        load->is_done = true;
        return ESP_GMF_IO_OK;
    }
    if (io->data_bus && io->task_hd) {
        if (load->valid_size > 0) {
            if (io->db_block.buf == NULL) {
                return ESP_GMF_IO_FAIL;
            }
            if (io->db_block.buf_length >= load->valid_size) {
                io->db_block.valid_size = load->valid_size;
                io->db_block.is_last = load->is_done;
                ret = esp_gmf_db_release_write(io->data_bus, &io->db_block, wait_ticks);
                memset(&io->db_block, 0, sizeof(esp_gmf_data_bus_block_t));
                if (ret == ESP_GMF_IO_OK) {
                    esp_gmf_info_file_update_pos(&io->attr, load->valid_size);
                }
                load->buf = NULL;
                load->buf_length = 0;
            } else {
                ESP_LOGE(TAG, "ACQ_WR buf not enough, need:%d, got:%d", (int)load->valid_size, (int)io->db_block.buf_length);
                return ESP_GMF_IO_FAIL;
            }
        } else if (load->is_done) {
            ret = esp_gmf_db_done_write(io->data_bus);
        }
    } else {
        if (io->release_write == NULL) {
            return ESP_GMF_IO_FAIL;
        }
        uint64_t start_time_ms = esp_gmf_oal_sys_get_time_ms();
        ret = io->release_write(io, load, wait_ticks);
        if (ret == ESP_GMF_IO_OK) {
            uint64_t interval_ms = esp_gmf_oal_sys_get_time_ms() - start_time_ms;
            esp_gmf_io_update_speed_stats(io, load->valid_size, interval_ms);
            if (load->valid_size > 0) {
                esp_gmf_info_file_update_pos(&io->attr, load->valid_size);
            }
        }
    }
    ESP_LOGV(TAG, "Write len = %zu, pos = %llu/%llu", load->valid_size, io->attr.pos, io->attr.size);
    return ret;
}

esp_gmf_err_t esp_gmf_io_set_info(esp_gmf_io_handle_t handle, esp_gmf_info_file_t *info)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, info, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    memcpy(&io->attr, info, sizeof(esp_gmf_info_file_t));
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_io_get_info(esp_gmf_io_handle_t handle, esp_gmf_info_file_t *info)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, info, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    memcpy(info, &io->attr, sizeof(esp_gmf_info_file_t));
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_io_set_uri(esp_gmf_io_handle_t handle, const char *uri)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    int ret = esp_gmf_info_file_set_uri(&io->attr, uri);
    return ret;
}
esp_gmf_err_t esp_gmf_io_get_uri(esp_gmf_io_handle_t handle, char **uri)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, uri, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    return esp_gmf_info_file_get_uri(&io->attr, uri);
}

esp_gmf_err_t esp_gmf_io_set_pos(esp_gmf_io_handle_t handle, uint64_t byte_pos)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    return esp_gmf_info_file_set_pos(&io->attr, byte_pos);
}
esp_gmf_err_t esp_gmf_io_update_pos(esp_gmf_io_handle_t handle, uint64_t byte_pos)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    return esp_gmf_info_file_update_pos(&io->attr, byte_pos);
}

esp_gmf_err_t esp_gmf_io_get_pos(esp_gmf_io_handle_t handle, uint64_t *byte_pos)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, byte_pos, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    return esp_gmf_info_file_get_pos(&io->attr, byte_pos);
}

esp_gmf_err_t esp_gmf_io_set_size(esp_gmf_io_handle_t handle, uint64_t total_size)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    return esp_gmf_info_file_set_size(&io->attr, total_size);
}

esp_gmf_err_t esp_gmf_io_get_size(esp_gmf_io_handle_t handle, uint64_t *total_size)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, total_size, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    return esp_gmf_info_file_get_size(&io->attr, total_size);
}

esp_gmf_err_t esp_gmf_io_get_db_filled_size(esp_gmf_io_handle_t handle, uint32_t *filled_size)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, filled_size, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    if (io->data_bus) {
        return esp_gmf_db_get_filled_size(io->data_bus, filled_size);
    }
    return ESP_GMF_ERR_NOT_SUPPORT;
}

esp_gmf_err_t esp_gmf_io_get_type(esp_gmf_io_handle_t handle, esp_gmf_io_type_t *type)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    if (type) {
        esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
        *type = io->type;
        return ESP_GMF_ERR_OK;
    }
    return ESP_GMF_ERR_INVALID_ARG;
}

esp_gmf_err_t esp_gmf_io_reset(esp_gmf_io_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    int ret = ESP_GMF_ERR_OK;

    io->_is_done = 0;
    io->_is_abort = 0;
    io->seek_pos = ESP_GMF_IO_SEEK_POS_INVALID;
    esp_gmf_io_set_pos(handle, 0);
    esp_gmf_io_set_size(handle, 0);

    if (io->data_bus) {
        esp_gmf_db_reset(io->data_bus);
    }
    if (io->reset) {
        ret = io->reset(io);
    }
    if (io->task_hd) {
        io->_is_hold = 0;
        xEventGroupClearBits((EventGroupHandle_t)io->evt_group, IO_EVT_TASK_BLOCK_RUN_BIT | IO_EVT_TASK_HOLD_DONE_BIT | IO_EVT_TASK_SEEK_DONE_BIT);
        esp_gmf_task_reset(io->task_hd);
        io_register_task(handle);
    }
    esp_gmf_io_enable_speed_monitor(io, io->speed_stats != NULL);
    return ret;
}

esp_gmf_err_t esp_gmf_io_reload(esp_gmf_io_handle_t handle, const char *uri)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, uri, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    if (io->reload == NULL) {
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    io->_is_done = 0;
    io->_is_abort = 0;
    io->seek_pos = ESP_GMF_IO_SEEK_POS_INVALID;
    esp_gmf_io_set_pos(handle, 0);
    esp_gmf_io_set_size(handle, 0);

    if (io->data_bus) {
        esp_gmf_db_reset_done_write(io->data_bus);
    }

    esp_gmf_err_t ret = io->reload(io, uri);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "esp_gmf_io_reload failed");

    if (io->task_hd) {
        io->_is_hold = 0;
        xEventGroupClearBits((EventGroupHandle_t)io->evt_group, IO_EVT_TASK_BLOCK_RUN_BIT | IO_EVT_TASK_HOLD_DONE_BIT | IO_EVT_TASK_SEEK_DONE_BIT);
        esp_gmf_task_reset(io->task_hd);
        io_register_task(handle);
        ret = esp_gmf_task_run(io->task_hd);
    }
    esp_gmf_io_enable_speed_monitor(io, io->speed_stats != NULL);
    return ret;
}

esp_gmf_err_t esp_gmf_io_done(esp_gmf_io_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    io->_is_done = 1;
    if (io->data_bus && io->task_hd) {
        if (!io->_is_hold) {
            xEventGroupClearBits((EventGroupHandle_t)io->evt_group, IO_EVT_TASK_HOLD_DONE_BIT);
            io->_is_hold = 1;
        }
        esp_gmf_db_abort(io->data_bus);
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_io_clear_done(esp_gmf_io_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    io->_is_done = 0;
    esp_gmf_io_set_pos(handle, 0);
    if (io->data_bus && io->task_hd) {
        esp_gmf_db_reset(io->data_bus);
        if (io->_is_hold) {
            io->_is_hold = 0;
            xEventGroupSetBits((EventGroupHandle_t)io->evt_group, IO_EVT_TASK_HOLD_DONE_BIT);
        }
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_io_abort(esp_gmf_io_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    io->_is_abort = 1;
    if (io->data_bus && io->task_hd) {
        if (!io->_is_hold) {
            xEventGroupClearBits((EventGroupHandle_t)io->evt_group, IO_EVT_TASK_HOLD_DONE_BIT);
            io->_is_hold = 1;
        }
        esp_gmf_db_abort(io->data_bus);
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_io_clear_abort(esp_gmf_io_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    io->_is_abort = 0;
    if (io->data_bus && io->task_hd) {
        esp_gmf_db_clear_abort(io->data_bus);
        if (io->_is_hold) {
            io->_is_hold = 0;
            xEventGroupSetBits((EventGroupHandle_t)io->evt_group, IO_EVT_TASK_HOLD_DONE_BIT);
        }
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_io_set_task_timeout(esp_gmf_io_handle_t handle, int32_t timeout_ms)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_io_t *io = (esp_gmf_io_t *)handle;
    io->task_timeout_ms = timeout_ms;
    ESP_LOGD(TAG, "Set task timeout %ld ms to io %p-%s", timeout_ms, io, OBJ_GET_TAG(io));
    return ESP_GMF_ERR_OK;
}

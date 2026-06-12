/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_mutex.h"
#include "esp_gmf_data_queue.h"

#define ESP_GMF_DATA_QUEUE_HEAD_SIZE          (4)  /*!< Header size for each frame */
#define ESP_GMF_DATA_QUEUE_DATA_ARRIVE_BITS   (1)  /*!< Data arrive event bits */
#define ESP_GMF_DATA_QUEUE_DATA_CONSUME_BITS  (2)  /*!< Data consume event bits */
#define ESP_GMF_DATA_QUEUE_USER_FREE_BITS     (4)  /*!< User free event bits */
#define ESP_GMF_DATA_QUEUE_DEFAULT_ALIGNMENT  (4)  /*!< Default alignment for buffer */

typedef struct {
    uint32_t  data_offset;  /*!< Offset from record start to payload data */
    uint32_t  buf_length;   /*!< Payload buffer length returned by acquire_write */
    uint32_t  valid_size;   /*!< Actual payload size released by writer */
    bool      is_last;      /*!< Last block flag */
} esp_gmf_data_queue_meta_t;

struct esp_gmf_data_queue {
    void *buffer;         /*!< Buffer for queue */
    int   size;           /*!< Buffer size */
    int   fill_end;       /*!< Buffer write position before ring back */
    int   last_fill_end;  /*!< Memorized last fill end for rewind */
    int   fixed_wr_size;  /*!< Fixed write size */
    int   wp;             /*!< Write pointer */
    int   rp;             /*!< Read pointer */
    int   filled;         /*!< Buffer filled size */
    int   user;           /*!< Buffer reference by reader or writer */
    bool  quit;           /*!< Buffer quit flag */
    bool  is_done;        /*!< Writer done flag */
    void *lock;           /*!< Protect lock */
    void *write_lock;     /*!< Write lock to let only one writer at same time */
    void *event;          /*!< Event group to wake up reader or writer */
};

struct esp_gmf_data_queue_bus {
    esp_gmf_data_queue_t *queue;       /*!< Underlying data queue */
    uint8_t               addr_align;  /*!< Returned payload address alignment */
    uint8_t               size_align;  /*!< Returned payload size alignment */
};

static uint8_t data_queue_resolve_addr_align(uint8_t addr_align)
{
    return addr_align ? addr_align : esp_gmf_oal_get_spiram_cache_align();
}

static uint8_t data_queue_resolve_size_align(uint8_t size_align)
{
    return (size_align == 0 || size_align == 1) ? 1 : size_align;
}

static uint32_t data_queue_get_align_padding(uint8_t addr_align)
{
    return addr_align > 1 ? addr_align - 1 : 0;
}

static uint8_t *data_queue_align_ptr(uint8_t *ptr, uint8_t align)
{
    return (uint8_t *)ESP_GMF_OAL_ALIGN_UP((uintptr_t)ptr, (uintptr_t)align);
}

static int data_queue_release_user(esp_gmf_data_queue_t *queue)
{
    xEventGroupSetBits((EventGroupHandle_t)queue->event, ESP_GMF_DATA_QUEUE_USER_FREE_BITS);
    return 0;
}

static int data_queue_notify_data(esp_gmf_data_queue_t *queue)
{
    xEventGroupSetBits((EventGroupHandle_t)queue->event, ESP_GMF_DATA_QUEUE_DATA_ARRIVE_BITS);
    return 0;
}

static int data_queue_wait_bits(esp_gmf_data_queue_t *queue, EventBits_t bits, int block_ticks)
{
    EventBits_t ret = xEventGroupWaitBits((EventGroupHandle_t)queue->event, bits, pdTRUE, pdFALSE, block_ticks);
    return (ret & bits) ? 0 : ESP_GMF_IO_TIMEOUT;
}

static int data_queue_wait_data(esp_gmf_data_queue_t *queue, int block_ticks)
{
    queue->user++;
    esp_gmf_oal_mutex_unlock(queue->lock);
    int wait_ret = data_queue_wait_bits(queue, ESP_GMF_DATA_QUEUE_DATA_ARRIVE_BITS, block_ticks);
    esp_gmf_oal_mutex_lock(queue->lock);
    int ret = (queue->quit) ? ESP_GMF_IO_ABORT : wait_ret;
    queue->user--;
    data_queue_release_user(queue);
    return ret;
}

static int data_queue_data_consumed(esp_gmf_data_queue_t *queue)
{
    xEventGroupSetBits((EventGroupHandle_t)queue->event, ESP_GMF_DATA_QUEUE_DATA_CONSUME_BITS);
    return 0;
}

static int data_queue_wait_consume(esp_gmf_data_queue_t *queue, int block_ticks)
{
    queue->user++;
    esp_gmf_oal_mutex_unlock(queue->lock);
    int wait_ret = data_queue_wait_bits(queue, ESP_GMF_DATA_QUEUE_DATA_CONSUME_BITS, block_ticks);
    esp_gmf_oal_mutex_lock(queue->lock);
    int ret = (queue->quit) ? ESP_GMF_IO_ABORT : wait_ret;
    queue->user--;
    data_queue_release_user(queue);
    return ret;
}

static inline int time_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == ESP_GMF_DATA_QUEUE_WAIT_FOREVER) {
        return portMAX_DELAY;
    }
    return (int)(timeout_ms / portTICK_PERIOD_MS);
}

static int data_queue_wait_user(esp_gmf_data_queue_t *queue)
{
    esp_gmf_oal_mutex_unlock(queue->lock);
    data_queue_wait_bits(queue, ESP_GMF_DATA_QUEUE_USER_FREE_BITS, portMAX_DELAY);
    esp_gmf_oal_mutex_lock(queue->lock);
    return 0;
}

static bool data_queue_have_data_locked(esp_gmf_data_queue_t *queue)
{
    if (queue->wp == queue->rp && queue->fill_end == 0) {
        return false;
    }
    return true;
}

static bool data_queue_have_data_from_last(esp_gmf_data_queue_t *queue)
{
    return queue->filled ? true : false;
}

static int data_queue_get_available_size(esp_gmf_data_queue_t *queue)
{
    if (queue->wp > queue->rp) {
        return queue->size - queue->wp;
    }
    if (queue->wp == queue->rp) {
        if (queue->fill_end) {
            return 0;
        }
        return queue->size - queue->wp;
    }
    return queue->rp - queue->wp;
}

static int data_queue_acquire_write(esp_gmf_data_queue_t *queue, void **buffer, int size, int block_ticks)
{
    if (queue == NULL || buffer == NULL || size <= 0) {
        return -1;
    }
    *buffer = NULL;
    int avail = 0;
    size += ESP_GMF_DATA_QUEUE_HEAD_SIZE;
    if (size > queue->size) {
        return -1;
    }
    int ret = 0;
    esp_gmf_oal_mutex_lock(queue->write_lock);
    esp_gmf_oal_mutex_lock(queue->lock);
    while (!queue->quit) {
        avail = data_queue_get_available_size(queue);
        if (avail < size && queue->fill_end == 0) {
            if (queue->wp == queue->rp) {
                queue->wp = queue->rp = 0;
            }
            queue->fill_end = queue->wp;
            queue->last_fill_end = queue->wp;
            queue->wp = 0;
            avail = data_queue_get_available_size(queue);
        }
        if (avail >= size) {
            *buffer = (uint8_t *)queue->buffer + queue->wp + ESP_GMF_DATA_QUEUE_HEAD_SIZE;
            queue->user++;
            esp_gmf_oal_mutex_unlock(queue->lock);
            return 0;
        }
        ret = data_queue_wait_consume(queue, block_ticks);
        if (ret != 0) {
            break;
        }
    }
    ret = queue->quit ? ESP_GMF_IO_ABORT : ret;
    esp_gmf_oal_mutex_unlock(queue->lock);
    esp_gmf_oal_mutex_unlock(queue->write_lock);
    return ret;
}

static int data_queue_done_write(esp_gmf_data_queue_t *queue)
{
    if (queue == NULL) {
        return -1;
    }
    esp_gmf_oal_mutex_lock(queue->lock);
    queue->is_done = true;
    data_queue_notify_data(queue);
    esp_gmf_oal_mutex_unlock(queue->lock);
    return 0;
}

static int data_queue_reset_done_write(esp_gmf_data_queue_t *queue)
{
    if (queue == NULL) {
        return -1;
    }
    queue->is_done = false;
    return 0;
}

static int data_queue_acquire_read(esp_gmf_data_queue_t *queue, void **buffer, int *size, bool *is_last, int block_ticks)
{
    if (queue == NULL || buffer == NULL || size == NULL) {
        return -1;
    }
    *buffer = NULL;
    *size = 0;
    if (is_last != NULL) {
        *is_last = false;
    }
    esp_gmf_oal_mutex_lock(queue->lock);
    int ret = -1;
    while (!queue->quit) {
        if (queue->filled == 0) {
            if (queue->is_done) {
                if (is_last != NULL) {
                    *is_last = true;
                }
                ret = 0;
                break;
            }
            ret = data_queue_wait_data(queue, block_ticks);
            if (ret != 0) {
                break;
            }
            continue;
        }
        int cur_rp;
        if (queue->filled <= queue->wp) {
            cur_rp = queue->wp - queue->filled;
        } else {
            cur_rp = queue->wp + queue->fill_end - queue->filled;
        }
        uint8_t *data_buffer = (uint8_t *)queue->buffer + cur_rp;
        int data_size = *((int *)data_buffer);
        if (data_size < 0 || data_size > queue->size) {
            ret = -1;
            break;
        }
        *buffer = data_buffer + ESP_GMF_DATA_QUEUE_HEAD_SIZE;
        *size = data_size - ESP_GMF_DATA_QUEUE_HEAD_SIZE;
        if (is_last != NULL) {
            *is_last = queue->is_done && (queue->filled == data_size);
        }
        queue->user++;
        ret = 0;
        break;
    }
    ret = queue->quit ? ESP_GMF_IO_ABORT : ret;
    esp_gmf_oal_mutex_unlock(queue->lock);
    return ret;
}

esp_gmf_data_queue_t *esp_gmf_data_queue_create(int size)
{
    if (size <= ESP_GMF_DATA_QUEUE_HEAD_SIZE) {
        return NULL;
    }
    esp_gmf_data_queue_t *queue = esp_gmf_oal_calloc(1, sizeof(esp_gmf_data_queue_t));
    if (queue == NULL) {
        return NULL;
    }
    queue->buffer = esp_gmf_oal_malloc(size);
    queue->lock = esp_gmf_oal_mutex_create();
    queue->write_lock = esp_gmf_oal_mutex_create();
    queue->event = xEventGroupCreate();
    if (queue->buffer == NULL || queue->lock == NULL || queue->write_lock == NULL || queue->event == NULL) {
        esp_gmf_data_queue_destroy(queue);
        return NULL;
    }
    queue->size = size;
    return queue;
}

int esp_gmf_data_queue_wakeup(esp_gmf_data_queue_t *queue)
{
    if (queue == NULL || queue->lock == NULL) {
        return -1;
    }
    esp_gmf_oal_mutex_lock(queue->lock);
    queue->quit = 1;
    data_queue_notify_data(queue);
    data_queue_data_consumed(queue);
    while (queue->user) {
        data_queue_wait_user(queue);
    }
    esp_gmf_oal_mutex_unlock(queue->lock);
    return 0;
}

int esp_gmf_data_queue_reset(esp_gmf_data_queue_t *queue)
{
    if (queue == NULL) {
        return -1;
    }
    esp_gmf_oal_mutex_lock(queue->lock);
    queue->fill_end = 0;
    queue->last_fill_end = 0;
    queue->fixed_wr_size = 0;
    queue->wp = 0;
    queue->rp = 0;
    queue->filled = 0;
    queue->quit = 0;
    queue->is_done = false;
    xEventGroupClearBits((EventGroupHandle_t)queue->event,
                         ESP_GMF_DATA_QUEUE_DATA_ARRIVE_BITS | ESP_GMF_DATA_QUEUE_DATA_CONSUME_BITS | ESP_GMF_DATA_QUEUE_USER_FREE_BITS);
    esp_gmf_oal_mutex_unlock(queue->lock);
    return 0;
}

void esp_gmf_data_queue_destroy(esp_gmf_data_queue_t *queue)
{
    if (queue == NULL) {
        return;
    }
    if (queue->lock) {
        esp_gmf_oal_mutex_destroy(queue->lock);
    }
    if (queue->write_lock) {
        esp_gmf_oal_mutex_destroy(queue->write_lock);
    }
    if (queue->event) {
        vEventGroupDelete((EventGroupHandle_t)queue->event);
    }
    if (queue->buffer) {
        esp_gmf_oal_free(queue->buffer);
    }
    esp_gmf_oal_free(queue);
}

int esp_gmf_data_queue_consume_all(esp_gmf_data_queue_t *queue)
{
    if (queue && queue->lock) {
        esp_gmf_oal_mutex_lock(queue->lock);
        while (data_queue_have_data_locked(queue)) {
            if (queue->quit) {
                break;
            }
            uint8_t *buffer = (uint8_t *)queue->buffer + queue->rp;
            int size = *((int *)buffer);
            if (size < 0 || size > queue->size) {
                break;
            }
            queue->rp += size;
            queue->filled -= size;
            if (queue->fill_end && queue->rp >= queue->fill_end) {
                queue->fill_end = 0;
                queue->rp = 0;
            }
            data_queue_data_consumed(queue);
        }
        esp_gmf_oal_mutex_unlock(queue->lock);
    }
    return 0;
}

int esp_gmf_data_queue_get_available(esp_gmf_data_queue_t *queue)
{
    if (queue == NULL) {
        return 0;
    }
    esp_gmf_oal_mutex_lock(queue->lock);
    int avail;
    if (queue->wp == queue->rp && queue->fill_end == 0) {
        avail = queue->size;
    } else {
        avail = data_queue_get_available_size(queue);
    }
    if (avail >= ESP_GMF_DATA_QUEUE_HEAD_SIZE) {
        avail -= ESP_GMF_DATA_QUEUE_HEAD_SIZE;
    } else {
        avail = 0;
    }
    esp_gmf_oal_mutex_unlock(queue->lock);
    return avail;
}

int esp_gmf_data_queue_acquire_write(esp_gmf_data_queue_t *queue, void **buffer, int size, uint32_t timeout)
{
    return data_queue_acquire_write(queue, buffer, size, time_to_ticks(timeout));
}

void *esp_gmf_data_queue_get_write_data(esp_gmf_data_queue_t *queue)
{
    if (queue == NULL) {
        return NULL;
    }
    esp_gmf_oal_mutex_lock(queue->lock);
    uint8_t *buffer = (uint8_t *)queue->buffer + queue->wp;
    esp_gmf_oal_mutex_unlock(queue->lock);
    return buffer + ESP_GMF_DATA_QUEUE_HEAD_SIZE;
}

int esp_gmf_data_queue_release_write(esp_gmf_data_queue_t *queue, int size)
{
    int ret = -1;
    if (queue == NULL) {
        return -1;
    }
    esp_gmf_oal_mutex_lock(queue->lock);
    if (size == 0) {
        queue->user--;
        data_queue_release_user(queue);
        esp_gmf_oal_mutex_unlock(queue->lock);
        esp_gmf_oal_mutex_unlock(queue->write_lock);
        return 0;
    }
    size += ESP_GMF_DATA_QUEUE_HEAD_SIZE;
    if (data_queue_get_available_size(queue) >= size) {
        uint8_t *buffer = (uint8_t *)queue->buffer + queue->wp;
        *((int *)buffer) = size;
        if (size < 0 || size > queue->size) {
            queue->user--;
            data_queue_release_user(queue);
            esp_gmf_oal_mutex_unlock(queue->lock);
            esp_gmf_oal_mutex_unlock(queue->write_lock);
            return -1;
        }
        queue->wp += size;
        queue->fixed_wr_size = size;
        queue->filled += size;
        queue->user--;
        data_queue_notify_data(queue);
        data_queue_release_user(queue);
        esp_gmf_oal_mutex_unlock(queue->lock);
        esp_gmf_oal_mutex_unlock(queue->write_lock);
        ret = 0;
    } else {
        queue->user--;
        data_queue_release_user(queue);
        esp_gmf_oal_mutex_unlock(queue->lock);
        esp_gmf_oal_mutex_unlock(queue->write_lock);
    }
    return ret;
}

int esp_gmf_data_queue_acquire_read(esp_gmf_data_queue_t *queue, void **buffer, int *size, uint32_t timeout)
{
    return data_queue_acquire_read(queue, buffer, size, NULL, time_to_ticks(timeout));
}

int esp_gmf_data_queue_peek_release(esp_gmf_data_queue_t *queue)
{
    int ret = -1;
    if (queue) {
        esp_gmf_oal_mutex_lock(queue->lock);
        queue->user--;
        esp_gmf_oal_mutex_unlock(queue->lock);
        return 0;
    }
    return ret;
}

int esp_gmf_data_queue_release_read(esp_gmf_data_queue_t *queue)
{
    int ret = -1;
    if (queue) {
        esp_gmf_oal_mutex_lock(queue->lock);
        if (data_queue_have_data_locked(queue)) {
            uint8_t *buffer = (uint8_t *)queue->buffer + queue->rp;
            int size = *((int *)buffer);
            if (size < 0 || size > queue->size) {
                esp_gmf_oal_mutex_unlock(queue->lock);
                return -1;
            }
            queue->rp += size;
            queue->filled -= size;
            if (queue->fill_end && queue->rp >= queue->fill_end) {
                queue->fill_end = 0;
                queue->rp = 0;
            }
            queue->user--;
            data_queue_data_consumed(queue);
            data_queue_release_user(queue);
        }
        esp_gmf_oal_mutex_unlock(queue->lock);
        return 0;
    }
    return ret;
}

int esp_gmf_data_queue_rewind(esp_gmf_data_queue_t *queue, int blocks)
{
    int ret = -1;
    if (queue == NULL) {
        return ret;
    }
    esp_gmf_oal_mutex_lock(queue->lock);
    if (queue->fixed_wr_size == 0) {
        esp_gmf_oal_mutex_unlock(queue->lock);
        return ret;
    }
    uint8_t *cur_wp_end = (uint8_t *)queue->buffer + queue->wp;
    int move_rp = queue->rp;
    uint8_t *cur_wp_start = queue->buffer;
    int loop_count = queue->last_fill_end ? 2 : 1;
    int filled_size = 0;
    for (int i = 0; i < loop_count; i++) {
        bool valid_block = false;
        while (blocks > 0) {
            if (cur_wp_end > cur_wp_start) {
                uint8_t *rp = (uint8_t *)cur_wp_end - queue->fixed_wr_size;
                if (rp >= cur_wp_start) {
                    int size = *(int *)rp;
                    if (size == queue->fixed_wr_size) {
                        valid_block = true;
                        blocks--;
                        move_rp = (int)(rp - (uint8_t *)queue->buffer);
                        cur_wp_end = rp;
                        filled_size += queue->fixed_wr_size;
                        continue;
                    }
                }
                blocks = 0;
            }
            break;
        }
        if (blocks == 0) {
            if (valid_block) {
                queue->rp = move_rp;
                if (i == 1) {
                    queue->fill_end = queue->last_fill_end;
                }
                queue->filled = filled_size;
                ret = 0;
            }
            break;
        }
        cur_wp_start = (uint8_t *)queue->buffer + queue->wp;
        cur_wp_end = (uint8_t *)queue->buffer + queue->last_fill_end;
    }
    esp_gmf_oal_mutex_unlock(queue->lock);
    return ret;
}

int esp_gmf_data_queue_have_data(esp_gmf_data_queue_t *queue, bool *have_data)
{
    if (queue == NULL || have_data == NULL) {
        return -1;
    }
    int ret = 0;
    esp_gmf_oal_mutex_lock(queue->lock);
    if (!queue->quit) {
        *have_data = data_queue_have_data_locked(queue);
    } else {
        ret = ESP_GMF_IO_ABORT;
    }
    esp_gmf_oal_mutex_unlock(queue->lock);
    return ret;
}

int esp_gmf_data_queue_query(esp_gmf_data_queue_t *queue, int *q_num, int *q_size)
{
    if (queue == NULL || q_num == NULL || q_size == NULL) {
        return -1;
    }
    esp_gmf_oal_mutex_lock(queue->lock);
    *q_num = *q_size = 0;
    if (data_queue_have_data_locked(queue)) {
        int rp = queue->rp;
        int ring = queue->fill_end;
        while (rp != queue->wp || ring) {
            int size = *(int *)((uint8_t *)queue->buffer + rp);
            if (size < 0 || size > queue->size) {
                esp_gmf_oal_mutex_unlock(queue->lock);
                return -1;
            }
            rp += size;
            if (ring && rp == ring) {
                ring = 0;
                rp = 0;
            }
            (*q_num)++;
            (*q_size) += size - ESP_GMF_DATA_QUEUE_HEAD_SIZE;
        }
    }
    esp_gmf_oal_mutex_unlock(queue->lock);
    return 0;
}

esp_gmf_data_queue_bus_t *esp_gmf_data_queue_db_create(int size)
{
    esp_gmf_data_queue_bus_t *bus = esp_gmf_oal_calloc(1, sizeof(esp_gmf_data_queue_bus_t));
    if (bus == NULL) {
        return NULL;
    }
    bus->queue = esp_gmf_data_queue_create(size);
    if (bus->queue == NULL) {
        esp_gmf_oal_free(bus);
        return NULL;
    }
    bus->addr_align = ESP_GMF_DATA_QUEUE_DEFAULT_ALIGNMENT;
    bus->size_align = 1;
    return bus;
}

esp_gmf_err_t esp_gmf_data_queue_db_destroy(esp_gmf_db_handle_t handle)
{
    if (handle == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_data_queue_bus_t *bus = (esp_gmf_data_queue_bus_t *)handle;
    esp_gmf_data_queue_destroy(bus->queue);
    esp_gmf_oal_free(bus);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_io_t esp_gmf_data_queue_db_acquire_read(esp_gmf_db_handle_t handle, esp_gmf_data_bus_block_t *blk, uint32_t wanted_size, int block_ticks)
{
    (void)wanted_size;
    if (handle == NULL || blk == NULL) {
        return ESP_GMF_IO_FAIL;
    }
    esp_gmf_data_queue_bus_t *bus = (esp_gmf_data_queue_bus_t *)handle;
    void *buffer = NULL;
    int size = 0;
    bool is_last = false;
    int ret = data_queue_acquire_read(bus->queue, &buffer, &size, &is_last, block_ticks);
    if (ret != 0) {
        if (ret == ESP_GMF_IO_ABORT) {
            return ESP_GMF_IO_ABORT;
        }
        if (ret == ESP_GMF_IO_TIMEOUT) {
            return ESP_GMF_IO_TIMEOUT;
        }
        return ESP_GMF_IO_FAIL;
    }
    memset(blk, 0, sizeof(*blk));
    if (size == 0) {
        blk->is_last = is_last;
        return ESP_GMF_IO_OK;
    }
    if (size < (int)sizeof(esp_gmf_data_queue_meta_t)) {
        esp_gmf_data_queue_release_read(bus->queue);
        return ESP_GMF_IO_FAIL;
    }
    esp_gmf_data_queue_meta_t *meta = (esp_gmf_data_queue_meta_t *)buffer;
    if (meta->data_offset > (uint32_t)size || meta->valid_size > ((uint32_t)size - meta->data_offset)) {
        esp_gmf_data_queue_release_read(bus->queue);
        return ESP_GMF_IO_FAIL;
    }
    blk->buf = (uint8_t *)buffer + meta->data_offset;
    blk->buf_length = meta->valid_size;
    blk->valid_size = meta->valid_size;
    blk->is_last = is_last || meta->is_last;
    return ESP_GMF_IO_OK;
}

esp_gmf_err_io_t esp_gmf_data_queue_db_release_read(esp_gmf_db_handle_t handle, esp_gmf_data_bus_block_t *blk, int block_ticks)
{
    (void)block_ticks;
    if (handle == NULL) {
        return ESP_GMF_IO_FAIL;
    }
    esp_gmf_data_queue_bus_t *bus = (esp_gmf_data_queue_bus_t *)handle;
    if (blk != NULL && blk->is_last && blk->valid_size == 0) {
        return ESP_GMF_IO_OK;
    }
    return esp_gmf_data_queue_release_read(bus->queue) == 0 ? ESP_GMF_IO_OK : ESP_GMF_IO_FAIL;
}

esp_gmf_err_io_t esp_gmf_data_queue_db_acquire_write(esp_gmf_db_handle_t handle, esp_gmf_data_bus_block_t *blk, uint32_t wanted_size, int block_ticks)
{
    if (handle == NULL || blk == NULL || wanted_size == 0) {
        return ESP_GMF_IO_FAIL;
    }
    esp_gmf_data_queue_bus_t *bus = (esp_gmf_data_queue_bus_t *)handle;
    uint32_t buf_length = (uint32_t)ESP_GMF_OAL_ALIGN_UP((uintptr_t)wanted_size, (uintptr_t)bus->size_align);
    uint32_t reserve_size = sizeof(esp_gmf_data_queue_meta_t) + data_queue_get_align_padding(bus->addr_align) + buf_length;
    void *buffer = NULL;
    int ret = data_queue_acquire_write(bus->queue, &buffer, (int)reserve_size, block_ticks);
    if (ret != 0) {
        if (ret == ESP_GMF_IO_ABORT) {
            return ESP_GMF_IO_ABORT;
        }
        if (ret == ESP_GMF_IO_TIMEOUT) {
            return ESP_GMF_IO_TIMEOUT;
        }
        return ESP_GMF_IO_FAIL;
    }
    esp_gmf_data_queue_meta_t *meta = (esp_gmf_data_queue_meta_t *)buffer;
    uint8_t *payload = data_queue_align_ptr((uint8_t *)buffer + sizeof(esp_gmf_data_queue_meta_t), bus->addr_align);
    memset(meta, 0, sizeof(*meta));
    meta->data_offset = payload - (uint8_t *)buffer;
    meta->buf_length = buf_length;
    memset(blk, 0, sizeof(*blk));
    blk->buf = payload;
    blk->buf_length = buf_length;
    return ESP_GMF_IO_OK;
}

esp_gmf_err_io_t esp_gmf_data_queue_db_release_write(esp_gmf_db_handle_t handle, esp_gmf_data_bus_block_t *blk, int block_ticks)
{
    (void)block_ticks;
    if (handle == NULL || blk == NULL || blk->valid_size > blk->buf_length) {
        return ESP_GMF_IO_FAIL;
    }
    esp_gmf_data_queue_bus_t *bus = (esp_gmf_data_queue_bus_t *)handle;
    uint8_t *record = (uint8_t *)esp_gmf_data_queue_get_write_data(bus->queue);
    if (record == NULL) {
        return ESP_GMF_IO_FAIL;
    }
    esp_gmf_data_queue_meta_t *meta = (esp_gmf_data_queue_meta_t *)record;
    if (blk->buf != record + meta->data_offset || blk->valid_size > meta->buf_length) {
        return ESP_GMF_IO_FAIL;
    }
    meta->valid_size = blk->valid_size;
    meta->is_last = blk->is_last;
    uint32_t release_size = meta->data_offset + blk->valid_size;
    if (esp_gmf_data_queue_release_write(bus->queue, (int)release_size) != 0) {
        return ESP_GMF_IO_FAIL;
    }
    if (blk->is_last) {
        data_queue_done_write(bus->queue);
    }
    return ESP_GMF_IO_OK;
}

esp_gmf_err_t esp_gmf_data_queue_db_done_write(esp_gmf_db_handle_t handle)
{
    if (handle == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_data_queue_bus_t *bus = (esp_gmf_data_queue_bus_t *)handle;
    return data_queue_done_write(bus->queue) == 0 ? ESP_GMF_ERR_OK : ESP_GMF_ERR_FAIL;
}

esp_gmf_err_t esp_gmf_data_queue_db_reset_done_write(esp_gmf_db_handle_t handle)
{
    if (handle == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_data_queue_bus_t *bus = (esp_gmf_data_queue_bus_t *)handle;
    return data_queue_reset_done_write(bus->queue) == 0 ? ESP_GMF_ERR_OK : ESP_GMF_ERR_FAIL;
}

esp_gmf_err_t esp_gmf_data_queue_db_set_align(esp_gmf_db_handle_t handle, uint8_t addr_align, uint8_t size_align)
{
    if (handle == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    uint8_t resolved_addr = data_queue_resolve_addr_align(addr_align);
    uint8_t resolved_size = data_queue_resolve_size_align(size_align);
    if (!ESP_GMF_OAL_ALIGN_BYTES_VALID(resolved_addr)) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    if (!ESP_GMF_OAL_ALIGN_BYTES_VALID(resolved_size)) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_data_queue_bus_t *bus = (esp_gmf_data_queue_bus_t *)handle;
    bus->addr_align = resolved_addr;
    bus->size_align = resolved_size;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_data_queue_db_reset(esp_gmf_db_handle_t handle)
{
    if (handle == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_data_queue_bus_t *bus = (esp_gmf_data_queue_bus_t *)handle;
    return esp_gmf_data_queue_reset(bus->queue) == 0 ? ESP_GMF_ERR_OK : ESP_GMF_ERR_FAIL;
}

esp_gmf_err_t esp_gmf_data_queue_db_abort(esp_gmf_db_handle_t handle)
{
    if (handle == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_data_queue_bus_t *bus = (esp_gmf_data_queue_bus_t *)handle;
    esp_gmf_data_queue_wakeup(bus->queue);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_data_queue_db_clear_abort(esp_gmf_db_handle_t handle)
{
    if (handle == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_data_queue_bus_t *bus = (esp_gmf_data_queue_bus_t *)handle;
    esp_gmf_data_queue_t *queue = bus->queue;
    esp_gmf_oal_mutex_lock(queue->lock);
    queue->quit = 0;
    xEventGroupClearBits((EventGroupHandle_t)queue->event,
                         ESP_GMF_DATA_QUEUE_DATA_ARRIVE_BITS | ESP_GMF_DATA_QUEUE_DATA_CONSUME_BITS | ESP_GMF_DATA_QUEUE_USER_FREE_BITS);
    if (queue->filled > 0) {
        data_queue_notify_data(queue);
    }
    if (data_queue_get_available_size(queue) > ESP_GMF_DATA_QUEUE_HEAD_SIZE) {
        data_queue_data_consumed(queue);
    }
    esp_gmf_oal_mutex_unlock(queue->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_data_queue_db_get_total_size(esp_gmf_db_handle_t handle, uint32_t *size)
{
    if (handle == NULL || size == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_data_queue_bus_t *bus = (esp_gmf_data_queue_bus_t *)handle;
    esp_gmf_data_queue_t *queue = bus->queue;
    esp_gmf_oal_mutex_lock(queue->lock);
    *size = queue->size;
    esp_gmf_oal_mutex_unlock(queue->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_data_queue_db_get_filled_size(esp_gmf_db_handle_t handle, uint32_t *filled_size)
{
    if (handle == NULL || filled_size == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_data_queue_bus_t *bus = (esp_gmf_data_queue_bus_t *)handle;
    esp_gmf_data_queue_t *queue = bus->queue;
    esp_gmf_oal_mutex_lock(queue->lock);
    int q_num = 0;
    int q_size = 0;
    if (data_queue_have_data_locked(queue)) {
        int rp = queue->rp;
        int ring = queue->fill_end;
        while (rp != queue->wp || ring) {
            int size = *(int *)((uint8_t *)queue->buffer + rp);
            if (size < 0 || size > queue->size) {
                esp_gmf_oal_mutex_unlock(queue->lock);
                return ESP_GMF_ERR_FAIL;
            }
            int record_size = size - ESP_GMF_DATA_QUEUE_HEAD_SIZE;
            if (record_size < (int)sizeof(esp_gmf_data_queue_meta_t)) {
                esp_gmf_oal_mutex_unlock(queue->lock);
                return ESP_GMF_ERR_FAIL;
            }
            esp_gmf_data_queue_meta_t *meta = (esp_gmf_data_queue_meta_t *)((uint8_t *)queue->buffer + rp + ESP_GMF_DATA_QUEUE_HEAD_SIZE);
            if (meta->data_offset > (uint32_t)record_size || meta->valid_size > ((uint32_t)record_size - meta->data_offset)) {
                esp_gmf_oal_mutex_unlock(queue->lock);
                return ESP_GMF_ERR_FAIL;
            }
            rp += size;
            if (ring && rp == ring) {
                ring = 0;
                rp = 0;
            }
            q_num++;
            q_size += meta->valid_size;
        }
    }
    (void)q_num;
    *filled_size = q_size;
    esp_gmf_oal_mutex_unlock(queue->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_data_queue_db_get_free_size(esp_gmf_db_handle_t handle, uint32_t *free_size)
{
    if (handle == NULL || free_size == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_data_queue_bus_t *bus = (esp_gmf_data_queue_bus_t *)handle;
    int available = esp_gmf_data_queue_get_available(bus->queue);
    int overhead = ESP_GMF_DATA_QUEUE_HEAD_SIZE + sizeof(esp_gmf_data_queue_meta_t) +
                   data_queue_get_align_padding(bus->addr_align);
    *free_size = available > overhead ? (uint32_t)(available - overhead) : 0;
    return ESP_GMF_ERR_OK;
}

/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "capture_os.h"
#include "data_queue.h"

#define DATA_Q_ALLOC_HEAD_SIZE   (4)
#define DATA_Q_DATA_ARRIVE_BITS  (1)
#define DATA_Q_DATA_CONSUME_BITS (2)
#define DATA_Q_USER_FREE_BITS    (4)

#define _SET_BITS(group, bit) capture_event_group_set_bits((capture_event_grp_handle_t)group, bit)
// Need manual clear bits
#define _WAIT_BITS(group, bit)                                                                    \
    capture_event_group_wait_bits((capture_event_grp_handle_t)group, bit, CAPTURE_MAX_LOCK_TIME); \
    capture_event_group_clr_bits(group, bit)

#define _MUTEX_LOCK(mutex)   capture_mutex_lock((capture_mutex_handle_t)mutex, CAPTURE_MAX_LOCK_TIME)
#define _MUTEX_UNLOCK(mutex) capture_mutex_unlock((capture_mutex_handle_t)mutex)

static int data_queue_release_user(data_q_t *q)
{
    _SET_BITS(q->event, DATA_Q_USER_FREE_BITS);
    return 0;
}

static int data_queue_notify_data(data_q_t *q)
{
    _SET_BITS(q->event, DATA_Q_DATA_ARRIVE_BITS);
    return 0;
}

static int data_queue_wait_data(data_q_t *q)
{
    q->user++;
    _MUTEX_UNLOCK(q->lock);
    _WAIT_BITS(q->event, DATA_Q_DATA_ARRIVE_BITS);
    _MUTEX_LOCK(q->lock);
    int ret = (q->quit) ? -1 : 0;
    q->user--;
    data_queue_release_user(q);
    return ret;
}

static int data_queue_data_consumed(data_q_t *q)
{
    _SET_BITS(q->event, DATA_Q_DATA_CONSUME_BITS);
    return 0;
}

static int data_queue_wait_consume(data_q_t *q)
{
    q->user++;
    _MUTEX_UNLOCK(q->lock);
    _WAIT_BITS(q->event, DATA_Q_DATA_CONSUME_BITS);
    _MUTEX_LOCK(q->lock);
    int ret = (q->quit) ? -1 : 0;
    q->user--;
    data_queue_release_user(q);
    return ret;
}

static int data_queue_wait_user(data_q_t *q)
{
    _MUTEX_UNLOCK(q->lock);
    _WAIT_BITS(q->event, DATA_Q_USER_FREE_BITS);
    _MUTEX_LOCK(q->lock);
    return 0;
}

static bool _data_q_have_data(data_q_t *q)
{
    if (q->wp == q->rp && q->fill_end == 0) {
        return false;
    }
    return true;
}

static bool _data_q_have_data_from_last(data_q_t *q)
{
    return q->filled ? true : false;
}

data_q_t *data_q_init(int size)
{
    data_q_t *q = capture_calloc(1, sizeof(data_q_t));
    if (q == NULL) {
        return NULL;
    }
    q->buffer = capture_malloc(size);
    capture_mutex_create(&q->lock);
    capture_mutex_create(&q->write_lock);
    capture_event_group_create(&q->event);
    if (q->buffer == NULL || q->lock == NULL || q->write_lock == NULL || q->event == NULL) {
        data_q_deinit(q);
        return NULL;
    }
    q->size = size;
    return q;
}

void data_q_wakeup(data_q_t *q)
{
    if (q && q->lock) {
        _MUTEX_LOCK(q->lock);
        q->quit = 1;
        // send quit message to let user quit
        data_queue_notify_data(q);
        data_queue_data_consumed(q);
        while (q->user) {
            data_queue_wait_user(q);
        }
        _MUTEX_UNLOCK(q->lock);
    }
}

int data_q_consume_all(data_q_t *q)
{
    if (q && q->lock) {
        _MUTEX_LOCK(q->lock);
        while (_data_q_have_data(q)) {
            if (q->quit) {
                break;
            }
            uint8_t *buffer = (uint8_t *)q->buffer + q->rp;
            int size = *((int *)buffer);
            if (size < 0 || size >= q->size) {
                *(int *)0 = 0;
            }
            q->rp += size;
            q->filled -= size;
            if (q->fill_end && q->rp >= q->fill_end) {
                q->fill_end = 0;
                q->rp = 0;
            }
            data_queue_data_consumed(q);
        }
        _MUTEX_UNLOCK(q->lock);
    }
    return 0;
}

void data_q_deinit(data_q_t *q)
{
    if (q == NULL) {
        return;
    }
    if (q->lock) {
        capture_mutex_destroy((capture_mutex_handle_t)q->lock);
    }
    if (q->write_lock) {
        capture_mutex_destroy((capture_mutex_handle_t)q->write_lock);
    }
    if (q->event) {
        capture_event_group_destroy((capture_mutex_handle_t)q->event);
    }
    if (q->buffer) {
        capture_free(q->buffer);
    }
    capture_free(q);
}

/*   case 1:  [0...rp...wp...size]
 *   case 2:  [0...wp...rp...fillend...size]
 *   special case: wp == rp  fill_end set buffer full else empty
 */
static int get_available_size(data_q_t *q)
{
    if (q->wp > q->rp) {
        return q->size - q->wp;
    }
    if (q->wp == q->rp) {
        if (q->fill_end) {
            return 0;
        }
        return q->size - q->wp;
    }
    return q->rp - q->wp;
}

int data_q_get_avail(data_q_t *q)
{
    if (q == NULL) {
        return 0;
    }
    _MUTEX_LOCK(q->lock);
    int avail;
    // Handle corner case [0 rp==wp fifo_end]
    // Left fifo is not enough but actually fifo is empty
    if (q->wp == q->rp && q->fill_end == 0) {
        avail = q->size;
    } else {
        avail = get_available_size(q);
    }
    if (avail >= DATA_Q_ALLOC_HEAD_SIZE) {
        avail -= DATA_Q_ALLOC_HEAD_SIZE;
    } else {
        avail = 0;
    }
    _MUTEX_UNLOCK(q->lock);
    return avail;
}

void *data_q_get_buffer(data_q_t *q, int size)
{
    int avail = 0;
    size += DATA_Q_ALLOC_HEAD_SIZE;
    if (q == NULL || size > q->size) {
        return NULL;
    }
    _MUTEX_LOCK(q->write_lock);
    _MUTEX_LOCK(q->lock);
    while (!q->quit) {
        avail = get_available_size(q);
        // size not enough
        if (avail < size && q->fill_end == 0) {
            if (q->wp == q->rp) {
                q->wp = q->rp = 0;
            }
            q->fill_end = q->wp;
            q->last_fill_end = q->wp;
            q->wp = 0;
            avail = get_available_size(q);
        }
        if (avail >= size) {
            uint8_t *buffer = (uint8_t *)q->buffer + q->wp;
            q->user++;
            _MUTEX_UNLOCK(q->lock);
            return buffer + DATA_Q_ALLOC_HEAD_SIZE;
        }
        int ret = data_queue_wait_consume(q);
        if (ret != 0) {
            break;
        }
    }
    _MUTEX_UNLOCK(q->lock);
    _MUTEX_UNLOCK(q->write_lock);
    return NULL;
}

void *data_q_get_write_data(data_q_t *q)
{
    if (q == NULL) {
        return NULL;
    }
    _MUTEX_LOCK(q->lock);
    uint8_t *buffer = (uint8_t *)q->buffer + q->wp;
    _MUTEX_UNLOCK(q->lock);
    return buffer + DATA_Q_ALLOC_HEAD_SIZE;
}

int data_q_send_buffer(data_q_t *q, int size)
{
    int ret = -1;
    if (q == NULL) {
        return -1;
    }
    _MUTEX_LOCK(q->lock);
    if (size == 0) {
        q->user--;
        data_queue_release_user(q);
        _MUTEX_UNLOCK(q->lock);
        _MUTEX_UNLOCK(q->write_lock);
        return 0;
    }
    size += DATA_Q_ALLOC_HEAD_SIZE;
    if (get_available_size(q) >= size) {
        uint8_t *buffer = (uint8_t *)q->buffer + q->wp;
        *((int *)buffer) = size;
        if (size < 0 || size > q->size) {
            *(int *)0 = 0;
        }
        q->wp += size;
        q->fixed_wr_wize = size;
        q->filled += size;
        q->user--;
        data_queue_notify_data(q);
        data_queue_release_user(q);
        _MUTEX_UNLOCK(q->lock);
        _MUTEX_UNLOCK(q->write_lock);
        ret = 0;
    } else {
        q->user--;
        data_queue_release_user(q);
        _MUTEX_UNLOCK(q->lock);
        _MUTEX_UNLOCK(q->write_lock);
    }
    return ret;
}

bool data_q_have_data(data_q_t *q)
{
    int has_data = false;
    if (q == NULL) {
        return has_data;
    }
    _MUTEX_LOCK(q->lock);
    if (!q->quit) {
        has_data = _data_q_have_data(q);
    }
    _MUTEX_UNLOCK(q->lock);
    return has_data;
}

int data_q_read_lock(data_q_t *q, void **buffer, int *size)
{
    int ret = -1;
    if (q == NULL) {
        return -1;
    }
    _MUTEX_LOCK(q->lock);
    while (!q->quit) {
        if (_data_q_have_data_from_last(q) == false) {
            if (data_queue_wait_data(q) != 0) {
                ret = -1;
                break;
            }
            continue;
        }
        int cur_rp;
        if (q->filled <= q->wp) {
            cur_rp = q->wp - q->filled;
        } else {
            cur_rp = q->wp + q->fill_end - q->filled;
        }
        uint8_t *data_buffer = (uint8_t *)q->buffer + cur_rp;
        int data_size = *((int *)data_buffer);
        if (data_size < 0 || data_size > q->size) {
            *(int *)0 = 0;
        }
        *buffer = data_buffer + DATA_Q_ALLOC_HEAD_SIZE;
        *size = data_size - DATA_Q_ALLOC_HEAD_SIZE;
        q->user++;
        ret = 0;
        break;
    }
    _MUTEX_UNLOCK(q->lock);
    return ret;
}

int data_q_peek_unlock(data_q_t *q)
{
    int ret = -1;
    if (q) {
        _MUTEX_LOCK(q->lock);
        q->user--;
        _MUTEX_UNLOCK(q->lock);
        return 0;
    }
    return ret;
}

int data_q_read_unlock(data_q_t *q)
{
    int ret = -1;
    if (q) {
        _MUTEX_LOCK(q->lock);
        if (_data_q_have_data(q)) {
            uint8_t *buffer = (uint8_t *)q->buffer + q->rp;
            int size = *((int *)buffer);
            if (size < 0 || size > q->size) {
                *(int *)0 = 0;
            }
            q->rp += size;
            q->filled -= size;
            if (q->fill_end && q->rp >= q->fill_end) {
                q->fill_end = 0;
                q->rp = 0;
            }
            q->user--;
            data_queue_data_consumed(q);
            data_queue_release_user(q);
        }
        _MUTEX_UNLOCK(q->lock);
        return 0;
    }
    return ret;
}

int data_q_rewind(data_q_t *q, int blocks)
{
    int ret = -1;
    if (q == NULL) {
        return ret;
    }
    _MUTEX_LOCK(q->lock);
    if (q->fixed_wr_wize == 0) {
        _MUTEX_UNLOCK(q->lock);
        return ret;
    }
    uint8_t *cur_wp_end = (uint8_t *)q->buffer + q->wp;
    int move_rp = q->rp;
    uint8_t *cur_wp_start = q->buffer;
    int loop_count = q->last_fill_end ? 2 : 1;
    int filled_size = 0;
    for (int i = 0; i < loop_count; i++) {
        bool valid_block = false;
        while (blocks > 0) {
            if (cur_wp_end > cur_wp_start) {
                uint8_t *rp = (uint8_t *)cur_wp_end - q->fixed_wr_wize;
                if (rp >= cur_wp_start) {
                    int size = *(int *)rp;
                    if (size == q->fixed_wr_wize) {
                        valid_block = true;
                        blocks--;
                        move_rp = (int) (rp - (uint8_t *)q->buffer);
                        cur_wp_end = rp;
                        filled_size += q->fixed_wr_wize;
                        continue;
                    }
                }
                blocks = 0;
            }
            break;
        }
        if (blocks == 0) {
            if (valid_block) {
                // Change rp and fill_end and filled_size when success
                q->rp = move_rp;
                if (i == 1) {
                    q->fill_end = q->last_fill_end;
                }
                q->filled = filled_size;
                ret = 0;
            }
            break;
        }
        cur_wp_start = (uint8_t *)q->buffer + q->wp;
        cur_wp_end = (uint8_t *)q->buffer + q->last_fill_end;
    }
    _MUTEX_UNLOCK(q->lock);
    return ret;
}

int data_q_query(data_q_t *q, int *q_num, int *q_size)
{
    if (q) {
        _MUTEX_LOCK(q->lock);
        *q_num = *q_size = 0;
        if (_data_q_have_data(q)) {
            int rp = q->rp;
            int ring = q->fill_end;
            while (rp != q->wp || ring) {
                int size = *(int *)(q->buffer + rp);
                if (size < 0 || size > q->size) {
                    *(int *)0 = 0;
                }
                rp += size;
                if (ring && rp == ring) {
                    ring = 0;
                    rp = 0;
                }
                (*q_num)++;
                (*q_size) += size - DATA_Q_ALLOC_HEAD_SIZE;
            }
        }
        _MUTEX_UNLOCK(q->lock);
    }
    return 0;
}

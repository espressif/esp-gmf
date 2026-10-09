/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "esp_ble_audio_bap_api.h"

#include "bt_audio_le_tx_group.h"

#define BT_AUDIO_LE_TX_EVT_TIMER   BIT0
#define BT_AUDIO_LE_TX_EVT_EXIT    BIT1
#define BT_AUDIO_LE_TX_EVT_EXITED  BIT2

/*!< Rounds of back-to-back SDUs sent when the group clock starts */
#define BT_AUDIO_LE_TX_PRIME_ROUNDS  3

#define BT_AUDIO_LE_TX_QUEUE_DEPTH  CONFIG_ESP_BT_AUDIO_LE_TX_QUEUE_DEPTH

#if BT_AUDIO_LE_TX_QUEUE_DEPTH < BT_AUDIO_LE_TX_PRIME_ROUNDS
#define BT_AUDIO_LE_TX_PRIME_TARGET  BT_AUDIO_LE_TX_QUEUE_DEPTH
#else
#define BT_AUDIO_LE_TX_PRIME_TARGET  BT_AUDIO_LE_TX_PRIME_ROUNDS
#endif

/*!< Ticks between two attempts to read the transmit grid from the controller */
#define BT_AUDIO_LE_TX_GRID_PROBE_TICKS  4
/*!< Period of the grid check that keeps the group clock next to the controller clock */
#define BT_AUDIO_LE_TX_GRID_CHECK_US     1000000
/*!< Backlog deviation the group tolerates before it corrects its clock by one tick */
#define BT_AUDIO_LE_TX_GRID_SLACK        2
/*!< Re-anchor only after a real production gap, not a 1–2 SDU timer/ISO slack */
#define BT_AUDIO_LE_TX_GRID_REANCHOR_SDUS  4

static const char *TAG = "BT_AUD_LE_TX_GRP";

struct bt_audio_le_tx_group {
    bt_audio_le_stream_t  *members[BT_AUDIO_LE_TX_GROUP_MAX_MEMBERS];  /*!< Paced source streams */
    uint8_t                member_cnt;       /*!< Members added so far */
    esp_timer_handle_t     timer;            /*!< Group SDU clock */
    EventGroupHandle_t     events;           /*!< Pacing task events */
    TaskHandle_t           task;             /*!< Pacing task */
    bool                   task_cfg_set;     /*!< Task configuration was set explicitly */
    uint8_t                task_core_id;     /*!< Pacing task core ID */
    uint8_t                task_prio;        /*!< Pacing task priority */
    uint32_t               task_stack_size;  /*!< Pacing task stack size in bytes */
    uint32_t               interval_us;      /*!< Group SDU interval in us */
    uint16_t               seq;              /*!< Packet sequence number shared by all members */
    uint8_t                prime_cnt;        /*!< Prefill rounds done since the clock started */
    bool                   anchored;         /*!< Clock phase realigned after the prefill burst */
    bool                   implicit;         /*!< Created for a single stream, owned by that stream */
    uint32_t               grid_ts;          /*!< CIG reference point the grid is anchored to */
    uint16_t               grid_seq;         /*!< Sequence number transmitted at grid_ts */
    bool                   grid_valid;       /*!< Members transmit with an explicit timestamp */
    uint8_t                grid_probe;       /*!< Ticks left before the next grid probe */
    bool                   grid_skip;        /*!< Skip the next tick when host is ahead of the controller */
    int64_t                grid_check_us;    /*!< Next grid check time */
    char                   name[configMAX_TASK_NAME_LEN];  /*!< Pacing task and timer name */
};

static inline uint8_t bt_audio_le_tx_group_depth(const bt_audio_le_stream_t *stream)
{
    QueueHandle_t queue = (QueueHandle_t)stream->base.data_q;
    return queue ? (uint8_t)uxQueueMessagesWaiting(queue) : 0;
}

static bool bt_audio_le_tx_group_has(const bt_audio_le_tx_group_t *group,
                                     const bt_audio_le_stream_t *stream)
{
    for (size_t i = 0; i < group->member_cnt; i++) {
        if (group->members[i] == stream) {
            return true;
        }
    }
    return false;
}

static inline bool bt_audio_le_tx_group_is_paced(const bt_audio_le_tx_group_t *group)
{
    for (size_t i = 0; i < group->member_cnt; i++) {
        const bt_audio_le_stream_t *stream = group->members[i];
        if (stream && stream->tx_state != BT_AUDIO_LE_TX_STATE_IDLE) {
            return true;
        }
    }
    return false;
}

static uint8_t bt_audio_le_tx_group_live_cnt(const bt_audio_le_tx_group_t *group)
{
    uint8_t count = 0;

    for (size_t i = 0; i < group->member_cnt; i++) {
        const bt_audio_le_stream_t *stream = group->members[i];

        if (!stream) {
            continue;
        }
        if (stream->tx_state == BT_AUDIO_LE_TX_STATE_ACTIVE) {
            count++;
        }
    }
    return count;
}

void bt_audio_le_tx_group_credit_give(bt_audio_le_stream_t *stream)
{
    if (stream && stream->tx_credits) {
        xSemaphoreGive(stream->tx_credits);
    }
}

esp_err_t bt_audio_le_tx_group_credit_take(bt_audio_le_stream_t *stream, uint32_t wait_ms)
{
    if (!stream || !stream->tx_credits) {
        return ESP_OK;
    }
    return xSemaphoreTake(stream->tx_credits, pdMS_TO_TICKS(wait_ms)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void bt_audio_le_tx_group_credit_reset(bt_audio_le_stream_t *stream)
{
    if (!stream || !stream->tx_credits) {
        return;
    }
    while (uxSemaphoreGetCount(stream->tx_credits) < BT_AUDIO_LE_TX_QUEUE_DEPTH) {
        if (xSemaphoreGive(stream->tx_credits) != pdTRUE) {
            break;
        }
    }
}

static bool bt_audio_le_tx_group_take_packet(bt_audio_le_stream_t *stream,
                                             esp_bt_audio_stream_packet_t *packet)
{
    QueueHandle_t queue = (QueueHandle_t)stream->base.data_q;

    if (!queue || xQueueReceive(queue, packet, 0) != pdTRUE) {
        return false;
    }
    bt_audio_le_tx_group_credit_give(stream);
    return true;
}

static inline uint32_t bt_audio_le_tx_group_grid_ts(const bt_audio_le_tx_group_t *group, uint16_t seq)
{
    return group->grid_ts + (uint32_t)(uint16_t)(seq - group->grid_seq) * group->interval_us;
}

static bool bt_audio_le_tx_group_send_one(bt_audio_le_tx_group_t *group, bt_audio_le_stream_t *stream,
                                          uint16_t seq)
{
    esp_bt_audio_stream_packet_t packet = {0};

    if (!bt_audio_le_tx_group_take_packet(stream, &packet)) {
        return false;
    }
    if (!packet.data || packet.size == 0 || packet.size > UINT16_MAX) {
        ESP_LOGW(TAG, "Discard invalid TX packet, data=%p, size=%" PRIu32, packet.data, packet.size);
        bt_audio_le_stream_release_packet(&packet);
        return false;
    }

    esp_err_t ret = bt_audio_le_stream_tx_send(stream, packet.data, (uint16_t)packet.size, seq,
                                               group->grid_valid, bt_audio_le_tx_group_grid_ts(group, seq));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send BAP stream packet: %s", esp_err_to_name(ret));
    }
    bt_audio_le_stream_release_packet(&packet);
    return ret == ESP_OK;
}

static void bt_audio_le_tx_group_drop_one(bt_audio_le_stream_t *stream)
{
    esp_bt_audio_stream_packet_t packet = {0};

    if (bt_audio_le_tx_group_take_packet(stream, &packet)) {
        bt_audio_le_stream_release_packet(&packet);
    }
}

static uint8_t bt_audio_le_tx_group_burst_count(const bt_audio_le_tx_group_t *group)
{
    uint8_t active = 0;
    uint8_t deep = 0;
    bool need_burst = false;

    for (size_t i = 0; i < group->member_cnt; i++) {
        const bt_audio_le_stream_t *stream = group->members[i];

        if (!stream || stream->tx_state != BT_AUDIO_LE_TX_STATE_ACTIVE) {
            continue;
        }
        active++;
        if (bt_audio_le_tx_group_depth(stream) >= 2) {
            deep++;
        }
        if (stream->tx_need_burst) {
            need_burst = true;
        }
    }
    return (need_burst && active > 0 && deep == active) ? 2 : 1;
}

static uint8_t bt_audio_le_tx_group_round(bt_audio_le_tx_group_t *group)
{
    uint8_t want = bt_audio_le_tx_group_burst_count(group);
    uint8_t sent = 0;

    for (size_t i = 0; i < group->member_cnt; i++) {
        bt_audio_le_stream_t *stream = group->members[i];

        if (!stream) {
            continue;
        }
        switch (stream->tx_state) {
            case BT_AUDIO_LE_TX_STATE_ACTIVE: {
                uint8_t depth = bt_audio_le_tx_group_depth(stream);

                if (depth == 0) {
                    stream->tx_need_burst = true;
                    break;
                }
                uint8_t got = 0;

                for (uint8_t n = 0; n < want; n++) {
                    uint16_t seq = (uint16_t)(group->seq + n);

                    if (!bt_audio_le_tx_group_send_one(group, stream, seq)) {
                        break;
                    }
                    got++;
                }
                if (got > 0) {
                    stream->tx_need_burst = false;
                    sent += got;
                }
                break;
            }
            case BT_AUDIO_LE_TX_STATE_SHADOW:
                bt_audio_le_tx_group_drop_one(stream);
                break;
            default:
                break;
        }
    }
    if (sent > 0) {
        group->seq = (uint16_t)(group->seq + want);
    }
    return sent;
}

static void bt_audio_le_tx_group_grid_sync(bt_audio_le_tx_group_t *group)
{
    if (!group->interval_us) {
        return;
    }

    if (bt_audio_le_tx_group_live_cnt(group) == 0) {
        return;
    }

    if (group->grid_valid) {
        int64_t now = esp_timer_get_time();
        if (now < group->grid_check_us) {
            return;
        }
        group->grid_check_us = now + BT_AUDIO_LE_TX_GRID_CHECK_US;
    } else if (group->grid_probe) {
        group->grid_probe--;
        return;
    } else {
        group->grid_probe = BT_AUDIO_LE_TX_GRID_PROBE_TICKS;
    }

    bool grid_was_valid = group->grid_valid;
    uint16_t backlog = 0;

    for (size_t i = 0; i < group->member_cnt; i++) {
        bt_audio_le_stream_t *stream = group->members[i];

        if (!stream || stream->tx_state != BT_AUDIO_LE_TX_STATE_ACTIVE) {
            continue;
        }

        esp_ble_iso_tx_info_t info = {0};
        if (esp_ble_audio_bap_stream_get_tx_sync(&stream->bap_stream, &info) != ESP_OK) {
            if (stream->tx_last_seq == 0) {
                stream->tx_need_burst = true;
            }
            continue;
        }
        if (!group->grid_valid) {
            group->grid_ts = info.ts;
            group->grid_seq = info.seq_num;
            group->grid_valid = true;
            group->grid_check_us = esp_timer_get_time() + BT_AUDIO_LE_TX_GRID_CHECK_US;
            ESP_LOGI(TAG, "[%s] Transmit grid anchored on seq %u at ts %" PRIu32 ", step %" PRIu32 " us",
                     group->name, info.seq_num, info.ts, group->interval_us);
        } else {
            int32_t error_us = (int32_t)(info.ts - bt_audio_le_tx_group_grid_ts(group, info.seq_num));
            int32_t reanchor_us = (int32_t)group->interval_us * BT_AUDIO_LE_TX_GRID_REANCHOR_SDUS;
            if (error_us <= -reanchor_us || error_us >= reanchor_us) {
                ESP_LOGW(TAG, "[%s] Stream %p transmits %" PRId32 " us off the grid, re-anchoring",
                         group->name, stream, error_us);
                group->grid_ts = info.ts;
                group->grid_seq = info.seq_num;
            }
        }
        backlog = (uint16_t)(group->seq - info.seq_num);
        break;
    }

    if (grid_was_valid && backlog > (uint16_t)(BT_AUDIO_LE_TX_QUEUE_DEPTH + BT_AUDIO_LE_TX_GRID_SLACK)) {
        group->grid_skip = true;
    }
}

static void bt_audio_le_tx_group_anchor(bt_audio_le_tx_group_t *group)
{
    group->anchored = true;
    if (!group->timer || !group->interval_us) {
        return;
    }
    esp_timer_stop(group->timer);
    xEventGroupClearBits(group->events, BT_AUDIO_LE_TX_EVT_TIMER);
    esp_err_t ret = esp_timer_start_periodic(group->timer, group->interval_us);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to re-anchor TX pacing: %s", esp_err_to_name(ret));
    }
}

static void bt_audio_le_tx_group_timer_cb(void *arg)
{
    bt_audio_le_tx_group_t *group = (bt_audio_le_tx_group_t *)arg;

    if (group && group->events) {
        xEventGroupSetBits(group->events, BT_AUDIO_LE_TX_EVT_TIMER);
    }
}

static void bt_audio_le_tx_group_task(void *arg)
{
    bt_audio_le_tx_group_t *group = (bt_audio_le_tx_group_t *)arg;

    while (group->events) {
        EventBits_t bits = xEventGroupWaitBits(group->events,
                                               BT_AUDIO_LE_TX_EVT_TIMER | BT_AUDIO_LE_TX_EVT_EXIT,
                                               pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & BT_AUDIO_LE_TX_EVT_EXIT) {
            break;
        }
        if (!(bits & BT_AUDIO_LE_TX_EVT_TIMER)) {
            continue;
        }

        if (group->grid_skip) {
            group->grid_skip = false;
            continue;
        }
        uint8_t sent = bt_audio_le_tx_group_round(group);
        if (group->anchored) {
            bt_audio_le_tx_group_grid_sync(group);
        } else {
            if (sent) {
                group->prime_cnt++;
            }
            while (group->prime_cnt < BT_AUDIO_LE_TX_PRIME_TARGET) {
                if (bt_audio_le_tx_group_round(group) == 0) {
                    break;
                }
                group->prime_cnt++;
            }
            if (group->prime_cnt >= BT_AUDIO_LE_TX_PRIME_TARGET) {
                bt_audio_le_tx_group_anchor(group);
            }
        }
    }

    xEventGroupSetBits(group->events, BT_AUDIO_LE_TX_EVT_EXITED);
#if defined(CONFIG_SPIRAM_BOOT_INIT) && CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
    vTaskDeleteWithCaps(NULL);
#else
    vTaskDelete(NULL);
#endif
}

static void bt_audio_le_tx_group_reset_grid(bt_audio_le_tx_group_t *group)
{
    group->prime_cnt = 0;
    group->anchored = false;
    group->grid_valid = false;
    group->grid_probe = 0;
    group->grid_skip = false;
    group->grid_check_us = 0;
}

static esp_err_t bt_audio_le_tx_group_start(bt_audio_le_tx_group_t *group, uint32_t interval_us)
{
    group->interval_us = interval_us;

    if (!group->events) {
        group->events = xEventGroupCreate();
        ESP_RETURN_ON_FALSE(group->events, ESP_ERR_NO_MEM, TAG, "Failed to create TX events");
    }
    if (!group->timer) {
        esp_timer_create_args_t timer_args = {
            .callback = bt_audio_le_tx_group_timer_cb,
            .arg = group,
            .name = group->name,
        };
        esp_err_t ret = esp_timer_create(&timer_args, &group->timer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create TX timer: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    if (!group->task) {
        xEventGroupClearBits(group->events,
                             BT_AUDIO_LE_TX_EVT_TIMER | BT_AUDIO_LE_TX_EVT_EXIT | BT_AUDIO_LE_TX_EVT_EXITED);
        BaseType_t task_ret;
#if defined(CONFIG_SPIRAM_BOOT_INIT) && CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
        task_ret = xTaskCreatePinnedToCoreWithCaps(bt_audio_le_tx_group_task, group->name,
                                                   group->task_stack_size, group,
                                                   group->task_prio, &group->task,
                                                   group->task_core_id,
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        task_ret = xTaskCreatePinnedToCore(bt_audio_le_tx_group_task, group->name,
                                           group->task_stack_size, group,
                                           group->task_prio, &group->task,
                                           group->task_core_id);
#endif
        ESP_RETURN_ON_FALSE(task_ret == pdPASS, ESP_ERR_NO_MEM, TAG, "Failed to create TX task");
    }

    bt_audio_le_tx_group_reset_grid(group);
    esp_timer_stop(group->timer);
    xEventGroupClearBits(group->events, BT_AUDIO_LE_TX_EVT_TIMER);
    return esp_timer_start_periodic(group->timer, group->interval_us);
}

static void bt_audio_le_tx_group_stop(bt_audio_le_tx_group_t *group)
{
    if (group->timer) {
        esp_timer_stop(group->timer);
    }
    if (group->events) {
        xEventGroupClearBits(group->events, BT_AUDIO_LE_TX_EVT_TIMER);
    }
    for (size_t i = 0; i < group->member_cnt; i++) {
        bt_audio_le_stream_t *stream = group->members[i];

        if (!stream) {
            continue;
        }
        stream->tx_state = BT_AUDIO_LE_TX_STATE_IDLE;
        bt_audio_le_stream_tx_reset(stream);
        bt_audio_le_stream_flush_queue(stream);
        bt_audio_le_tx_group_credit_reset(stream);
    }
    group->interval_us = 0;
    bt_audio_le_tx_group_reset_grid(group);
}

esp_err_t bt_audio_le_tx_group_create(const char *name, bt_audio_le_tx_group_t **out_group)
{
    ESP_RETURN_ON_FALSE(out_group, ESP_ERR_INVALID_ARG, TAG, "out_group is NULL");
    *out_group = NULL;

    bt_audio_le_tx_group_t *group = heap_caps_calloc_prefer(1, sizeof(*group), 2,
                                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                                            MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(group, ESP_ERR_NO_MEM, TAG, "No memory for TX group");

    group->task_core_id = BT_AUDIO_LE_TX_TASK_CORE_ID_DEFAULT;
    group->task_prio = BT_AUDIO_LE_TX_TASK_PRIO_DEFAULT;
    group->task_stack_size = BT_AUDIO_LE_TX_TASK_STACK_SIZE_DEFAULT;
    group->seq = 1;
    if (name && name[0]) {
        snprintf(group->name, sizeof(group->name), "%s", name);
    } else {
        snprintf(group->name, sizeof(group->name), "le_tx_%p", group);
    }

    *out_group = group;
    return ESP_OK;
}

void bt_audio_le_tx_group_destroy(bt_audio_le_tx_group_t *group)
{
    if (!group) {
        return;
    }

    /* Join the pacing task before the blocking stop. After EXITED it only
     * deletes itself, so it cannot call start_periodic again. */
    if (group->task && group->events) {
        xEventGroupClearBits(group->events, BT_AUDIO_LE_TX_EVT_EXITED);
        xEventGroupSetBits(group->events, BT_AUDIO_LE_TX_EVT_EXIT);
        xEventGroupWaitBits(group->events, BT_AUDIO_LE_TX_EVT_EXITED,
                            pdTRUE, pdTRUE, portMAX_DELAY);
        group->task = NULL;
    }
    if (group->timer) {
        esp_err_t ret = esp_timer_stop_blocking(group->timer, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop TX timer: %s", esp_err_to_name(ret));
            return;
        }
    }
    bt_audio_le_tx_group_stop(group);
    if (group->timer) {
        esp_timer_delete(group->timer);
        group->timer = NULL;
    }
    if (group->events) {
        vEventGroupDelete(group->events);
        group->events = NULL;
    }
    for (size_t i = 0; i < group->member_cnt; i++) {
        if (group->members[i]) {
            group->members[i]->tx_group = NULL;
            group->members[i] = NULL;
        }
    }
    group->member_cnt = 0;
    heap_caps_free(group);
}

esp_err_t bt_audio_le_tx_group_add(bt_audio_le_tx_group_t *group, bt_audio_le_stream_t *stream)
{
    ESP_RETURN_ON_FALSE(group && stream, ESP_ERR_INVALID_ARG, TAG, "Invalid TX group member");
    ESP_RETURN_ON_FALSE(!stream->tx_group, ESP_ERR_INVALID_STATE, TAG,
                        "Stream %p already belongs to a TX group", stream);
    ESP_RETURN_ON_FALSE(group->member_cnt < BT_AUDIO_LE_TX_GROUP_MAX_MEMBERS, ESP_ERR_NO_MEM, TAG,
                        "TX group %s is full", group->name);

    if (!stream->tx_credits) {
        stream->tx_credits = xSemaphoreCreateCounting(BT_AUDIO_LE_TX_QUEUE_DEPTH,
                                                       BT_AUDIO_LE_TX_QUEUE_DEPTH);
        ESP_RETURN_ON_FALSE(stream->tx_credits, ESP_ERR_NO_MEM, TAG, "No memory for TX credits");
    }
    if (!group->task_cfg_set) {
        group->task_core_id = stream->tx_task_core_id;
        group->task_prio = stream->tx_task_prio;
        group->task_stack_size = stream->tx_task_stack_size;
    }
    stream->tx_state = BT_AUDIO_LE_TX_STATE_IDLE;
    stream->tx_group = group;
    group->members[group->member_cnt] = stream;
    group->member_cnt++;
    return ESP_OK;
}

esp_err_t bt_audio_le_tx_group_set_task_cfg(bt_audio_le_tx_group_t *group, uint8_t core_id,
                                            uint8_t prio, uint32_t stack_size)
{
    ESP_RETURN_ON_FALSE(group, ESP_ERR_INVALID_ARG, TAG, "TX group is NULL");
    ESP_RETURN_ON_FALSE(core_id < CONFIG_FREERTOS_NUMBER_OF_CORES, ESP_ERR_INVALID_ARG, TAG,
                        "Invalid LE source send task core ID: %d", core_id);
    ESP_RETURN_ON_FALSE(prio < 24, ESP_ERR_INVALID_ARG, TAG, "Invalid LE source send task priority: %d", prio);
    ESP_RETURN_ON_FALSE(stack_size > 0, ESP_ERR_INVALID_ARG, TAG,
                        "Invalid LE source send task stack size: %" PRIu32, stack_size);
    group->task_core_id = core_id;
    group->task_prio = prio;
    group->task_stack_size = stack_size;
    group->task_cfg_set = true;
    return ESP_OK;
}

esp_err_t bt_audio_le_tx_group_member_started(bt_audio_le_stream_t *stream)
{
    ESP_RETURN_ON_FALSE(stream, ESP_ERR_INVALID_ARG, TAG, "TX group member is NULL");

    if (!stream->tx_group) {
        bt_audio_le_tx_group_t *group = NULL;
        ESP_RETURN_ON_ERROR(bt_audio_le_tx_group_create(stream->tx_name, &group), TAG,
                            "Failed to create implicit TX group");
        group->implicit = true;
        esp_err_t ret = bt_audio_le_tx_group_add(group, stream);
        if (ret != ESP_OK) {
            bt_audio_le_tx_group_destroy(group);
            return ret;
        }
    }

    bt_audio_le_tx_group_t *group = stream->tx_group;
    ESP_RETURN_ON_FALSE(bt_audio_le_tx_group_has(group, stream), ESP_ERR_INVALID_STATE, TAG,
                        "Stream %p left its TX group", stream);
    uint32_t interval_us = stream->sdu_interval_us ? stream->sdu_interval_us : stream->iso_interval;
    ESP_RETURN_ON_FALSE(interval_us, ESP_ERR_INVALID_STATE, TAG, "TX interval is unavailable");

    bool running = bt_audio_le_tx_group_is_paced(group);
    bt_audio_le_stream_tx_reset(stream);
    stream->tx_state = BT_AUDIO_LE_TX_STATE_ACTIVE;
    if (!running) {
        return bt_audio_le_tx_group_start(group, interval_us);
    }
    if (group->interval_us != interval_us) {
        ESP_LOGW(TAG, "[%s] Stream %p wants a %" PRIu32 " us SDU interval, group paces %" PRIu32 " us",
                 group->name, stream, interval_us, group->interval_us);
    }
    return ESP_OK;
}

bool bt_audio_le_tx_group_member_stopped(bt_audio_le_stream_t *stream)
{
    if (!stream) {
        return false;
    }
    bt_audio_le_tx_group_t *group = stream->tx_group;
    if (!group) {
        stream->tx_state = BT_AUDIO_LE_TX_STATE_IDLE;
        return false;
    }

    stream->tx_state = BT_AUDIO_LE_TX_STATE_IDLE;
    if (bt_audio_le_tx_group_live_cnt(group) > 0) {
        stream->tx_state = BT_AUDIO_LE_TX_STATE_SHADOW;
        ESP_LOGI(TAG, "[%s] Stream %p muted but kept paced with the group", group->name, stream);
        return true;
    }
    bt_audio_le_tx_group_stop(group);
    return false;
}

void bt_audio_le_tx_group_member_detach(bt_audio_le_stream_t *stream)
{
    if (!stream) {
        return;
    }

    bt_audio_le_tx_group_t *group = stream->tx_group;
    if (group) {
        bt_audio_le_tx_group_member_stopped(stream);
        stream->tx_state = BT_AUDIO_LE_TX_STATE_IDLE;
        size_t left = 0;
        for (size_t i = 0; i < group->member_cnt; i++) {
            if (group->members[i] == stream) {
                group->members[i] = NULL;
            } else if (group->members[i]) {
                left++;
            }
        }
        stream->tx_group = NULL;
        if (group->implicit && left == 0) {
            bt_audio_le_tx_group_destroy(group);
        }
    }
    if (stream->tx_credits) {
        vSemaphoreDelete(stream->tx_credits);
        stream->tx_credits = NULL;
    }
}

void bt_audio_le_tx_group_kick(bt_audio_le_stream_t *stream)
{
    if (!stream || !stream->tx_group) {
        return;
    }
    bt_audio_le_tx_group_t *group = stream->tx_group;
    if (!group->anchored && group->events) {
        xEventGroupSetBits(group->events, BT_AUDIO_LE_TX_EVT_TIMER);
    }
}

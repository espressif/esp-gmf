/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_timer.h"
#include "esp_ble_audio_bap_api.h"

#include "esp_bt_audio_stream.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  LE Audio stream base structure.
 */
typedef struct {
    esp_bt_audio_stream_base_t  base;               /*!< Public stream base */
    esp_ble_audio_bap_stream_t  bap_stream;         /*!< BAP stream object */
    uint32_t                    presentation_delay; /*!< Presentation delay in us */
    uint16_t                    seq;                /*!< ISO packet sequence */
    bool                        first_packet;       /*!< First RX packet flag */
    uint32_t                    tx_start_time;      /*!< TX start time in us */
    volatile bool               started;            /*!< Stream has entered started state */
    uint32_t                    iso_interval;       /*!< Controller ISO interval in us */
    uint32_t                    sdu_interval_us;    /*!< QoS SDU interval in us */
    esp_timer_handle_t          tx_timer;           /*!< Source pacing timer */
    EventGroupHandle_t          tx_events;          /*!< Source pacing task events */
    TaskHandle_t                tx_task;            /*!< Source pacing task */
    bool                        tx_task_ext_mem;    /*!< Source pacing task stack is in PSRAM */
    uint8_t                     tx_task_core_id;    /*!< Source pacing task core ID */
    uint8_t                     tx_task_prio;       /*!< Source pacing task priority */
    uint32_t                    tx_task_stack_size; /*!< Source pacing task stack size in bytes */
    uint32_t                    tx_interval_us;     /*!< Active pacing period in us */
    uint8_t                     tx_prime_cnt;       /*!< Initial prefill packet count */
    bool                        tx_anchored;        /*!< Pacing phase realigned */
    char                        tx_name[configMAX_TASK_NAME_LEN]; /*!< Source pacing task and timer name */
} bt_audio_le_stream_t;

/**
 * @brief  Allocate an LE stream object and register BAP stream callbacks.
 *
 * @param[out]  stream  Receives the new stream pointer; set to NULL on error.
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  If stream is NULL
 *       - ESP_ERR_NO_MEM       If allocation fails
 *       - Other                non-zero codes from esp_ble_audio_bap_stream_cb_register
 */
esp_err_t bt_audio_le_stream_create(bt_audio_le_stream_t **stream);

/**
 * @brief  Destroy a stream, drain its queue, and free codec configuration memory.
 *
 * @param[in]  stream  Stream returned by @ref bt_audio_le_stream_create (may be NULL).
 */
void bt_audio_le_stream_destroy(bt_audio_le_stream_t *stream);

/**
 * @brief  Locate the LE stream wrapper for a BAP stream pointer.
 *
 * @param[in]   bap_stream  Registered BAP stream.
 * @param[out]  stream      Receives the LE stream wrapper.
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  If a pointer argument is NULL
 */
esp_err_t bt_audio_le_stream_find_by_bap_stream(esp_ble_audio_bap_stream_t *bap_stream, bt_audio_le_stream_t **stream);

/**
 * @brief  Notify the user pipeline that stream resources are allocated (codec filled).
 *
 * @param[in]  stream  Target stream.
 */
void bt_audio_le_stream_dispatch_allocated(bt_audio_le_stream_t *stream);

/**
 * @brief  Dispatch stream state change to the event hub.
 *
 * @param[in]  stream  Target stream.
 * @param[in]  state   New @c esp_bt_audio_stream_state_t value.
 */
void bt_audio_le_stream_dispatch_state(bt_audio_le_stream_t *stream, esp_bt_audio_stream_state_t state);

/**
 * @brief  Configure the LE source send task for one stream.
 *
 * @param[in]  stream      Stream returned by @ref bt_audio_le_stream_create
 * @param[in]  core_id     Core ID, 0 or 1
 * @param[in]  prio        Task priority, less than 24
 * @param[in]  stack_size  Stack size in bytes, must be greater than 0
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  If stream is NULL or a parameter is out of range
 */
esp_err_t bt_audio_le_stream_set_tx_task_cfg(bt_audio_le_stream_t *stream, uint8_t core_id, uint8_t prio, uint32_t stack_size);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

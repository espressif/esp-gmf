/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "driver/i2s_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#if CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM

/**
 * @brief  Opaque handle for LE playback synchronization (I2S / modem ETM)
 */
typedef struct esp_bt_audio_le_playback_sync *esp_bt_audio_le_playback_sync_handle_t;

/**
 * @brief  Opaque handle for LE playback clock synchronization (modem ETM -> I2S FIFO sync)
 */
typedef struct esp_bt_audio_le_clk_sync *esp_bt_audio_le_clk_sync_handle_t;

/**
 * @brief  Clock synchronization monitor message.
 */
typedef struct {
    int32_t   diff;      /*!< FIFO count difference reported by I2S sync check */
    uint32_t  fifo_cnt;  /*!< I2S TX FIFO counter sampled in the sync ISR */
    uint32_t  bck_cnt;   /*!< I2S TX BCK counter sampled in the sync ISR */
} esp_bt_audio_le_clk_sync_msg_t;

/**
 * @brief  Initialize LE playback synchronization (modem ETM -> I2S start)
 *
 *         Allocates internal resources; the caller must release the handle with
 *         esp_bt_audio_le_playback_sync_deinit().
 *
 * @param[in]   tx_handle   I2S TX channel handle
 * @param[out]  out_handle  On success, set to a valid handle; on error, set to NULL
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  If out_handle or tx_handle is NULL
 *       - ESP_ERR_NO_MEM       If heap allocation fails
 *       - Others               Error code from ETM or modem setup
 */
esp_err_t esp_bt_audio_le_playback_sync_init(i2s_chan_handle_t tx_handle,
                                             esp_bt_audio_le_playback_sync_handle_t *out_handle);

/**
 * @brief  Enable LE playback synchronization
 *
 * @param[in]  handle  Handle populated by esp_bt_audio_le_playback_sync_init()
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid argument
 *       - Others               Error code from esp_etm_channel_enable()
 */
esp_err_t esp_bt_audio_le_playback_sync_enable(esp_bt_audio_le_playback_sync_handle_t handle);

/**
 * @brief  Disable LE playback synchronization
 *
 * @param[in]  handle  Handle populated by esp_bt_audio_le_playback_sync_init()
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid argument
 *       - Others               Error code from esp_etm_channel_disable()
 */
esp_err_t esp_bt_audio_le_playback_sync_disable(esp_bt_audio_le_playback_sync_handle_t handle);

/**
 * @brief  Deinitialize LE playback synchronization
 *
 *         After success, handle must not be used again.
 *
 * @param[in]  handle  Handle populated by esp_bt_audio_le_playback_sync_init()
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid argument
 */
esp_err_t esp_bt_audio_le_playback_sync_deinit(esp_bt_audio_le_playback_sync_handle_t handle);

/**
 * @brief  Initialize LE playback clock synchronization
 *
 *         The sync interrupt reports FIFO count difference and raw counters to monitor_queue.
 *         The queue item type must be esp_bt_audio_le_clk_sync_msg_t.
 *
 * @param[in]   tx_handle       I2S TX channel handle
 * @param[in]   ideal_cnt       Ideal FIFO count at each BLE timing event
 * @param[in]   diff_threshold  Difference threshold for sync interrupt
 * @param[in]   hw_sync_enable  Enable I2S hardware data supplement; otherwise software should adjust data
 * @param[in]   monitor_queue   Queue used to report esp_bt_audio_le_clk_sync_msg_t, can be NULL
 * @param[out]  out_handle      On success, set to a valid handle; on error, set to NULL
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  If out_handle or tx_handle is NULL
 *       - ESP_ERR_NO_MEM       If heap allocation fails
 *       - Others               Error code from I2S/ETM setup
 */
esp_err_t esp_bt_audio_le_clk_sync_init(i2s_chan_handle_t tx_handle,
                                        uint32_t ideal_cnt,
                                        uint32_t diff_threshold,
                                        bool hw_sync_enable,
                                        QueueHandle_t monitor_queue,
                                        esp_bt_audio_le_clk_sync_handle_t *out_handle);

/**
 * @brief  Enable LE playback clock synchronization
 *
 * @param[in]  handle  Handle populated by esp_bt_audio_le_clk_sync_init()
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid argument
 */
esp_err_t esp_bt_audio_le_clk_sync_enable(esp_bt_audio_le_clk_sync_handle_t handle);

/**
 * @brief  Disable LE playback clock synchronization
 *
 * @param[in]  handle  Handle populated by esp_bt_audio_le_clk_sync_init()
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid argument
 */
esp_err_t esp_bt_audio_le_clk_sync_disable(esp_bt_audio_le_clk_sync_handle_t handle);

/**
 * @brief  Deinitialize LE playback clock synchronization
 *
 * @param[in]  handle  Handle populated by esp_bt_audio_le_clk_sync_init()
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid argument
 */
esp_err_t esp_bt_audio_le_clk_sync_deinit(esp_bt_audio_le_clk_sync_handle_t handle);

#endif  /* CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM */

#ifdef __cplusplus
}
#endif  /* __cplusplus */

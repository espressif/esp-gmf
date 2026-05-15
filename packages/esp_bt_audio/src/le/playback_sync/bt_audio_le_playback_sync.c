/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "driver/i2s_etm.h"
#include "esp_bt_audio_le_playback_sync.h"
#include "esp_check.h"
#include "esp_etm.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "modem/modem_etm.h"

/**
 * @brief  ETM resources used to start I2S from the BLE ISO timing event.
 */
struct esp_bt_audio_le_playback_sync {
    esp_etm_channel_handle_t  etm_ch;          /*!< ETM channel connecting modem event to I2S task */
    esp_etm_task_handle_t     i2s_start_task;  /*!< I2S start ETM task */
    esp_etm_event_handle_t    modem_event;     /*!< Modem ETM timing event */
};

static const char *TAG = "le_playback_sync";

#if CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM

esp_err_t esp_bt_audio_le_playback_sync_init(i2s_chan_handle_t tx_handle,
                                             esp_bt_audio_le_playback_sync_handle_t *out_handle)
{
    esp_err_t ret = ESP_OK;
    esp_bt_audio_le_playback_sync_handle_t sync = NULL;

    ESP_RETURN_ON_FALSE(out_handle, ESP_ERR_INVALID_ARG, TAG, "Init failed: out_handle is NULL");
    *out_handle = NULL;
    ESP_RETURN_ON_FALSE(tx_handle, ESP_ERR_INVALID_ARG, TAG, "Init failed: tx_handle is NULL");

    sync = heap_caps_calloc_prefer(1, sizeof(struct esp_bt_audio_le_playback_sync), 2,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                   MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(sync, ESP_ERR_NO_MEM, TAG, "Allocation failed: playback sync context");

    esp_etm_channel_config_t etm_config = {};
    i2s_etm_task_config_t i2s_task_cfg = {
        .task_type = I2S_ETM_TASK_START,
    };
    ESP_GOTO_ON_ERROR(i2s_new_etm_task(tx_handle, &i2s_task_cfg, &sync->i2s_start_task),
                      err, TAG, "I2S ETM task allocation failed");

    modem_etm_event_config_t modem_event_cfg = {
        .event_type = MODEM_ETM_EVENT_G1,
    };
    ESP_GOTO_ON_ERROR(modem_new_etm_event(&modem_event_cfg, &sync->modem_event),
                      err, TAG, "Modem ETM event allocation failed");
    ESP_GOTO_ON_ERROR(esp_etm_new_channel(&etm_config, &sync->etm_ch),
                      err, TAG, "ETM channel allocation failed");
    ESP_GOTO_ON_ERROR(esp_etm_channel_connect(sync->etm_ch, sync->modem_event, sync->i2s_start_task),
                      err, TAG, "ETM channel connect failed");

    *out_handle = sync;
    return ESP_OK;

err:
    if (sync) {
        if (sync->etm_ch) {
            esp_etm_del_channel(sync->etm_ch);
        }
        if (sync->i2s_start_task) {
            esp_etm_del_task(sync->i2s_start_task);
        }
        if (sync->modem_event) {
            esp_etm_del_event(sync->modem_event);
        }
        heap_caps_free(sync);
        sync = NULL;
    }
    *out_handle = NULL;
    ESP_LOGE(TAG, "Init failed: %s", esp_err_to_name(ret));
    return ret;
}

esp_err_t esp_bt_audio_le_playback_sync_enable(esp_bt_audio_le_playback_sync_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Enable failed: handle is NULL");
    esp_err_t err = esp_etm_channel_enable(handle->etm_ch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Enable failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t esp_bt_audio_le_playback_sync_disable(esp_bt_audio_le_playback_sync_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Disable failed: handle is NULL");
    esp_err_t err = esp_etm_channel_disable(handle->etm_ch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Disable failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t esp_bt_audio_le_playback_sync_deinit(esp_bt_audio_le_playback_sync_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Deinit failed: handle is NULL");
    esp_etm_del_channel(handle->etm_ch);
    esp_etm_del_task(handle->i2s_start_task);
    esp_etm_del_event(handle->modem_event);
    heap_caps_free(handle);
    handle = NULL;
    return ESP_OK;
}

#endif  /* CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_AUDIO && CONFIG_BT_ISO && CONFIG_SOC_MODEM_SUPPORT_ETM */

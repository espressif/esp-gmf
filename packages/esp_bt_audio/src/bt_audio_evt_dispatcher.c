/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "bt_audio_evt_dispatcher.h"

typedef struct {
    esp_bt_audio_event_cb_t  event_cb[ESP_BT_AUDIO_EVT_DST_MAX];        /*!< Event callback functions */
    void                    *event_user_ctx[ESP_BT_AUDIO_EVT_DST_MAX];  /*!< User context for the event callback */
} bt_audio_evt_dispatcher_t;

static const char *TAG = "BT_AUD_EVT";
static bt_audio_evt_dispatcher_t *s_evt_dispatcher;

esp_err_t bt_audio_evt_dispatcher_init()
{
    if (s_evt_dispatcher) {
        ESP_LOGE(TAG, "Bluetooth event dispatcher is already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    s_evt_dispatcher = heap_caps_calloc_prefer(1, sizeof(bt_audio_evt_dispatcher_t), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(s_evt_dispatcher, ESP_ERR_NO_MEM, TAG, "Failed to malloc space for evt_dispatcher");
    return ESP_OK;
}

esp_err_t bt_audio_evt_dispatcher_cb_register(esp_bt_audio_evt_dst_t dst, esp_bt_audio_event_cb_t event_cb, void *event_user_ctx)
{
    if (!s_evt_dispatcher) {
        ESP_LOGE(TAG, "Bluetooth event dispatcher is not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_FALSE(dst < ESP_BT_AUDIO_EVT_DST_MAX, ESP_ERR_INVALID_ARG, TAG, "Invalid event dst %d", dst);
    s_evt_dispatcher->event_cb[dst] = event_cb;
    s_evt_dispatcher->event_user_ctx[dst] = event_user_ctx;

    return ESP_OK;
}

void bt_audio_evt_dispatcher_deinit()
{
    if (s_evt_dispatcher) {
        free(s_evt_dispatcher);
        s_evt_dispatcher = NULL;
    }
}

esp_err_t bt_audio_evt_dispatch(esp_bt_audio_evt_dst_t dst, esp_bt_audio_event_t event, void *event_data)
{
    if (!s_evt_dispatcher) {
        ESP_LOGE(TAG, "Bluetooth event dispatcher is not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_FALSE(dst < ESP_BT_AUDIO_EVT_DST_MAX, ESP_ERR_INVALID_ARG, TAG, "Invalid event dst %d", dst);
    if (s_evt_dispatcher->event_cb[dst]) {
        s_evt_dispatcher->event_cb[dst](event, event_data, s_evt_dispatcher->event_user_ctx[dst]);
    }

    return ESP_OK;
}

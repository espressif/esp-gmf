/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "esp_err.h"

#include "esp_bt_audio.h"
#include "esp_bt_audio_host.h"

#ifdef CONFIG_BT_CLASSIC_ENABLED
#include "bt_audio_classic.h"
#endif  /* CONFIG_BT_CLASSIC_ENABLED */

#include "bt_audio_ops.h"
#include "bt_audio_evt_dispatcher.h"

static bool s_bt_audio_inited = false;
static bool s_bt_host_inited = false;
static const char *TAG = "BT_AUDIO";

esp_err_t esp_bt_audio_init(esp_bt_audio_config_t *bt_config)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(!s_bt_audio_inited, ESP_ERR_INVALID_STATE, TAG, "BT Audio already initialized");
    ESP_RETURN_ON_FALSE(bt_config != NULL, ESP_ERR_INVALID_ARG, TAG, "bt_config is NULL");
    ESP_GOTO_ON_ERROR(bt_audio_ops_init(), error, TAG, "Failed to init bt ops");
    ESP_GOTO_ON_ERROR(bt_audio_evt_dispatcher_init(), error, TAG, "Failed to init evt");
    ESP_GOTO_ON_ERROR(bt_audio_evt_dispatcher_cb_register(ESP_BT_AUDIO_EVT_DST_USR, bt_config->event_cb, bt_config->event_user_ctx), error, TAG, "Failed to register evt cb");
    if (bt_config->host_config) {
        ESP_GOTO_ON_ERROR(esp_bt_audio_host_init(bt_config->host_config), error, TAG, "Failed to init bt host");
        s_bt_host_inited = true;
    }
    s_bt_audio_inited = true;

#if CONFIG_BT_CLASSIC_ENABLED
    if (bt_config->classic.roles) {
        ESP_GOTO_ON_ERROR(bt_audio_classic_init(&bt_config->classic), error, TAG, "Classic Audio init failed");
    }
#endif  /* CONFIG_BT_CLASSIC_ENABLED */
    return ret;

error:
    esp_bt_audio_deinit();
    return ret;
}

void esp_bt_audio_deinit(void)
{
    bt_audio_ops_deinit();
    bt_audio_evt_dispatcher_deinit();
#ifdef CONFIG_BT_CLASSIC_ENABLED
    bt_audio_classic_deinit();
#endif  /* CONFIG_BT_CLASSIC_ENABLED */
    if (s_bt_host_inited) {
        esp_bt_audio_host_deinit();
        s_bt_host_inited = false;
    }
    s_bt_audio_inited = false;
}
